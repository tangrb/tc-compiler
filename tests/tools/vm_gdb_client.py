#!/usr/bin/env python3
"""Minimal GDB remote serial protocol client used by VM protocol tests."""

from __future__ import annotations

import socket


class GdbClientError(RuntimeError):
    pass


class GdbProtocolError(GdbClientError):
    pass


def _encode_frame(payload: bytes) -> bytes:
    escaped = bytearray()
    for value in payload:
        if value in b"#$}*":
            escaped.append(ord("}"))
            escaped.append(value ^ 0x20)
        else:
            escaped.append(value)
    checksum = sum(escaped) & 0xFF
    return b"$" + bytes(escaped) + b"#" + f"{checksum:02x}".encode("ascii")


def _decode_payload(raw: bytes) -> bytes:
    decoded = bytearray()
    index = 0
    while index < len(raw):
        value = raw[index]
        if value == ord("}"):
            index += 1
            if index >= len(raw):
                raise GdbProtocolError("truncated RSP escape")
            decoded.append(raw[index] ^ 0x20)
        elif value == ord("*"):
            index += 1
            if index >= len(raw) or not decoded:
                raise GdbProtocolError("invalid RSP run-length encoding")
            count = raw[index] - 29
            if count <= 0:
                raise GdbProtocolError("invalid RSP run-length count")
            decoded.extend(bytes((decoded[-1],)) * (count - 1))
        else:
            if value in b"#$":
                raise GdbProtocolError("unescaped reserved byte in RSP payload")
            decoded.append(value)
        index += 1
    return bytes(decoded)


