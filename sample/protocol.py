"""Safe parser for the sampler executable's line-oriented response protocol."""

from __future__ import unicode_literals

import keyword
import re
import unicodedata


_FIELD_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
_INTEGER_RE = re.compile(r"^[+-]?[0-9]+$")
_FLOAT_RE = re.compile(
    r"^[+-]?(?:(?:[0-9]+\.[0-9]*|\.[0-9]+)(?:[eE][+-]?[0-9]+)?|[0-9]+[eE][+-]?[0-9]+)$"
)
_NAN_TOKENS = frozenset(("nan", "+nan", "-nan", "1.#ind", "-1.#ind", "1.#qnan", "-1.#qnan",
                         "1.#snan", "-1.#snan"))
_POSITIVE_INFINITY_TOKENS = frozenset(("inf", "+inf", "infinity", "+infinity", "1.#inf", "+1.#inf"))
_NEGATIVE_INFINITY_TOKENS = frozenset(("-inf", "-infinity", "-1.#inf"))


def summarize_response(response, limit=512):
    """Return a bounded, single-line representation of *response*."""
    if limit <= 0:
        return ""
    escaped = []
    for character in response:
        code = ord(character)
        if character == "\\":
            escaped.append("\\\\")
        elif character == "\r":
            escaped.append("\\r")
        elif character == "\n":
            escaped.append("\\n")
        elif character == "\t":
            escaped.append("\\t")
        elif unicodedata.category(character) == "Cc":
            escaped.append("\\x{0:02x}".format(code))
        else:
            escaped.append(character)
    result = "".join(escaped)
    if len(result) <= limit:
        return result
    marker = "..."
    if limit <= len(marker):
        return marker[:limit]
    available = limit - len(marker)
    head_length = (available + 1) // 2
    return result[:head_length] + marker + result[-(available - head_length):]


class ProtocolError(ValueError):
    """An invalid sampler response with bounded diagnostic context."""

    def __init__(self, message, response, line_number=None, field_name=None):
        self.line_number = line_number
        self.field_name = field_name
        self.response_length = len(response)
        self.response_summary = summarize_response(response)
        details = "line={0}, field={1!r}, response_length={2}, response={3!r}".format(
            line_number, field_name, self.response_length, self.response_summary
        )
        super(ProtocolError, self).__init__("{0} ({1})".format(message, details))


class _ValueParser(object):

    def __init__(self, text):
        self.text = text
        self.position = 0

    def parse(self):
        self._skip_whitespace()
        value = self._parse_value(allow_list=True)
        self._skip_whitespace()
        if self.position != len(self.text):
            raise ValueError("trailing content")
        return value

    def _parse_value(self, allow_list):
        if self.position >= len(self.text):
            raise ValueError("missing value")
        character = self.text[self.position]
        if character == '"':
            return self._parse_string()
        if character == "[":
            if not allow_list:
                raise ValueError("nested containers are not allowed")
            return self._parse_list()
        return self._parse_atom()

    def _parse_string(self):
        self.position += 1
        result = []
        escapes = {'"': '"', "\\": "\\", "r": "\r", "n": "\n", "t": "\t"}
        while self.position < len(self.text):
            character = self.text[self.position]
            self.position += 1
            if character == '"':
                return "".join(result)
            if character == "\\":
                if self.position >= len(self.text) or self.text[self.position] not in escapes:
                    raise ValueError("unknown or incomplete string escape")
                result.append(escapes[self.text[self.position]])
                self.position += 1
            elif ord(character) < 32:
                raise ValueError("unescaped control character in string")
            else:
                result.append(character)
        raise ValueError("unterminated string")

    def _parse_list(self):
        self.position += 1
        values = []
        self._skip_whitespace()
        if self._consume("]"):
            return values
        while True:
            values.append(self._parse_value(allow_list=False))
            self._skip_whitespace()
            if self._consume("]"):
                return values
            if not self._consume(","):
                raise ValueError("expected comma or closing bracket")
            self._skip_whitespace()
            if self._consume("]"):
                return values

    def _parse_atom(self):
        start = self.position
        while self.position < len(self.text) and self.text[self.position] not in " \t,]":
            self.position += 1
        token = self.text[start:self.position]
        lowered = token.lower()
        if token == "True":
            return True
        if token == "False":
            return False
        if lowered in _NAN_TOKENS:
            return float("nan")
        if lowered in _POSITIVE_INFINITY_TOKENS:
            return float("inf")
        if lowered in _NEGATIVE_INFINITY_TOKENS:
            return float("-inf")
        if _INTEGER_RE.match(token):
            return int(token, 10)
        if _FLOAT_RE.match(token):
            return float(token)
        raise ValueError("unsupported value")

    def _skip_whitespace(self):
        while self.position < len(self.text) and self.text[self.position] in " \t":
            self.position += 1

    def _consume(self, expected):
        if self.position < len(self.text) and self.text[self.position] == expected:
            self.position += 1
            return True
        return False


def parse_assignments(response, known_types=None):
    """Parse a sampler response without evaluating any of its contents."""
    result = {}
    for line_number, line in enumerate(response.splitlines(), 1):
        if not line.strip():
            continue
        field_name = None
        if "=" in line:
            raw_field_name, raw_value = line.split("=", 1)
            field_name = raw_field_name.strip()
        else:
            raw_value = ""
        try:
            if field_name is None or not _FIELD_RE.match(field_name) or keyword.iskeyword(field_name):
                raise ValueError("invalid field name")
            if field_name in result:
                raise ValueError("duplicate field")
            value = _ValueParser(raw_value).parse()
            if known_types is not None and field_name in known_types:
                expected = known_types[field_name]
                expected_types = expected if isinstance(expected, tuple) else (expected,)
                if not any(type(value) is expected_type for expected_type in expected_types):
                    raise ValueError("unexpected field type")
            result[field_name] = value
        except (TypeError, ValueError) as error:
            raise ProtocolError(str(error), response, line_number, field_name)
    return result
