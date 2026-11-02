import re

KEY = re.compile(r"([a-z][a-z0-9_-]*):(?: (.*))?")
INDICATORS = set("-?:,[]{}#&*!|>'\"%@`")
DECIMAL = re.compile(r"0|[1-9][0-9]*")
BOOLEANS = {"true": True, "false": False}
RESERVED = [
    re.compile(r"yes|Yes|YES|no|No|NO|True|TRUE|False|FALSE|on|On|ON|off|Off|OFF"),
    re.compile(r"~|null|Null|NULL"),
    re.compile(r"""[-+]?(?:[0-9][0-9_]*)\.[0-9_]*(?:[eE][-+][0-9]+)?
                |\.[0-9][0-9_]*(?:[eE][-+][0-9]+)?
                |[-+]?[0-9][0-9_]*(?::[0-5]?[0-9])+\.[0-9_]*
                |[-+]?\.(?:inf|Inf|INF)
                |\.(?:nan|NaN|NAN)""", re.X),
    re.compile(r"""[-+]?0b[0-1_]+
                |[-+]?0[0-7_]+
                |[-+]?(?:0|[1-9][0-9_]*)
                |[-+]?0x[0-9a-fA-F_]+
                |[-+]?[1-9][0-9_]*(?::[0-5]?[0-9])+""", re.X),
    re.compile(r"""[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]
                |[0-9][0-9][0-9][0-9]-[0-9][0-9]?-[0-9][0-9]?
                (?:[Tt]|[ \t]+)[0-9][0-9]?
                :[0-9][0-9]:[0-9][0-9](?:\.[0-9]*)?
                (?:[ \t]*(?:Z|[-+][0-9][0-9]?(?::[0-9][0-9])?))?""", re.X),
    re.compile(r"<<|="),
]


class YamlSubsetError(ValueError):
    def __init__(self, line, message):
        super().__init__(f"line {line}: {message}")
        self.line = line


class _Line:
    def __init__(self, number, indent, text):
        self.number = number
        self.indent = indent
        self.text = text


def _unprintable(character):
    code = ord(character)
    return code < 32 or 0x7F <= code <= 0x9F or character in "\u2028\u2029\ufeff"


def _reserved(text):
    return text in BOOLEANS or any(pattern.fullmatch(text) for pattern in RESERVED)


def _lines(source):
    if source.startswith("﻿"):
        source = source[1:]
    result = []
    for number, raw in enumerate(source.split("\n"), start=1):
        if "\t" in raw:
            raise YamlSubsetError(number, "tabs are not allowed")
        if raw != raw.rstrip(" \r"):
            raise YamlSubsetError(number, "trailing whitespace is not allowed")
        if any(_unprintable(character) for character in raw):
            raise YamlSubsetError(number, "control and line-separator characters are not allowed")
        text = raw.lstrip(" ")
        if not text:
            continue
        if text.startswith("#"):
            raise YamlSubsetError(number, "comments are not allowed")
        if text in ("---", "...") or text.startswith(("--- ", "%")):
            raise YamlSubsetError(number, "document markers and directives are not allowed")
        result.append(_Line(number, len(raw) - len(text), text))
    return result


def _plain(text, number, flow):
    if not text:
        raise YamlSubsetError(number, "empty value; quote an empty string")
    if text[0] in INDICATORS or text[0] == " ":
        raise YamlSubsetError(number, f"plain scalar {text!r} starts with an indicator; quote it")
    if ": " in text or " #" in text or text.endswith(":"):
        raise YamlSubsetError(number, f"plain scalar {text!r} is ambiguous; quote it")
    if flow and any(character in text for character in ",[]{}"):
        raise YamlSubsetError(number, f"flow scalar {text!r} contains a flow indicator; quote it")
    if text in BOOLEANS:
        return BOOLEANS[text]
    if DECIMAL.fullmatch(text):
        return int(text)
    if _reserved(text):
        raise YamlSubsetError(number, f"plain scalar {text!r} has an implicit YAML type; quote it")
    return text


