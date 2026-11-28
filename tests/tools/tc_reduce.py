#!/usr/bin/env python3
"""Deterministic source reducer for the private tc-vm/tc-aot tools."""

from __future__ import annotations

import ctypes
import errno
import fcntl
import json
import os
import re
import select
import shutil
import signal
import stat
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from typing import Optional

EXIT_OK = 0
EXIT_DIAGNOSTIC = 1
EXIT_USAGE = 2
EXIT_UNAVAILABLE = 70
EXIT_INTERNAL = 71
MAX_SOURCE = 4 * 1024 * 1024
MAX_DIAG = 4 * 1024 * 1024
IMPORT_RE = re.compile(rb"\bimport\s+([A-Za-z_][A-Za-z0-9_]*)")
INTEGER_RE = re.compile(rb"(?<![A-Za-z0-9_])[0-9]+(?![A-Za-z0-9_])")
FLOAT_RE = re.compile(
    rb"(?<![A-Za-z0-9_])(?:[0-9]+\.[0-9]*|\.[0-9]+)(?:[eE][+-]?[0-9]+)?(?![A-Za-z0-9_])"
)
STRING_RE = re.compile(rb'"(?:[^"\\]|\\.)*"')
CHAR_RE = re.compile(rb"'(?:[^'\\]|\\.)*'")


@dataclass
class Fingerprint:
    code: str
    exit_status: int
    termination: str


@dataclass
class RunResult:
    fingerprint: Optional[Fingerprint]
    exit_status: int
    termination: str
    protocol_error: bool = False
    exec_error: bool = False
    sentinel_error: bool = False
    capture_error: bool = False
    timed_out: bool = False
    detail: str = ""


class ReduceFailure(Exception):
    def __init__(self, code: int, message: str = "") -> None:
        super().__init__(message)
        self.code = code
        self.message = message


