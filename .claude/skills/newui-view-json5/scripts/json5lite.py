"""A small JSON5 reader and the newui .newui writer style, for the skill scripts (stdlib only).

parse(text) accepts comments, unquoted keys, single or double quoted strings, trailing commas, hex numbers,
+/- signs, Infinity and NaN. dumps(value) writes the plain `key: value` style newui files use.
"""
import math
import re


class Json5Error(ValueError):
    pass


identStart = re.compile(r"[A-Za-z_$]")
identPart = re.compile(r"[A-Za-z0-9_$]")
numberRe = re.compile(r"[+-]?(?:0[xX][0-9a-fA-F]+|Infinity|NaN|(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?)")


class Parser:
    def __init__(self, text):
        self.text = text
        self.pos = 0

    def error(self, message):
        line = self.text.count("\n", 0, self.pos) + 1
        column = self.pos - (self.text.rfind("\n", 0, self.pos) + 1) + 1
        raise Json5Error("%s at line %d, column %d" % (message, line, column))

    def skip(self):
        t = self.text
        while self.pos < len(t):
            c = t[self.pos]
            if c in " \t\r\n\ufeff":
                self.pos += 1
            elif t.startswith("//", self.pos):
                end = t.find("\n", self.pos)
                self.pos = len(t) if end < 0 else end + 1
            elif t.startswith("/*", self.pos):
                end = t.find("*/", self.pos + 2)
                if end < 0:
                    self.error("unterminated comment")
                self.pos = end + 2
            else:
                break

    def value(self):
        self.skip()
        if self.pos >= len(self.text):
            self.error("unexpected end of input")
        c = self.text[self.pos]
        if c == "{":
            return self.obj()
        if c == "[":
            return self.array()
        if c in "\"'":
            return self.string()
        for word, result in (("true", True), ("false", False), ("null", None)):
            if self.text.startswith(word, self.pos) and not identPart.match(self.text[self.pos + len(word):self.pos + len(word) + 1] or " "):
                self.pos += len(word)
                return result
        m = numberRe.match(self.text, self.pos)
        if m:
            self.pos = m.end()
            token = m.group(0)
            sign = -1 if token.startswith("-") else 1
            body = token.lstrip("+-")
            if body.lower().startswith("0x"):
                return sign * int(body, 16)
            if body == "Infinity":
                return sign * math.inf
            if body == "NaN":
                return math.nan
            number = float(body)
            return sign * (int(number) if number.is_integer() and "." not in body and "e" not in body.lower() else number)
        self.error("unexpected character %r" % c)

    def string(self):
        quote = self.text[self.pos]
        self.pos += 1
        out = []
        t = self.text
        while self.pos < len(t):
            c = t[self.pos]
            if c == quote:
                self.pos += 1
                return "".join(out)
            if c == "\\":
                self.pos += 1
                e = t[self.pos]
                table = {"n": "\n", "t": "\t", "r": "\r", "b": "\b", "f": "\f", "0": "\0", "\n": ""}
                if e == "u":
                    out.append(chr(int(t[self.pos + 1:self.pos + 5], 16)))
                    self.pos += 4
                elif e == "x":
                    out.append(chr(int(t[self.pos + 1:self.pos + 3], 16)))
                    self.pos += 2
                else:
                    out.append(table.get(e, e))
                self.pos += 1
            else:
                out.append(c)
                self.pos += 1
        self.error("unterminated string")

    def key(self):
        self.skip()
        c = self.text[self.pos]
        if c in "\"'":
            return self.string()
        if identStart.match(c):
            start = self.pos
            while self.pos < len(self.text) and identPart.match(self.text[self.pos]):
                self.pos += 1
            return self.text[start:self.pos]
        self.error("expected a property name")

    def obj(self):
        self.pos += 1
        result = {}
        while True:
            self.skip()
            if self.text[self.pos:self.pos + 1] == "}":
                self.pos += 1
                return result
            k = self.key()
            self.skip()
            if self.text[self.pos:self.pos + 1] != ":":
                self.error("expected ':'")
            self.pos += 1
            result[k] = self.value()
            self.skip()
            c = self.text[self.pos:self.pos + 1]
            if c == ",":
                self.pos += 1
            elif c != "}":
                self.error("expected ',' or '}'")

    def array(self):
        self.pos += 1
        result = []
        while True:
            self.skip()
            if self.text[self.pos:self.pos + 1] == "]":
                self.pos += 1
                return result
            result.append(self.value())
            self.skip()
            c = self.text[self.pos:self.pos + 1]
            if c == ",":
                self.pos += 1
            elif c != "]":
                self.error("expected ',' or ']'")


def parse(text):
    p = Parser(text)
    value = p.value()
    p.skip()
    if p.pos != len(text):
        p.error("trailing content")
    return value


def number(v):
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, int):
        return str(v)
    if v == int(v) and abs(v) < 1e15:
        return str(int(v))
    return ("%.6f" % v).rstrip("0").rstrip(".")


def inline(v):
    if isinstance(v, dict):
        return "{ " + ", ".join("%s: %s" % (k, inline(x)) for k, x in v.items()) + " }"
    if isinstance(v, list):
        return "[" + ", ".join(inline(x) for x in v) + "]"
    if isinstance(v, str):
        return '"' + v.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'
    if v is None:
        return "null"
    return number(v)


def dumps(v, indent=0, inlineKeys=("pos", "size", "font", "color")):
    pad = "  " * indent
    if isinstance(v, dict):
        if not v:
            return "{}"
        if v.get("type") in ("Point", "Size", "Font", "Cursor") or (len(inline(v)) < 70 and not any(isinstance(x, (dict, list)) and x for x in v.values())):
            return inline(v)
        lines = []
        for k, x in v.items():
            lines.append("%s  %s: %s" % (pad, k, dumps(x, indent + 1)))
        return "{\n" + ",\n".join(lines) + "\n" + pad + "}"
    if isinstance(v, list):
        if not v:
            return "[]"
        if all(not isinstance(x, (dict, list)) for x in v):
            return inline(v)
        return "[\n" + ",\n".join("%s  %s" % (pad, dumps(x, indent + 1)) for x in v) + "\n" + pad + "]"
    return inline(v)
