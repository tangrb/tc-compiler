"""Dependency-free parser for the YAML subset used by TC case metadata.

Supported: indentation-based mappings and sequences, inline lists/maps,
single/double quoted scalars, unquoted scalars, null, booleans, decimal and
0x integers, comments, and empty containers.  Block scalars, anchors, tags,
multiple documents and other YAML features are rejected explicitly.
"""

import re

_INT_RE = re.compile(r"^[+-]?[0-9]+$")
_HEX_INT_RE = re.compile(r"^[+-]?0[xX][0-9a-fA-F]+$")
_FLOAT_RE = re.compile(r"^[+-]?(?:[0-9]+\.[0-9]*|\.[0-9]+)(?:[eE][+-]?[0-9]+)?$")


class YamlError(ValueError):
    def __init__(self, message, line=None):
        self.message = message
        self.line = line
        super().__init__(f"line {line}: {message}" if line else message)


def parse(text):
    """Parse one YAML-subset document into Python data."""
    if not isinstance(text, str):
        raise YamlError("input must be text")
    return _Parser(text).parse()


def _strip_comment(line, lineno):
    quote = None
    escaped = False
    i = 0
    while i < len(line):
        ch = line[i]
        if quote == '"':
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == '"':
                quote = None
        elif quote == "'":
            if ch == "'":
                if i + 1 < len(line) and line[i + 1] == "'":
                    i += 1
                else:
                    quote = None
        elif ch in "\"'":
            quote = ch
        elif ch == "#" and (i == 0 or line[i - 1].isspace()):
            return line[:i].rstrip()
        i += 1
    return line.rstrip()


class _Parser:
    def __init__(self, text):
        self.lines = []
        self.raw = text.splitlines()
        for lineno, raw in enumerate(self.raw, 1):
            if "\t" in raw[: len(raw) - len(raw.lstrip(" \t"))]:
                raise YamlError("tab in indentation", lineno)
            body = raw.rstrip("\r")
            stripped = body.lstrip(" ")
            if not stripped or stripped.startswith("#"):
                continue
            if stripped in ("---", "..."):
                if stripped == "..." and not self.lines:
                    raise YamlError("document end without content", lineno)
                continue
            indent = len(body) - len(stripped)
            content = _strip_comment(stripped, lineno)
            if not content:
                continue
            self.lines.append((lineno, indent, content))
        self.index = 0

    def parse(self):
        if not self.lines:
            raise YamlError("empty document")
        node, index = self._parse_node(0, self.lines[0][1])
        if index != len(self.lines):
            lineno = self.lines[index][0]
            raise YamlError("unexpected content or indentation", lineno)
        return node

    def _parse_node(self, index, indent):
        lineno, actual, content = self.lines[index]
        if actual != indent:
            raise YamlError("inconsistent indentation", lineno)
        if _is_sequence_item(content):
            return self._parse_sequence(index, indent)
        return self._parse_mapping(index, indent)

    def _parse_mapping(self, index, indent, first=None):
        result = {}
        if first is not None:
            index = self._parse_entry(result, first[0], first[1], index, indent)
        while index < len(self.lines):
            lineno, actual, content = self.lines[index]
            if actual < indent:
                break
            if actual > indent:
                raise YamlError("unexpected indentation in mapping", lineno)
            if _is_sequence_item(content):
                break
            index = self._parse_entry(result, content, index + 1, indent)
        return result, index

    def _parse_entry(self, result, content, next_index, indent):
        key, rest = _split_entry(content)
        if key == "":
            raise YamlError("empty mapping key", self.lines[next_index - 1][0])
        if key in result:
            raise YamlError(f"duplicate key {key!r}", self.lines[next_index - 1][0])
        if rest == "":
            if next_index < len(self.lines) and self.lines[next_index][1] > indent:
                value, next_index = self._parse_node(
                    next_index, self.lines[next_index][1]
                )
            else:
                value = None
        else:
            if rest.startswith(("|", ">")):
                raise YamlError(
                    "block scalars are not supported",
                    self.lines[next_index - 1][0],
                )
            value = _parse_flow(rest, self.lines[next_index - 1][0])
            if next_index < len(self.lines) and self.lines[next_index][1] > indent:
                raise YamlError(
                    "unexpected indentation after scalar",
                    self.lines[next_index][0],
                )
        result[key] = value
        return next_index

    def _parse_sequence(self, index, indent):
        result = []
        while index < len(self.lines):
            lineno, actual, content = self.lines[index]
            if actual < indent:
                break
            if actual > indent or not _is_sequence_item(content):
                break
            rest = content[1:].lstrip(" ")
            index += 1
            if rest == "":
                if index < len(self.lines) and self.lines[index][1] > indent:
                    item, index = self._parse_node(index, self.lines[index][1])
                else:
                    item = None
            elif _entry_colon(rest) >= 0:
                base = indent + 2
                item = {}
                index = self._parse_entry(item, rest, index, base)
                while (
                    index < len(self.lines)
                    and self.lines[index][1] == base
                    and not _is_sequence_item(self.lines[index][2])
                ):
                    entry_content = self.lines[index][2]
                    index = self._parse_entry(
                        item, entry_content, index + 1, base
                    )
                if index < len(self.lines) and self.lines[index][1] > indent:
                    raise YamlError(
                        "unexpected indentation in sequence item",
                        self.lines[index][0],
                    )
            else:
                item = _parse_flow(rest, lineno)
                if index < len(self.lines) and self.lines[index][1] > indent:
                    raise YamlError(
                        "unexpected indentation after scalar", self.lines[index][0]
                    )
            result.append(item)
        return result, index