def _quoted(text, start, number):
    result = []
    index = start + 1
    while index < len(text):
        character = text[index]
        if character == '"':
            return "".join(result), index + 1
        if character == "\\":
            escaped = text[index + 1:index + 2]
            if escaped not in ('"', "\\"):
                raise YamlSubsetError(number, "only \\\" and \\\\ escapes are allowed")
            result.append(escaped)
            index += 2
            continue
        result.append(character)
        index += 1
    raise YamlSubsetError(number, "unterminated double-quoted string")


def _flow_sequence(text, number):
    if not text.endswith("]"):
        raise YamlSubsetError(number, "a flow sequence must end on the same line")
    body = text[1:-1]
    if not body.strip():
        if body:
            raise YamlSubsetError(number, "write an empty sequence as []")
        return []
    items = []
    index = 0
    while True:
        while index < len(body) and body[index] == " ":
            index += 1
        if index < len(body) and body[index] == '"':
            value, index = _quoted(body, index, number)
            while index < len(body) and body[index] == " ":
                index += 1
        else:
            end = body.find(",", index)
            end = len(body) if end < 0 else end
            value = _plain(body[index:end].strip(" "), number, flow=True)
            index = end
        items.append(value)
        if index == len(body):
            return items
        if body[index] != ",":
            raise YamlSubsetError(number, "expected ',' between flow sequence items")
        index += 1


def _inline(text, number):
    if text.startswith("["):
        return _flow_sequence(text, number)
    if text.startswith('"'):
        value, end = _quoted(text, 0, number)
        if end != len(text):
            raise YamlSubsetError(number, "unexpected text after a quoted string")
        return value
    if text[0] in "{'|>&*!":
        raise YamlSubsetError(number, f"unsupported YAML construct {text[0]!r}")
    return _plain(text, number, flow=False)


class _Parser:
    def __init__(self, lines):
        self.lines = lines
        self.position = 0

    def peek(self):
        return self.lines[self.position] if self.position < len(self.lines) else None

    def block(self, indent):
        line = self.peek()
        if line.text == "-" or line.text.startswith("- "):
            return self.sequence(indent)
        return self.mapping(indent)

    def nested(self, parent, number):
        line = self.peek()
        if line is None or line.indent <= parent:
            raise YamlSubsetError(number, "expected an indented block")
        return self.block(line.indent)

    def mapping(self, indent):
        result = {}
        while True:
            line = self.peek()
            if line is None or line.indent < indent:
                return result
            if line.indent > indent:
                raise YamlSubsetError(line.number, "unexpected indentation")
            if line.text == "-" or line.text.startswith("- "):
                raise YamlSubsetError(line.number, "sequence item where a key was expected")
            match = KEY.fullmatch(line.text)
            if not match:
                raise YamlSubsetError(line.number,
                                      "expected 'key: value' with a lowercase plain key")
            key, value = match.group(1), match.group(2)
            if _reserved(key):
                raise YamlSubsetError(line.number, f"key {key!r} has an implicit YAML type")
            if key in result:
                raise YamlSubsetError(line.number, f"duplicate key {key!r}")
            self.position += 1
            if value is None:
                result[key] = self.nested(indent, line.number)
            elif not value.strip(" "):
                raise YamlSubsetError(line.number, "empty value; quote an empty string")
            else:
                result[key] = _inline(value, line.number)

    def sequence(self, indent):
        result = []
        while True:
            line = self.peek()
            if line is None or line.indent < indent:
                return result
            if line.indent > indent:
                raise YamlSubsetError(line.number, "unexpected indentation")
            if not (line.text == "-" or line.text.startswith("- ")):
                raise YamlSubsetError(line.number, "expected a sequence item")
            if line.text == "-":
                self.position += 1
                result.append(self.nested(indent, line.number))
                continue
            rest = line.text[2:].lstrip(" ")
            inner = indent + len(line.text) - len(rest)
            if KEY.fullmatch(rest):
                self.lines[self.position] = _Line(line.number, inner, rest)
                result.append(self.mapping(inner))
            else:
                self.position += 1
                result.append(_inline(rest, line.number))


def load(source):
    lines = _lines(source)
    if not lines:
        raise YamlSubsetError(1, "the document is empty")
    if lines[0].indent != 0:
        raise YamlSubsetError(lines[0].number, "the document must start at column 1")
    parser = _Parser(lines)
    document = parser.block(0)
    if parser.peek() is not None:
        raise YamlSubsetError(parser.peek().number, "unexpected content")
    return document