class Reducer:
    def __init__(self, source: str, timeout: Optional[int], stdin_path: Optional[str],
                 output_path: Optional[str], tool: str, tool_args: list[str]) -> None:
        self.source_arg = source
        self.source = os.path.realpath(source)
        self.timeout = timeout
        self.stdin_arg = stdin_path
        self.output_arg = output_path
        self.tool_arg = tool
        self.tool_args = tool_args
        self.best = b""
        self.fingerprint: Optional[Fingerprint] = None
        self.deadline: Optional[float] = None
        self.stderr_safe = False
        self.cleanup_ok = True
        self.candidate_serial = 1
        self.result_serial = 1
        self.exec_fd = -1
        self.tool_path = ""
        self.tool_name = ""
        self.stdin_snapshot = ""
        self.stdin_identity: Optional[tuple[int, int]] = None
        self.source_identity: Optional[tuple[int, int]] = None
        self.manifest_path: Optional[str] = None
        self.manifest_identity: Optional[tuple[int, int]] = None
        self.project_root = os.path.dirname(self.source)
        self.include_roots: list[str] = []
        self.protected_ids: set[tuple[int, int]] = set()
        self.protected_paths: set[str] = set()
        self.import_names: set[str] = set()
        self.result_mode = "stdout"
        self.result_path: Optional[str] = None
        self.result_identity: Optional[tuple[int, int]] = None
        self.result_name_key: Optional[str] = None
        self.child_output_specs: list[tuple[str, str]] = []
        self.static_usage_checked = False

    def note(self, message: str) -> None:
        if not self.stderr_safe:
            return
        try:
            os.write(2, (message + "\n").encode("utf-8", "replace"))
        except OSError:
            pass

    def fail(self, code: int, message: str) -> None:
        raise ReduceFailure(code, message)

    @staticmethod
    def identity(path: str) -> Optional[tuple[int, int]]:
        try:
            st = os.stat(path)
        except OSError:
            return None
        return st.st_dev, st.st_ino

    @staticmethod
    def fd_identity(fd: int) -> Optional[tuple[int, int]]:
        try:
            st = os.fstat(fd)
        except OSError:
            return None
        return st.st_dev, st.st_ino

    @staticmethod
    def path_name_key(path: str) -> str:
        parent = os.path.realpath(os.path.dirname(os.path.abspath(path)))
        return os.path.normcase(os.path.join(parent, os.path.basename(path)))

    @staticmethod
    def read_file(path: str, limit: int = MAX_SOURCE) -> bytes:
        try:
            st = os.stat(path)
            if not stat.S_ISREG(st.st_mode) or st.st_size < 0 or st.st_size > limit:
                raise OSError(errno.EINVAL)
            with open(path, "rb") as stream:
                data = stream.read(limit + 1)
        except OSError as exc:
            raise ReduceFailure(EXIT_UNAVAILABLE, "cannot read input") from exc
        if len(data) > limit:
            raise ReduceFailure(EXIT_INTERNAL, "input exceeds implementation limit")
        return data

    def parse_manifest(self) -> None:
        directory = os.path.dirname(self.source)
        while True:
            candidate = os.path.join(directory, "tc.project")
            if os.path.lexists(candidate):
                self.manifest_path = candidate
                self.project_root = directory
                self.manifest_identity = self.identity(candidate)
                if self.manifest_identity is not None:
                    self.protected_ids.add(self.manifest_identity)
                self.protected_paths.add(os.path.realpath(candidate))
                try:
                    manifest_stat = os.stat(candidate)
                    if not stat.S_ISREG(manifest_stat.st_mode) or manifest_stat.st_size > MAX_SOURCE:
                        return
                    manifest_fd = os.open(candidate, os.O_RDONLY | os.O_NONBLOCK)
                    with os.fdopen(manifest_fd, "rb") as stream:
                        text = stream.read().decode("utf-8", "surrogateescape")
                except OSError:
                    return
                for raw_line in text.splitlines():
                    line = raw_line.strip()
                    if not line or line.startswith("#") or "=" not in line:
                        continue
                    key, value = line.split("=", 1)
                    key = key.strip()
                    value = value.strip()
                    if key != "include":
                        continue
                    if len(value) >= 2 and value[0] == value[-1] == '"':
                        value = value[1:-1]
                    if value:
                        root = value if os.path.isabs(value) else os.path.join(directory, value)
                        self.include_roots.append(os.path.realpath(root))
                if not self.include_roots:
                    self.include_roots.append(os.path.realpath(os.path.join(directory, "src")))
                return
            parent = os.path.dirname(directory)
            if parent == directory:
                return
            directory = parent

    def parse_cli_includes(self) -> None:
        for arg in self.tool_args:
            if arg.startswith("--include=") and len(arg) > len("--include="):
                value = arg[len("--include="):]
                self.include_roots.append(os.path.realpath(value))

    def scan_imports(self, data: bytes) -> list[str]:
        names: list[str] = []
        for match in IMPORT_RE.finditer(data):
            try:
                name = match.group(1).decode("ascii")
            except UnicodeDecodeError:
                continue
            names.append(name)
        return names

    def build_input_closure(self, data: bytes) -> None:
        self.source_identity = self.identity(self.source)
        if self.source_identity is None:
            self.fail(EXIT_UNAVAILABLE, "source is not readable")
        self.protected_ids.add(self.source_identity)
        self.protected_paths.add(self.source)
        self.protected_paths.add(self.path_name_key(self.source))
        queue: list[tuple[str, bytes]] = [(self.source, data)]
        visited: set[tuple[int, int]] = set()
        while queue:
            path, content = queue.pop(0)
            ident = self.identity(path)
            if ident is not None:
                if ident in visited:
                    continue
                visited.add(ident)
                self.protected_ids.add(ident)
            self.protected_paths.add(os.path.realpath(path))
            for name in self.scan_imports(content):
                self.import_names.add(name)
                filename = name + ".tc"
                for root in [os.path.dirname(path), *self.include_roots]:
                    candidate = os.path.join(root, filename)
                    self.protected_paths.add(os.path.realpath(candidate))
                    self.protected_paths.add(self.path_name_key(candidate))
                    candidate_id = self.identity(candidate)
                    if candidate_id is None:
                        continue
                    self.protected_ids.add(candidate_id)
                    try:
                        if os.path.getsize(candidate) > MAX_SOURCE:
                            continue
                        with open(candidate, "rb") as stream:
                            child = stream.read(MAX_SOURCE + 1)
                    except OSError:
                        continue
                    if len(child) <= MAX_SOURCE:
                        queue.append((candidate, child))

    def resolve_tool(self) -> None:
        resolved = shutil.which(self.tool_arg)
        if resolved is None:
            self.fail(EXIT_USAGE, "tool not found")
        resolved = os.path.realpath(resolved)
        self.tool_name = os.path.basename(resolved)
        if self.tool_name not in ("tc-vm", "tc-aot"):
            self.fail(EXIT_USAGE, "only tc-vm and tc-aot are supported")
        script_dir = os.path.dirname(os.path.realpath(__file__))
        expected = os.path.join(script_dir, self.tool_name)
        if not os.path.exists(expected):
            override = os.environ.get("TC_REDUCE_TOOL_DIR")
            if override:
                expected = os.path.join(override, self.tool_name)
        if not os.path.exists(expected):
            self.fail(EXIT_USAGE, "tool is not part of this installation")
        try:
            if not os.path.samefile(resolved, expected):
                self.fail(EXIT_USAGE, "tool is not part of this installation")
        except OSError as exc:
            raise ReduceFailure(EXIT_USAGE, "tool is not part of this installation") from exc
        env = dict(os.environ)
        env.pop("TC_REDUCE_DIAG_FD", None)
        env.pop("TC_REDUCE_EXEC_FD", None)
        try:
            auth = subprocess.run([resolved, "--tc-reduce-auth"], stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, env=env, timeout=5, check=False)
        except (OSError, subprocess.SubprocessError) as exc:
            raise ReduceFailure(EXIT_USAGE, "tool authentication failed") from exc
        if auth.returncode != 0:
            self.fail(EXIT_USAGE, "tool authentication failed")
        try:
            record = json.loads(auth.stdout.decode("utf-8"))
            st = os.stat(resolved)
        except (UnicodeDecodeError, ValueError, OSError) as exc:
            raise ReduceFailure(EXIT_USAGE, "tool authentication failed") from exc
        if (not isinstance(record, dict) or record.get("version") != "0.0.47" or
                record.get("dev") != st.st_dev or record.get("ino") != st.st_ino):
            self.fail(EXIT_USAGE, "tool authentication failed")
        try:
            self.exec_fd = os.open(resolved, os.O_RDONLY | os.O_CLOEXEC)
            fd_st = os.fstat(self.exec_fd)
        except OSError as exc:
            raise ReduceFailure(EXIT_UNAVAILABLE, "cannot open tool") from exc
        if (fd_st.st_dev, fd_st.st_ino) != (st.st_dev, st.st_ino):
            self.fail(EXIT_USAGE, "tool identity changed")
        self.tool_path = resolved

    def validate_tool_args(self) -> None:
        json_seen = False
        for arg in self.tool_args:
            if arg == "--diag-format=text":
                self.fail(EXIT_USAGE, "text diagnostics are not accepted")
            if arg == "--diag-format=json":
                if json_seen:
                    self.fail(EXIT_USAGE, "duplicate diagnostic format")
                json_seen = True
            elif arg.startswith("--diag-format="):
                self.fail(EXIT_USAGE, "invalid diagnostic format")
        self.child_output_specs = []
        output_options = ("--output=", "--trace-out=", "--stats-out=", "--gdb-elf-out=")
        index = 0
        while index < len(self.tool_args):
            arg = self.tool_args[index]
            matched = False
            for prefix in output_options:
                if arg.startswith(prefix):
                    value = arg[len(prefix):]
                    if not value:
                        self.fail(EXIT_USAGE, "empty output path")
                    if "{}" in value:
                        self.fail(EXIT_USAGE, "child output cannot use the candidate placeholder")
                    self.child_output_specs.append((prefix[:-1], value))
                    matched = True
                    break
            if matched:
                index += 1
                continue
            if arg in ("--dump-ir", "--dump-linemap"):
                self.child_output_specs.append((arg, ""))
                index += 1
                continue
            if arg.startswith("--dump-ir=") or arg.startswith("--dump-linemap="):
                option, value = arg.split("=", 1)
                if not value:
                    self.fail(EXIT_USAGE, "empty output path")
                if "{}" in value:
                    self.fail(EXIT_USAGE, "child output cannot use the candidate placeholder")
                self.child_output_specs.append((option, value))
            index += 1

    def setup_stdin(self) -> None:
        if self.stdin_arg is None:
            return
        path = os.path.realpath(self.stdin_arg)
        try:
            st = os.stat(path)
            if not stat.S_ISREG(st.st_mode):
                self.fail(EXIT_UNAVAILABLE, "stdin is not a regular file")
            self.stdin_identity = (st.st_dev, st.st_ino)
            self.protected_ids.add(self.stdin_identity)
            self.protected_paths.add(path)
            self.protected_paths.add(self.path_name_key(path))
            fd, snapshot = tempfile.mkstemp(prefix="tc-reduce-stdin-")
            self.stdin_snapshot = snapshot
            os.fchmod(fd, 0o400)
            with os.fdopen(fd, "wb") as out, open(path, "rb") as src:
                while True:
                    chunk = src.read(1024 * 1024)
                    if not chunk:
                        break
                    out.write(chunk)
                out.flush()
                os.fsync(out.fileno())
        except ReduceFailure:
            raise
        except OSError as exc:
            raise ReduceFailure(EXIT_UNAVAILABLE, "cannot snapshot stdin") from exc

    def resolve_result_target(self) -> None:
        if self.output_arg is None:
            self.result_mode = "stdout"
            return
        raw = os.path.abspath(self.output_arg)
        resolved_raw = os.path.realpath(raw)
        fd1 = self.fd_identity(1)
        fd2 = self.fd_identity(2)
        if raw in ("/dev/stdout", "/dev/fd/1", "/proc/self/fd/1") or \
                resolved_raw in ("/dev/stdout", "/dev/fd/1", "/proc/self/fd/1"):
            self.result_mode = "fd1"
            return
        if raw in ("/dev/stderr", "/dev/fd/2", "/proc/self/fd/2") or \
                resolved_raw in ("/dev/stderr", "/dev/fd/2", "/proc/self/fd/2"):
            self.fail(EXIT_USAGE, "result aliases stderr")
        try:
            st = os.stat(raw)
        except OSError:
            st = None
        if st is not None and fd1 is not None and (st.st_dev, st.st_ino) == fd1:
            self.result_mode = "fd1"
            return
        if st is not None and fd2 is not None and (st.st_dev, st.st_ino) == fd2:
            self.fail(EXIT_USAGE, "result aliases stderr")
        expected = resolved_raw
        name_key = self.path_name_key(raw)
        same_source_name = name_key == self.path_name_key(self.source)
        if st is not None:
            if not stat.S_ISREG(st.st_mode):
                self.fail(EXIT_USAGE, "result is not a regular file")
            ident = (st.st_dev, st.st_ino)
            if ident in self.protected_ids:
                if not (same_source_name and ident == self.source_identity):
                    self.fail(EXIT_USAGE, "result aliases an input")
            if fd1 is not None and ident == fd1:
                self.result_mode = "fd1"
                return
            self.result_identity = ident
        else:
            if expected in self.protected_paths or name_key in self.protected_paths:
                if not (same_source_name and expected == self.source):
                    self.fail(EXIT_USAGE, "result aliases an input")
            flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
            if hasattr(os, "O_NOFOLLOW"):
                flags |= os.O_NOFOLLOW
            try:
                probe_fd = os.open(expected, flags, 0o600)
            except FileExistsError:
                try:
                    probe_stat = os.stat(expected)
                except OSError as exc:
                    raise ReduceFailure(EXIT_UNAVAILABLE, "cannot inspect result target") from exc
                probe_ident = (probe_stat.st_dev, probe_stat.st_ino)
                if not stat.S_ISREG(probe_stat.st_mode):
                    self.fail(EXIT_USAGE, "result is not a regular file")
                if probe_ident in self.protected_ids:
                    self.fail(EXIT_USAGE, "result aliases an input")
                self.result_identity = probe_ident
            except OSError as exc:
                raise ReduceFailure(EXIT_UNAVAILABLE, "cannot probe result target") from exc
            else:
                probe_ident: Optional[tuple[int, int]] = None
                try:
                    probe_stat = os.fstat(probe_fd)
                    probe_ident = (probe_stat.st_dev, probe_stat.st_ino)
                    if (probe_ident in self.protected_ids or
                            name_key in self.protected_paths):
                        self.fail(EXIT_USAGE, "result aliases an input")
                finally:
                    os.close(probe_fd)
                    if probe_ident is not None:
                        try:
                            current = os.lstat(expected)
                            if (current.st_dev, current.st_ino) != probe_ident:
                                self.cleanup_ok = False
                            else:
                                os.unlink(expected)
                        except FileNotFoundError:
                            self.cleanup_ok = False
                        except OSError:
                            self.cleanup_ok = False
                if not self.cleanup_ok:
                    self.fail(EXIT_UNAVAILABLE, "cannot clean result probe")
        if fd2 is not None and self.result_identity == fd2:
            self.fail(EXIT_USAGE, "result aliases stderr")
        if fd1 is not None and self.result_identity == fd1:
            self.result_mode = "fd1"
            return
        self.result_mode = "file"
        self.result_path = expected
        self.result_name_key = name_key

    def preflight_stdio(self) -> None:
        fd0 = self.fd_identity(0)
        fd1 = self.fd_identity(1)
        fd2 = self.fd_identity(2)
        if fd1 is not None and fd1 == fd2:
            self.fail(EXIT_USAGE, "stdout and stderr alias")
        if fd2 is not None and fd2 in self.protected_ids:
            self.fail(EXIT_USAGE, "stderr aliases an input")
        if fd1 is not None and fd1 in self.protected_ids:
            self.fail(EXIT_USAGE, "stdout aliases an input")
        if fd0 is not None and fd0 in self.protected_ids:
            self.fail(EXIT_USAGE, "stdin aliases an input")
        if self.result_mode == "file" and fd2 is not None and self.result_identity == fd2:
            self.fail(EXIT_USAGE, "result aliases stderr")

    def substitute(self, value: str, candidate: str) -> str:
        return value.replace("{}", candidate)

    def resolved_output_path(self, value: str, candidate: str) -> str:
        return os.path.realpath(self.substitute(value, candidate))

    def preflight_child_outputs(self, candidate: str) -> None:
        seen_paths: set[str] = set()
        seen_ids: set[tuple[int, int]] = set()
        candidate_id = self.identity(candidate)
        for option, value in self.child_output_specs:
            if not value:
                continue
            raw = self.substitute(value, candidate)
            if raw in ("/dev/stdout", "/dev/stderr", "/proc/self/fd/1",
                       "/proc/self/fd/2", "/dev/fd/1", "/dev/fd/2"):
                self.fail(EXIT_USAGE, "child output aliases a standard stream")
            path = os.path.realpath(raw)
            name_key = self.path_name_key(raw)
            if path == candidate or name_key == self.path_name_key(candidate):
                self.fail(EXIT_USAGE, "child output aliases the candidate")
            if path in seen_paths or name_key in seen_paths:
                self.fail(EXIT_USAGE, "child outputs alias")
            seen_paths.add(path)
            seen_paths.add(name_key)
            try:
                st = os.stat(raw)
            except OSError:
                st = None
            if st is not None:
                if not stat.S_ISREG(st.st_mode):
                    self.fail(EXIT_USAGE, "child output is not a regular file")
                ident = (st.st_dev, st.st_ino)
                if ident in self.protected_ids or ident == candidate_id:
                    self.fail(EXIT_USAGE, "child output aliases an input")
                if ident in seen_ids:
                    self.fail(EXIT_USAGE, "child outputs alias")
                seen_ids.add(ident)
                if self.result_identity is not None and ident == self.result_identity:
                    self.fail(EXIT_USAGE, "child output aliases the result")
            elif path in self.protected_paths or name_key in self.protected_paths:
                self.fail(EXIT_USAGE, "child output aliases an input")
            if self.result_path is not None and (
                    path == self.result_path or name_key == self.result_name_key):
                self.fail(EXIT_USAGE, "child output aliases the result")

    def candidate_path(self, content: bytes) -> str:
        directory = os.path.dirname(self.source)
        while True:
            name = f"tc_reduce_{self.candidate_serial}.tc"
            self.candidate_serial += 1
            if name[:-3] in self.import_names:
                continue
            path = os.path.join(directory, name)
            if os.path.lexists(path):
                continue
            if self.result_path is not None and self.path_name_key(path) == self.result_name_key:
                continue
            return path

    def create_candidate(self, path: str, content: bytes) -> tuple[int, tuple[int, int]]:
        flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
        if hasattr(os, "O_NOFOLLOW"):
            flags |= os.O_NOFOLLOW
        try:
            fd = os.open(path, flags, 0o600)
        except FileExistsError:
            raise
        except OSError as exc:
            raise ReduceFailure(EXIT_UNAVAILABLE, "cannot create candidate") from exc
        try:
            os.fchmod(fd, 0o600)
            view = memoryview(content)
            while view:
                wrote = os.write(fd, view)
                view = view[wrote:]
            st = os.fstat(fd)
            return fd, (st.st_dev, st.st_ino)
        except OSError as exc:
            try:
                os.close(fd)
            except OSError:
                pass
            try:
                now = os.lstat(path)
                if (now.st_dev, now.st_ino) == (st.st_dev, st.st_ino):
                    os.unlink(path)
            except (OSError, UnboundLocalError):
                pass
            raise ReduceFailure(EXIT_UNAVAILABLE, "cannot write candidate") from exc

    def remove_candidate(self, path: str, ident: tuple[int, int]) -> None:
        try:
            st = os.lstat(path)
            if (st.st_dev, st.st_ino) != ident:
                self.cleanup_ok = False
                return
            os.unlink(path)
        except FileNotFoundError:
            self.cleanup_ok = False
        except OSError:
            self.cleanup_ok = False

    @staticmethod
    def process_start_token(pid: int) -> Optional[str]:
        try:
            with open(f"/proc/{pid}/stat", "rb") as stream:
                fields = stream.read().split()
                return fields[21].decode("ascii")
        except (OSError, IndexError, UnicodeDecodeError):
            try:
                out = subprocess.check_output(["ps", "-o", "lstart=", "-p", str(pid)],
                                              stderr=subprocess.DEVNULL, timeout=2)
                return out.decode("utf-8", "replace").strip()
            except (OSError, subprocess.SubprocessError):
                return None

    def exec_authenticated(self, args: list[str], env: dict[str, str]) -> None:
        libc = ctypes.CDLL(None, use_errno=True)
        fexecve = getattr(libc, "fexecve", None)
        if fexecve is not None:
            argv = [os.fsencode(value) for value in args]
            envp = [os.fsencode(f"{key}={value}") for key, value in env.items()]
            argv_array = (ctypes.c_char_p * (len(argv) + 1))(*argv, None)
            envp_array = (ctypes.c_char_p * (len(envp) + 1))(*envp, None)
            fexecve.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_char_p),
                                ctypes.POINTER(ctypes.c_char_p)]
            fexecve.restype = ctypes.c_int
            if fexecve(self.exec_fd, argv_array, envp_array) == 0:
                os._exit(127)
            error = ctypes.get_errno()
            raise OSError(error, os.strerror(error))
        try:
            descriptor = os.fstat(self.exec_fd)
        except OSError as exc:
            raise OSError(exc.errno, f"cannot stat executable descriptor: {exc}") from exc
        try:
            path = os.stat(self.tool_path)
        except OSError as exc:
            raise OSError(exc.errno, f"cannot stat executable path: {exc}") from exc
        if (descriptor.st_dev, descriptor.st_ino) != (path.st_dev, path.st_ino):
            raise OSError(errno.ESTALE, "tool identity changed")
        try:
            os.execve(self.tool_path, args, env)
        except OSError as exc:
            raise OSError(exc.errno, f"cannot execute tool path: {exc}") from exc

    def execute(self, candidate: str) -> RunResult:
        if self.deadline is not None and time.monotonic() >= self.deadline:
            return RunResult(None, EXIT_DIAGNOSTIC, "timeout", timed_out=True)
        stdout_r, stdout_w = os.pipe()
        stderr_r, stderr_w = os.pipe()
        diag_r, diag_w = os.pipe()
        status_r, status_w = os.pipe()
        cleanup_r, cleanup_w = os.pipe()
        sentinel_pid = -1
        tool_pid = -1
        start_token: Optional[str] = None
        try:
            sentinel_pid = os.fork()
        except OSError as exc:
            for fd in (stdout_r, stdout_w, stderr_r, stderr_w, diag_r, diag_w,
                       status_r, status_w, cleanup_r, cleanup_w):
                try:
                    os.close(fd)
                except OSError:
                    pass
            raise ReduceFailure(EXIT_UNAVAILABLE, "cannot fork sentinel") from exc
        if sentinel_pid == 0:
            try:
                os.close(stdout_r)
                os.close(stderr_r)
                os.close(diag_r)
                os.close(status_r)
                os.close(cleanup_w)
                os.setsid()
                tool_pid = os.fork()
                if tool_pid == 0:
                    try:
                        signal.signal(signal.SIGPIPE, signal.SIG_DFL)
                        os.close(cleanup_r)
                        if self.stdin_snapshot:
                            stdin_fd = os.open(self.stdin_snapshot, os.O_RDONLY)
                            os.lseek(stdin_fd, 0, os.SEEK_SET)
                        else:
                            stdin_fd = os.open(os.devnull, os.O_RDONLY)
                        os.dup2(stdin_fd, 0)
                        if stdin_fd != 0:
                            os.close(stdin_fd)
                        os.dup2(stdout_w, 1)
                        os.dup2(stderr_w, 2)
                        os.close(stdout_w)
                        os.close(stderr_w)
                        os.set_inheritable(diag_w, True)
                        os.set_inheritable(self.exec_fd, True)
                        env = dict(os.environ)
                        env["TC_REDUCE_DIAG_FD"] = str(diag_w)
                        env["TC_REDUCE_EXEC_FD"] = str(self.exec_fd)
                        args = self.tool_args_for(candidate)
                        self.exec_authenticated(args, env)
                    except BaseException as exc:
                        try:
                            os.write(status_w, json.dumps(
                                {"type": "exec-error", "detail": str(exc)}).encode() + b"\n")
                        except OSError:
                            pass
                        os._exit(127)
                os.close(stdout_w)
                os.close(stderr_w)
                os.close(diag_w)
                os.write(status_w, json.dumps({"type": "start", "pid": tool_pid}).encode() + b"\n")
                tool_status: Optional[int] = None
                reported = False
                while True:
                    try:
                        ready, _, _ = select.select([cleanup_r], [], [], 0.05)
                    except InterruptedError:
                        continue
                    if ready:
                        try:
                            os.read(cleanup_r, 1)
                        except OSError:
                            pass
                        try:
                            os.killpg(os.getpgrp(), signal.SIGKILL)
                        except OSError:
                            pass
                        break
                    if tool_status is None:
                        try:
                            pid, status_value = os.waitpid(tool_pid, os.WNOHANG)
                        except ChildProcessError:
                            pid, status_value = tool_pid, 0
                        if pid == tool_pid:
                            tool_status = status_value
                            if not reported:
                                os.write(status_w, json.dumps(
                                    {"type": "exit", "status": status_value}).encode() + b"\n")
                                reported = True
                if tool_status is None:
                    try:
                        _, tool_status = os.waitpid(tool_pid, 0)
                    except ChildProcessError:
                        tool_status = 0
                if not reported:
                    try:
                        os.write(status_w, json.dumps(
                            {"type": "exit", "status": tool_status}).encode() + b"\n")
                    except OSError:
                        pass
                os._exit(0)
            except BaseException:
                os._exit(70)
        os.close(stdout_w)
        os.close(stderr_w)
        os.close(diag_w)
        os.close(status_w)
        os.close(cleanup_r)
        for fd in (stdout_r, stderr_r, diag_r, status_r, cleanup_r):
            try:
                os.set_blocking(fd, False)
            except OSError:
                pass
        status_buffer = bytearray()
        diag_buffer = bytearray()
        got_start = False
        got_exit = False
        exec_error = False
        exec_detail = ""
        sentinel_error = False
        capture_error = False
        timed_out = False
        cleanup_sent = False
        tool_status: Optional[int] = None
        start_deadline = time.monotonic() + 5.0
        cleanup_time: Optional[float] = None
        try:
            while True:
                now = time.monotonic()
                if not got_start and now >= start_deadline:
                    sentinel_error = True
                    break
                if got_exit and not cleanup_sent:
                    try:
                        os.write(cleanup_w, b"x")
                    except OSError:
                        sentinel_error = True
                    cleanup_sent = True
                    cleanup_time = now
                wait = 0.05
                if self.deadline is not None and not got_exit and not timed_out:
                    wait = max(0.0, min(wait, self.deadline - now))
                try:
                    ready, _, _ = select.select([stdout_r, stderr_r, diag_r, status_r],
                                                [], [], wait)
                except InterruptedError:
                    continue
                for fd in ready:
                    try:
                        chunk = os.read(fd, 65536)
                    except BlockingIOError:
                        continue
                    except OSError:
                        chunk = b""
                    if fd == status_r:
                        if not chunk:
                            sentinel_error = True
                            continue
                        status_buffer.extend(chunk)
                        while b"\n" in status_buffer:
                            line, _, rest = status_buffer.partition(b"\n")
                            status_buffer = bytearray(rest)
                            try:
                                message = json.loads(line.decode("utf-8"))
                            except (UnicodeDecodeError, ValueError):
                                sentinel_error = True
                                continue
                            if message.get("type") == "start" and not got_start:
                                got_start = True
                                tool_pid = int(message.get("pid", -1))
                                start_token = self.process_start_token(tool_pid)
                            elif message.get("type") == "exit":
                                got_exit = True
                                tool_status = int(message.get("status", 0))
                            elif message.get("type") == "exec-error":
                                exec_error = True
                                exec_detail = str(message.get("detail", ""))
                                got_exit = True
                                tool_status = 127
                    elif fd == diag_r:
                        if not chunk:
                            continue
                        diag_buffer.extend(chunk)
                        if len(diag_buffer) > MAX_DIAG:
                            capture_error = True
                            diag_buffer.clear()
                    if got_exit and cleanup_sent and cleanup_time is not None:
                        if time.monotonic() - cleanup_time >= 0.2:
                            break
                now = time.monotonic()
                if (self.deadline is not None and not got_exit and not timed_out and
                        now >= self.deadline):
                    timed_out = True
                if (timed_out or got_exit) and not cleanup_sent:
                    try:
                        os.write(cleanup_w, b"x")
                    except OSError:
                        sentinel_error = True
                    cleanup_sent = True
                    cleanup_time = now
                if got_exit and cleanup_sent and cleanup_time is not None:
                    if time.monotonic() - cleanup_time >= 0.2:
                        break
                if sentinel_error:
                    break
        finally:
            try:
                os.close(cleanup_w)
            except OSError:
                pass
            for fd in (stdout_r, stderr_r, diag_r, status_r, cleanup_r):
                try:
                    os.close(fd)
                except OSError:
                    pass
            if sentinel_pid > 0:
                reaped = False
                for _ in range(20):
                    try:
                        pid, _ = os.waitpid(sentinel_pid, os.WNOHANG)
                    except ChildProcessError:
                        reaped = True
                        break
                    if pid == sentinel_pid:
                        reaped = True
                        break
                    time.sleep(0.01)
                if not reaped:
                    try:
                        os.kill(sentinel_pid, signal.SIGKILL)
                        os.waitpid(sentinel_pid, 0)
                    except (OSError, ChildProcessError):
                        pass
                    self.cleanup_ok = False
        if sentinel_error and not got_exit and not timed_out:
            if tool_pid > 0 and start_token is not None:
                if self.process_start_token(tool_pid) == start_token:
                    try:
                        os.kill(tool_pid, signal.SIGKILL)
                    except OSError:
                        pass
            return RunResult(None, EXIT_UNAVAILABLE, "sentinel-error",
                             sentinel_error=True)
        if capture_error:
            return RunResult(None, EXIT_INTERNAL, "capture-error", capture_error=True)
        if timed_out:
            return RunResult(None, EXIT_DIAGNOSTIC, "timeout", timed_out=True)
        if exec_error:
            return RunResult(None, EXIT_UNAVAILABLE, "exec-error", exec_error=True,
                             detail=exec_detail)
        if tool_status is None:
            return RunResult(None, EXIT_UNAVAILABLE, "sentinel-error",
                             sentinel_error=True)
        if os.WIFEXITED(tool_status):
            exit_status = os.WEXITSTATUS(tool_status)
            termination = "exit"
        elif os.WIFSIGNALED(tool_status):
            exit_status = 128 + os.WTERMSIG(tool_status)
            termination = "signal"
        else:
            return RunResult(None, EXIT_INTERNAL, "invalid-status",
                             sentinel_error=True)
        fingerprint, protocol_error = self.parse_diagnostics(bytes(diag_buffer))
        return RunResult(fingerprint, exit_status, termination,
                         protocol_error=protocol_error)

    def tool_args_for(self, candidate: str) -> list[str]:
        args = [self.tool_path]
        has_json = any(arg == "--diag-format=json" for arg in self.tool_args)
        if not has_json:
            args.append("--diag-format=json")
        replaced = [self.substitute(arg, candidate) for arg in self.tool_args]
        if not any("{}" in arg for arg in self.tool_args):
            replaced.append(candidate)
        args.extend(replaced)
        return args

    def parse_diagnostics(self, data: bytes) -> tuple[Optional[Fingerprint], bool]:
        if not data:
            return None, False
        errors: list[Fingerprint] = []
        try:
            lines = data.splitlines()
            for raw in lines:
                if not raw:
                    return None, True
                record = json.loads(raw.decode("utf-8"))
                if not isinstance(record, dict):
                    return None, True
                severity = record.get("severity")
                if severity == "note":
                    if not isinstance(record.get("note"), str):
                        return None, True
                    continue
                if severity != "error":
                    return None, True
                code = record.get("code")
                if not isinstance(code, str) or not (code.startswith("TC_CE_") or
                                                       code.startswith("TC_RE_")):
                    return None, True
                required = ("code", "severity", "stage", "print_name", "file",
                            "line", "col")
                if any(key not in record for key in required):
                    return None, True
                if (not isinstance(record["stage"], str) or
                        not isinstance(record["print_name"], str) or
                        not isinstance(record["file"], str) or
                        not isinstance(record["line"], int) or
                        not isinstance(record["col"], int)):
                    return None, True
                errors.append(Fingerprint(code, -1, "diagnostic"))
        except (UnicodeDecodeError, ValueError, TypeError):
            return None, True
        if len(errors) != 1:
            return None, True
        return errors[0], False

    def run_candidate(self, content: bytes) -> tuple[Optional[Fingerprint], Optional[ReduceFailure]]:
        while True:
            path = self.candidate_path(content)
            try:
                self.preflight_child_outputs(path)
            except ReduceFailure as exc:
                if exc.code == EXIT_USAGE and "candidate" in exc.message:
                    continue
                raise
            try:
                fd, ident = self.create_candidate(path, content)
            except FileExistsError:
                continue
            except ReduceFailure:
                raise
            try:
                os.close(fd)
                result = self.execute(path)
            finally:
                self.remove_candidate(path, ident)
            if result.timed_out:
                return None, ReduceFailure(EXIT_DIAGNOSTIC, "timeout")
            if result.protocol_error:
                return None, ReduceFailure(EXIT_INTERNAL, "diagnostic protocol error")
            if result.exec_error or result.sentinel_error or result.capture_error:
                return None, ReduceFailure(EXIT_UNAVAILABLE, "worker failure")
            if result.termination == "signal":
                return None, ReduceFailure(EXIT_INTERNAL, "tool terminated by signal")
            if result.exit_status in (EXIT_UNAVAILABLE, EXIT_INTERNAL):
                return None, ReduceFailure(result.exit_status, "tool failure")
            if result.fingerprint is None:
                return None, None
            result.fingerprint.exit_status = result.exit_status
            result.fingerprint.termination = result.termination
            return result.fingerprint, None

    @staticmethod
    def logical_lines(text: str) -> list[str]:
        physical = text.splitlines(keepends=True)
        if not physical:
            return []
        groups: list[str] = []
        current = ""
        for line in physical:
            current += line
            body = line.rstrip("\r\n")
            if body.endswith("\\"):
                continue
            groups.append(current)
            current = ""
        if current:
            groups.append(current)
        return groups

    @staticmethod
    def line_body(line: str) -> str:
        return line.rstrip("\r\n")

    def candidates(self, text: str):
        lines = self.logical_lines(text)
        for index, line in enumerate(lines):
            body = self.line_body(line).strip()
            if body.startswith("import "):
                match = re.match(r"import\s+([A-Za-z_][A-Za-z0-9_]*)", body)
                if match:
                    name = match.group(1)
                    if len(re.findall(r"\b" + re.escape(name) + r"\b", text)) == 1:
                        yield text[:sum(len(x) for x in lines[:index])] + "".join(lines[index + 1:])
        for index, line in enumerate(lines):
            body = self.line_body(line).strip()
            if not body or body.startswith("import ") or body.startswith("//") or body.startswith("#"):
                continue
            yield "".join(lines[:index] + lines[index + 1:])
        for index, line in enumerate(lines):
            body = self.line_body(line).strip()
            if not body or body.startswith("//") or body.startswith("#"):
                yield "".join(lines[:index] + lines[index + 1:])
        for index, line in enumerate(lines):
            raw = line.encode("utf-8", "surrogateescape")
            matches: list[tuple[int, int, list[bytes]]] = []
            for regex, values in ((INTEGER_RE, (b"0", b"1")),
                                  (FLOAT_RE, (b"0.0",)),
                                  (STRING_RE, (b'""',)),
                                  (CHAR_RE, (b"'\\0'",)),
                                  (re.compile(rb"\b(?:true|false)\b"), (b"false", b"true"))):
                for match in regex.finditer(raw):
                    original = match.group(0)
                    replacements = [value for value in values if len(value) < len(original)]
                    if replacements:
                        matches.append((match.start(), match.end(), replacements))
            for start, end, replacements in sorted(matches, key=lambda item: item[0]):
                for replacement in replacements:
                    changed = raw[:start] + replacement + raw[end:]
                    candidate_lines = list(lines)
                    candidate_lines[index] = changed.decode("utf-8", "surrogateescape")
                    yield "".join(candidate_lines)
        index = 0
        while index < len(lines):
            if re.match(r"\s*(?:public\s+)?func\b", self.line_body(lines[index])):
                indent = len(self.line_body(lines[index])) - len(self.line_body(lines[index]).lstrip())
                end = index + 1
                while end < len(lines):
                    stripped = self.line_body(lines[end]).strip()
                    end_indent = len(self.line_body(lines[end])) - len(self.line_body(lines[end]).lstrip())
                    if stripped == "end" and end_indent == indent:
                        break
                    end += 1
                if end < len(lines):
                    yield "".join(lines[:index] + lines[end + 1:])
                    index = end + 1
                    continue
            index += 1

    def reduce(self) -> None:
        text = self.best.decode("utf-8", "surrogateescape")
        while True:
            if self.deadline is not None and time.monotonic() >= self.deadline:
                raise ReduceFailure(EXIT_DIAGNOSTIC, "timeout")
            improved = False
            seen: set[bytes] = set()
            for candidate_text in self.candidates(text):
                candidate_bytes = candidate_text.encode("utf-8", "surrogateescape")
                if len(candidate_bytes) >= len(self.best) or candidate_bytes in seen:
                    continue
                seen.add(candidate_bytes)
                fingerprint, failure = self.run_candidate(candidate_bytes)
                if failure is not None:
                    raise failure
                if fingerprint is not None and self.same_fingerprint(fingerprint):
                    self.best = candidate_bytes
                    text = candidate_text
                    improved = True
                    break
            if not improved:
                return

    def same_fingerprint(self, candidate: Fingerprint) -> bool:
        expected = self.fingerprint
        return (expected is not None and candidate.code == expected.code and
                candidate.exit_status == expected.exit_status and
                candidate.termination == expected.termination)

    def deliver(self) -> None:
        if self.result_mode == "stdout" or self.result_mode == "fd1":
            self.write_fd(1, self.best)
            return
        assert self.result_path is not None
        directory = os.path.dirname(self.result_path)
        while True:
            temp_path = os.path.join(directory, f".tc-reduce-result-{self.result_serial}.tmp")
            self.result_serial += 1
            flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
            if hasattr(os, "O_NOFOLLOW"):
                flags |= os.O_NOFOLLOW
            try:
                fd = os.open(temp_path, flags, 0o600)
                break
            except FileExistsError:
                continue
            except OSError as exc:
                raise ReduceFailure(EXIT_UNAVAILABLE, "cannot create result temporary") from exc
        temp_ident: Optional[tuple[int, int]] = None
        try:
            os.fchmod(fd, 0o600)
            st = os.fstat(fd)
            temp_ident = (st.st_dev, st.st_ino)
            self.write_fd(fd, self.best)
            os.fsync(fd)
            os.close(fd)
            fd = -1
            current = self.identity(self.result_path)
            if self.result_identity is None:
                if current is not None:
                    raise ReduceFailure(EXIT_UNAVAILABLE, "result target was replaced")
                try:
                    os.link(temp_path, self.result_path)
                    os.unlink(temp_path)
                    temp_ident = None
                except FileExistsError as exc:
                    raise ReduceFailure(EXIT_UNAVAILABLE, "result target was replaced") from exc
                except OSError as exc:
                    raise ReduceFailure(EXIT_UNAVAILABLE, "cannot publish result") from exc
            else:
                if current != self.result_identity:
                    raise ReduceFailure(EXIT_UNAVAILABLE, "result target was replaced")
                os.replace(temp_path, self.result_path)
                temp_ident = None
        finally:
            if fd >= 0:
                try:
                    os.close(fd)
                except OSError:
                    pass
            if temp_ident is not None:
                try:
                    st = os.lstat(temp_path)
                    if (st.st_dev, st.st_ino) == temp_ident:
                        os.unlink(temp_path)
                except OSError:
                    self.cleanup_ok = False

    @staticmethod
    def write_fd(fd: int, data: bytes) -> None:
        view = memoryview(data)
        while view:
            try:
                wrote = os.write(fd, view)
            except BrokenPipeError as exc:
                raise ReduceFailure(EXIT_UNAVAILABLE, "cannot write result") from exc
            except OSError as exc:
                raise ReduceFailure(EXIT_UNAVAILABLE, "cannot write result") from exc
            if wrote <= 0:
                raise ReduceFailure(EXIT_UNAVAILABLE, "cannot write result")
            view = view[wrote:]

    def cleanup(self) -> None:
        if self.exec_fd >= 0:
            try:
                os.close(self.exec_fd)
            except OSError:
                pass
            self.exec_fd = -1
        if self.stdin_snapshot:
            try:
                os.unlink(self.stdin_snapshot)
            except OSError:
                self.cleanup_ok = False
            self.stdin_snapshot = ""

    def run(self) -> int:
        data = self.read_file(self.source)
        self.best = data
        self.parse_manifest()
        self.parse_cli_includes()
        self.build_input_closure(data)
        self.setup_stdin()
        self.resolve_result_target()
        self.preflight_stdio()
        self.validate_tool_args()
        self.resolve_tool()
        self.stderr_safe = True
        if self.timeout is not None and self.timeout > 0:
            self.deadline = time.monotonic() + self.timeout
        seed = self.execute(self.source)
        if seed.timed_out:
            self.deliver()
            return EXIT_DIAGNOSTIC
        if seed.protocol_error:
            self.fail(EXIT_INTERNAL, "diagnostic protocol error")
        if seed.exec_error or seed.sentinel_error or seed.capture_error:
            self.fail(EXIT_UNAVAILABLE, "worker failure" +
                      (f": {seed.detail}" if seed.detail else ""))
        if seed.termination == "signal":
            self.fail(EXIT_INTERNAL, "tool terminated by signal")
        if seed.exit_status in (EXIT_UNAVAILABLE, EXIT_INTERNAL):
            self.fail(seed.exit_status, "tool failure")
        if seed.fingerprint is None:
            self.fail(EXIT_USAGE, "seed has no diagnostic fingerprint")
        seed.fingerprint.exit_status = seed.exit_status
        seed.fingerprint.termination = seed.termination
        self.fingerprint = seed.fingerprint
        try:
            self.reduce()
        except ReduceFailure as exc:
            if exc.code == EXIT_DIAGNOSTIC and exc.message == "timeout":
                self.deliver()
                return EXIT_DIAGNOSTIC
            if self.fingerprint is not None:
                self.deliver()
                return exc.code
            raise
        self.deliver()
        if not self.cleanup_ok:
            return EXIT_UNAVAILABLE
        return EXIT_OK