class GdbClient:
    def __init__(self, port: int, host: str = "127.0.0.1", timeout: float = 5.0):
        if timeout <= 0:
            raise ValueError("timeout must be positive")
        self.host = host
        self.port = port
        self.timeout = timeout
        self._socket: socket.socket | None = None
        self._buffer = bytearray()
        self._ack = True

    def _endpoint(self) -> str:
        return f"{self.host}:{self.port}"

    def _connected_socket(self) -> socket.socket:
        if self._socket is None:
            raise GdbClientError(f"not connected to {self._endpoint()}")
        return self._socket

    def connect(self) -> None:
        if self._socket is not None:
            return
        try:
            connection = socket.create_connection(
                (self.host, self.port), timeout=self.timeout)
            connection.settimeout(self.timeout)
        except OSError as error:
            raise GdbClientError(
                f"could not connect to {self._endpoint()}: {error}") from error
        self._socket = connection
        self._buffer.clear()
        self._ack = True

    def close(self) -> None:
        connection = self._socket
        self._socket = None
        self._buffer.clear()
        if connection is not None:
            connection.close()

    def _send_all(self, data: bytes) -> None:
        connection = self._connected_socket()
        try:
            connection.sendall(data)
        except OSError as error:
            raise GdbClientError(
                f"send to {self._endpoint()} failed: {error}") from error

    def _read_byte(self) -> int:
        connection = self._connected_socket()
        if self._buffer:
            value = self._buffer[0]
            del self._buffer[0]
            return value
        try:
            data = connection.recv(1)
        except OSError as error:
            raise GdbClientError(
                f"receive from {self._endpoint()} failed: {error}") from error
        if not data:
            raise GdbClientError(
                f"connection to {self._endpoint()} closed unexpectedly")
        return data[0]

    def _send_ack(self, value: bytes) -> None:
        if self._ack:
            self._send_all(value)

    def _wait_for_ack(self) -> bool:
        while True:
            value = self._read_byte()
            if value == ord("+"):
                return True
            if value == ord("-"):
                return False
            if value == ord("$"):
                raise GdbProtocolError(
                    f"reply from {self._endpoint()} arrived before packet ack")
            raise GdbProtocolError(
                f"invalid ack byte 0x{value:02x} from {self._endpoint()}")

    def _read_wire_packet(self) -> tuple[bytes, bytes]:
        while True:
            value = self._read_byte()
            if value == ord("$"):
                break
        payload = bytearray()
        while True:
            value = self._read_byte()
            if value == ord("#"):
                break
            payload.append(value)
        checksum = bytes((self._read_byte(), self._read_byte()))
        return bytes(payload), checksum

    def recv_packet(self) -> bytes:
        last_error = "no packet received"
        for _ in range(3):
            raw, checksum_text = self._read_wire_packet()
            try:
                if len(checksum_text) != 2 or any(
                        value not in b"0123456789abcdefABCDEF"
                        for value in checksum_text):
                    raise ValueError("checksum must contain two hex digits")
                expected = int(checksum_text, 16)
            except ValueError as error:
                last_error = f"invalid RSP checksum field {checksum_text!r}: {error}"
                self._send_ack(b"-")
                continue
            actual = sum(raw) & 0xFF
            if actual != expected:
                last_error = (
                    f"RSP checksum mismatch: computed 0x{actual:02x}, "
                    f"received 0x{expected:02x}")
                self._send_ack(b"-")
                continue
            try:
                payload = _decode_payload(raw)
            except GdbProtocolError as error:
                last_error = str(error)
                self._send_ack(b"-")
                continue
            self._send_ack(b"+")
            return payload
        raise GdbProtocolError(
            f"discarded three invalid packets from {self._endpoint()}: {last_error}")

    def send_packet(self, payload: bytes, expect_reply: bool = True) -> bytes | None:
        if not isinstance(payload, (bytes, bytearray, memoryview)):
            raise TypeError("payload must be bytes-like")
        payload_bytes = bytes(payload)
        frame = _encode_frame(payload_bytes)
        for _ in range(3):
            self._send_all(frame)
            if not self._ack or self._wait_for_ack():
                if expect_reply:
                    return self.recv_packet()
                return None
        raise GdbClientError(
            f"packet to {self._endpoint()} was not acknowledged after three attempts")

    def send_packet_nowait(self, payload: bytes) -> None:
        self._send_all(_encode_frame(payload))
        if self._ack:
            self._wait_for_ack()

    def send_interrupt(self) -> None:
        self._send_all(b"\x03")

    def enable_no_ack(self) -> None:
        if not self._ack:
            return
        reply = self.send_packet(b"QStartNoAckMode")
        if reply != b"OK":
            raise GdbProtocolError(
                f"QStartNoAckMode was rejected by {self._endpoint()}: {reply!r}")
        self._ack = False

    def q_xfer(self, annex: str, offset: int = 0, length: int = 1024) -> bytes:
        if offset < 0 or length < 0:
            raise ValueError("qXfer offset and length must be non-negative")
        if isinstance(annex, bytes):
            annex_text = annex.decode("ascii")
        elif isinstance(annex, str):
            annex_text = annex
        else:
            raise TypeError("annex must be str or ASCII bytes")
        if annex_text.startswith("qXfer:"):
            prefix = annex_text
        elif annex_text.startswith("features:"):
            prefix = "qXfer:" + annex_text
        elif annex_text == "memory-map":
            prefix = "qXfer:memory-map:read"
        elif annex_text == "memory-map:read":
            prefix = "qXfer:memory-map:read"
        else:
            prefix = "qXfer:features:read:" + annex_text

        result = bytearray()
        while True:
            request = f"{prefix}:{offset:x}:{length:x}".encode("ascii")
            reply = self.send_packet(request)
            if reply is None:
                raise GdbProtocolError("qXfer request returned no packet")
            if not reply or reply[:1] not in (b"m", b"l"):
                raise GdbProtocolError(
                    f"invalid qXfer response from {self._endpoint()}: {reply!r}")
            marker = reply[:1]
            chunk = reply[1:]
            if marker == b"m" and not chunk:
                raise GdbProtocolError(
                    f"empty continuing qXfer chunk from {self._endpoint()}")
            result.extend(chunk)
            if marker == b"l":
                return bytes(result)
            offset += len(chunk)

    def monitor(self, command: str) -> str:
        encoded_command = command.encode("utf-8").hex().encode("ascii")
        reply = self.send_packet(b"qRcmd," + encoded_command)
        output = bytearray()
        while True:
            if reply == b"OK":
                try:
                    return output.decode("utf-8")
                except UnicodeDecodeError as error:
                    raise GdbProtocolError(
                        f"monitor output from {self._endpoint()} is not UTF-8") from error
            if reply == b"E01":
                partial = output.decode("utf-8", errors="replace")
                raise GdbClientError(
                    f"monitor command {command!r} failed with E01; output={partial!r}")
            if reply is not None and reply.startswith(b"O"):
                chunk_text = reply[1:]
                try:
                    if len(chunk_text) % 2 or any(
                            value not in b"0123456789abcdefABCDEF"
                            for value in chunk_text):
                        raise ValueError("O chunk is not strict hexadecimal")
                    output.extend(bytes.fromhex(chunk_text.decode("ascii")))
                except (UnicodeDecodeError, ValueError) as error:
                    raise GdbProtocolError(
                        f"invalid monitor O chunk from {self._endpoint()}: "
                        f"{chunk_text!r}") from error
                reply = self.recv_packet()
                continue
            raise GdbProtocolError(
                f"invalid monitor response from {self._endpoint()}: {reply!r}")


def _selftest() -> None:
    payload = b"A#}$*\x00\xff"
    if _decode_payload(_encode_frame(payload)[1:-3]) != payload:
        raise AssertionError("RSP frame round-trip failed")
    if _decode_payload(b"A*\x5c") != b"A" * 63:
        raise AssertionError("RSP run-length decoding failed")


if __name__ == "__main__":
    _selftest()
    print("vm_gdb_client selftest passed")