def _is_sequence_item(content):
    return content == "-" or content.startswith("- ")


def _entry_colon(content):
    """Return index of a top-level mapping colon, or -1."""
    quote = None
    depth = 0
    escaped = False
    i = 0
    while i < len(content):
        ch = content[i]
        if quote == '"':
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == '"':
                quote = None
        elif quote == "'":
            if ch == "'":
                if i + 1 < len(content) and content[i + 1] == "'":
                    i += 1
                else:
                    quote = None
        elif ch in "\"'":
            quote = ch
        elif ch in "[{":
            depth += 1
        elif ch in "]}":
            depth -= 1
        elif ch == ":" and depth == 0:
            if i + 1 == len(content) or content[i + 1].isspace():
                return i
        i += 1
    return -1


def _split_entry(content):
    pos = _entry_colon(content)
    if pos < 0:
        raise YamlError(f"expected 'key: value', got {content!r}")
    key = content[:pos].strip()
    if key and key[0] in "\"'":
        key = _parse_quoted(key)
    if not isinstance(key, str) or not key:
        raise YamlError(f"invalid mapping key {content[:pos]!r}")
    return key, content[pos + 1 :].strip()


def _split_flow(content, lineno):
    parts = []
    start = 0
    quote = None
    depth = 0
    escaped = False
    i = 0
    while i < len(content):
        ch = content[i]
        if quote == '"':
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == '"':
                quote = None
        elif quote == "'":
            if ch == "'":
                if i + 1 < len(content) and content[i + 1] == "'":
                    i += 1
                else:
                    quote = None
        elif ch in "\"'":
            quote = ch
        elif ch in "[{":
            depth += 1
        elif ch in "]}":
            depth -= 1
            if depth < 0:
                raise YamlError("unbalanced flow collection", lineno)
        elif ch == "," and depth == 0:
            parts.append(content[start:i].strip())
            start = i + 1
        i += 1
    if quote is not None or depth != 0:
        raise YamlError("unterminated flow collection or quote", lineno)
    parts.append(content[start:].strip())
    if parts and parts[-1] == "" and len(parts) > 1:
        parts.pop()
    if any(part == "" for part in parts):
        raise YamlError("empty item in flow collection", lineno)
    return parts


def _parse_flow(raw, lineno):
    raw = raw.strip()
    if raw.startswith("["):
        if not raw.endswith("]"):
            raise YamlError("unterminated inline list", lineno)
        inner = raw[1:-1].strip()
        if not inner:
            return []
        return [_parse_flow(part, lineno) for part in _split_flow(inner, lineno)]
    if raw.startswith("{"):
        if not raw.endswith("}"):
            raise YamlError("unterminated inline map", lineno)
        inner = raw[1:-1].strip()
        if not inner:
            return {}
        result = {}
        for part in _split_flow(inner, lineno):
            pos = _entry_colon(part)
            if pos < 0:
                raise YamlError(f"invalid inline map entry {part!r}", lineno)
            key = part[:pos].strip()
            if key and key[0] in "\"'":
                key = _parse_quoted(key)
            if not isinstance(key, str) or not key:
                raise YamlError(f"invalid inline map key {part[:pos]!r}", lineno)
            if key in result:
                raise YamlError(f"duplicate key {key!r}", lineno)
            result[key] = _parse_flow(part[pos + 1 :].strip(), lineno)
        return result
    return _parse_scalar(raw, lineno)


def _parse_scalar(raw, lineno=None):
    raw = raw.strip()
    if raw == "":
        return None
    if raw[0] in "&*!":
        raise YamlError("anchors, aliases and tags are not supported", lineno)
    if raw.startswith(("|", ">")):
        raise YamlError("block scalars are not supported", lineno)
    if raw[0] in "\"'":
        return _parse_quoted(raw)
    if raw in ("null", "Null", "NULL", "~"):
        return None
    if raw in ("true", "True", "TRUE"):
        return True
    if raw in ("false", "False", "FALSE"):
        return False
    if _INT_RE.match(raw):
        return int(raw, 10)
    if _HEX_INT_RE.match(raw):
        return int(raw, 16)
    if _FLOAT_RE.match(raw):
        return float(raw)
    return raw


def _parse_quoted(raw):
    if len(raw) < 2 or raw[0] not in "\"'" or raw[-1] != raw[0]:
        raise YamlError(f"unterminated quoted scalar {raw!r}")
    quote = raw[0]
    body = raw[1:-1]
    if quote == "'":
        return body.replace("''", "'")
    out = []
    i = 0
    escapes = {
        '"': '"', "\\": "\\", "/": "/", "n": "\n", "r": "\r",
        "t": "\t", "0": "\0", "b": "\b", "f": "\f",
    }
    while i < len(body):
        ch = body[i]
        if ch != "\\":
            out.append(ch)
            i += 1
            continue
        i += 1
        if i >= len(body):
            raise YamlError("trailing backslash in quoted scalar")
        esc = body[i]
        if esc in escapes:
            out.append(escapes[esc])
            i += 1
        elif esc in ("u", "U"):
            width = 4 if esc == "u" else 8
            digits = body[i + 1 : i + 1 + width]
            if len(digits) != width or not re.match(r"^[0-9a-fA-F]+$", digits):
                raise YamlError(f"invalid \\{esc} escape in quoted scalar")
            out.append(chr(int(digits, 16)))
            i += 1 + width
        else:
            raise YamlError(f"unsupported escape \\{esc} in quoted scalar")
    return "".join(out)