def parse_timeout(value: str) -> int:
    if not value or not value.isascii() or not value.isdigit():
        raise ReduceFailure(EXIT_USAGE, "invalid timeout")
    parsed = int(value, 10)
    if parsed > 0xFFFFFFFFFFFFFFFF:
        raise ReduceFailure(EXIT_USAGE, "timeout overflow")
    return parsed


def parse_args(argv: list[str]) -> tuple[str, Optional[int], Optional[str], Optional[str], str, list[str]]:
    if len(argv) < 4:
        raise ReduceFailure(EXIT_USAGE, "missing arguments")
    source = argv[1]
    timeout: Optional[int] = None
    stdin_path: Optional[str] = None
    output_path: Optional[str] = None
    index = 2
    while index < len(argv) and argv[index] != "--":
        arg = argv[index]
        if arg.startswith("--timeout="):
            if timeout is not None:
                raise ReduceFailure(EXIT_USAGE, "duplicate timeout")
            timeout = parse_timeout(arg[len("--timeout="):])
        elif arg.startswith("--stdin="):
            if stdin_path is not None:
                raise ReduceFailure(EXIT_USAGE, "duplicate stdin")
            stdin_path = arg[len("--stdin="):]
            if not stdin_path:
                raise ReduceFailure(EXIT_USAGE, "empty stdin")
        elif arg.startswith("--output="):
            if output_path is not None:
                raise ReduceFailure(EXIT_USAGE, "duplicate output")
            output_path = arg[len("--output="):]
            if not output_path:
                raise ReduceFailure(EXIT_USAGE, "empty output")
        else:
            raise ReduceFailure(EXIT_USAGE, "invalid reducer option")
        index += 1
    if index >= len(argv) or argv[index] != "--" or index + 1 >= len(argv):
        raise ReduceFailure(EXIT_USAGE, "missing tool")
    tool = argv[index + 1]
    tool_args = argv[index + 2:]
    if any("{}" in arg for arg in argv[2:index]):
        raise ReduceFailure(EXIT_USAGE, "placeholder before separator")
    return source, timeout, stdin_path, output_path, tool, tool_args


def main(argv: list[str]) -> int:
    reducer: Optional[Reducer] = None
    try:
        source, timeout, stdin_path, output_path, tool, tool_args = parse_args(argv)
        reducer = Reducer(source, timeout, stdin_path, output_path, tool, tool_args)
        result = reducer.run()
        reducer.cleanup()
        if result == EXIT_OK and not reducer.cleanup_ok:
            return EXIT_UNAVAILABLE
        return result
    except ReduceFailure as exc:
        if reducer is not None:
            reducer.note(exc.message)
        return exc.code
    except MemoryError:
        return EXIT_UNAVAILABLE
    except OSError:
        return EXIT_UNAVAILABLE
    finally:
        if reducer is not None:
            reducer.cleanup()


if __name__ == "__main__":
    sys.exit(main(sys.argv))
