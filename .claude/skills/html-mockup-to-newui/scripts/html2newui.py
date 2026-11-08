#!/usr/bin/env python3
"""Converts an HTML/CSS mockup into a newui .newui View document (approximate, one time conversion).

usage: html2newui.py mockup.html [-o out.newui] [--name frameName] [--title T] [--width W] [--height H]
                     [--fragment] [--base-font-size N] [--font NAME] [--strict]

Stdlib only. Reads inline styles, <style> blocks (tag, .class, #id, [attr], descendant and child selectors,
:root custom properties) and a practical subset of CSS: flex layout, gap, padding, width/height, colours,
linear-gradient, fonts, borders, border-radius, opacity, absolute positioning, simple grid and tables.
Everything it cannot represent is listed on stderr as a warning. See ../SKILL.md for the mapping tables.
"""
import argparse
import datetime
import html.parser
import math
import os
import re
import sys

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, "..", "..", "newui-view-json5", "scripts"))
import json5lite  # noqa: E402

warnings = []


def warn(message):
    if message not in warnings:
        warnings.append(message)


# ---------------------------------------------------------------- DOM and CSS

voidTags = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "source", "track", "wbr"}
skipTags = {"script", "style", "head", "meta", "link", "title", "template", "noscript", "option", "optgroup", "datalist"}
inlineTags = {"span", "a", "b", "i", "u", "em", "strong", "small", "code", "label", "button", "input", "select", "textarea", "img", "abbr", "sub", "sup", "br", "kbd"}
textTags = {"span", "a", "b", "i", "u", "em", "strong", "small", "code", "abbr", "sub", "sup", "kbd", "label"}
inherited = {"color", "font-family", "font-size", "font-weight", "font-style", "text-decoration", "text-align", "white-space"}


class Node:
    def __init__(self, tag, attrs=None, parent=None):
        self.tag = tag
        self.attrs = attrs or {}
        self.parent = parent
        self.children = []
        self.text = ""
        self.style = {}
        self.custom = {}

    def classes(self):
        return self.attrs.get("class", "").split()


class DomBuilder(html.parser.HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.root = Node("#root")
        self.stack = [self.root]
        self.styles = []
        self.inStyle = False
        self.inScript = False
        self.inTitle = False
        self.title = ""

    def handle_starttag(self, tag, attrs):
        if tag == "style":
            self.inStyle = True
        elif tag == "script":
            self.inScript = True
        elif tag == "title":
            self.inTitle = True
        node = Node(tag, {k: (v if v is not None else "") for k, v in attrs}, self.stack[-1])
        self.stack[-1].children.append(node)
        if tag not in voidTags:
            self.stack.append(node)

    def handle_startendtag(self, tag, attrs):
        self.handle_starttag(tag, attrs)
        if tag not in voidTags:
            self.handle_endtag(tag)

    def handle_endtag(self, tag):
        if tag == "style":
            self.inStyle = False
        elif tag == "script":
            self.inScript = False
        elif tag == "title":
            self.inTitle = False
        for i in range(len(self.stack) - 1, 0, -1):
            if self.stack[i].tag == tag:
                del self.stack[i:]
                return

    def handle_data(self, data):
        if self.inStyle:
            self.styles.append(data)
        elif self.inScript:
            return
        elif self.inTitle:
            self.title += data
        else:
            text = re.sub(r"\s+", " ", data)
            if text.strip():
                node = Node("#text", parent=self.stack[-1])
                node.text = text.strip()
                self.stack[-1].children.append(node)


def stripComments(text):
    return re.sub(r"/\*.*?\*/", "", text, flags=re.S)


def stripAtRules(text):
    out = []
    i = 0
    while i < len(text):
        if text[i] == "@":
            brace = text.find("{", i)
            semi = text.find(";", i)
            if semi != -1 and (brace == -1 or semi < brace):
                i = semi + 1
                continue
            depth = 0
            j = brace
            while j < len(text):
                depth += text[j] == "{"
                depth -= text[j] == "}"
                j += 1
                if depth == 0:
                    break
            warn("at-rule %s ignored (media queries, keyframes and font-face have no .newui equivalent)" % text[i:i + 20].split("{")[0].strip())
            i = j
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def splitTop(text, sep):
    parts, depth, cur = [], 0, []
    for c in text:
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        if c == sep and depth == 0:
            parts.append("".join(cur))
            cur = []
        else:
            cur.append(c)
    parts.append("".join(cur))
    return parts


def parseDecls(text):
    decls = {}
    for part in splitTop(text, ";"):
        if ":" not in part:
            continue
        prop, value = part.split(":", 1)
        prop = prop.strip().lower()
        value = re.sub(r"\s*!important\s*$", "", value.strip(), flags=re.I)
        if prop:
            decls[prop] = value
    return decls


def splitBox(value):
    parts = value.split()
    if len(parts) == 1:
        return parts * 4
    if len(parts) == 2:
        return [parts[0], parts[1], parts[0], parts[1]]
    if len(parts) == 3:
        return [parts[0], parts[1], parts[2], parts[1]]
    return parts[:4]


def isColorToken(token):
    t = token.lower()
    return t.startswith("#") or t.startswith(("rgb(", "rgba(", "hsl(", "hsla(", "var(")) or t in ("transparent", "currentcolor") or t in knownColors() or (t.isalpha() and t not in ("none", "solid", "dashed", "dotted", "auto", "inherit", "bold", "normal", "italic", "oblique"))


def expand(prop, value):
    if prop in ("margin", "padding"):
        return [("%s-%s" % (prop, side), v) for side, v in zip(("top", "right", "bottom", "left"), splitBox(value))]
    if prop == "border":
        out = []
        for token in splitTop(value, " "):
            token = token.strip()
            if not token:
                continue
            if re.match(r"^[\d.]+(px|pt|em)?$", token):
                out.append(("border-width", token))
            elif token in ("solid", "dashed", "dotted", "double", "none", "hidden"):
                out.append(("border-style", token))
            elif isColorToken(token):
                out.append(("border-color", token))
        if value.strip() in ("none", "0"):
            out.append(("border-width", "0"))
        return out
    if prop == "background":
        if "gradient(" in value:
            return [("background-image", value)]
        for token in splitTop(value, " "):
            if isColorToken(token.strip()):
                return [("background-color", token.strip())]
        return []
    if prop == "flex":
        first = value.split()[0] if value.split() else "0"
        return [("flex-grow", "0" if first in ("none", "initial") else ("1" if first == "auto" else first))]
    if prop == "gap":
        parts = value.split()
        return [("row-gap", parts[0]), ("column-gap", parts[-1])]
    if prop == "font":
        size = re.search(r"(?:^|\s)(\d+(?:\.\d+)?(?:px|pt|em|rem))(?:/[\d.a-z]+)?\s+(.+)$", value)
        out = []
        if size:
            out += [("font-size", size.group(1)), ("font-family", size.group(2))]
        if "italic" in value:
            out.append(("font-style", "italic"))
        weight = re.search(r"\b(bold|[1-9]00)\b", value)
        if weight:
            out.append(("font-weight", weight.group(1)))
        return out
    return [(prop, value)]


_known = []


def knownColors():
    if not _known:
        names = set()
        for candidate in (os.environ.get("NEWUI_DIR", r"D:\code\newui"),):
            path = os.path.join(candidate, "include", "newui", "color_constants.h")
            if os.path.exists(path):
                names = set(re.findall(r'\{"([a-z]+)",\s*0x', open(path, encoding="utf-8", errors="replace").read()))
        _known.append(names)
    return _known[0]


class Rule:
    def __init__(self, selector, decls, order):
        self.selector = selector
        self.decls = decls
        self.order = order
        self.parts = None
        self.specificity = (0, 0, 0)


compoundRe = re.compile(r"(\*|[a-zA-Z][\w-]*)?((?:[#.][\w-]+|\[[^\]]+\]|:[\w-]+(?:\([^)]*\))?)*)")


def parseCompound(text):
    m = compoundRe.fullmatch(text)
    if not m:
        return None
    tag = m.group(1)
    ids, classes, attrs, pseudos = [], [], [], []
    for token in re.findall(r"[#.][\w-]+|\[[^\]]+\]|:[\w-]+(?:\([^)]*\))?", m.group(2)):
        if token[0] == "#":
            ids.append(token[1:])
        elif token[0] == ".":
            classes.append(token[1:])
        elif token[0] == "[":
            am = re.match(r"\[\s*([\w-]+)\s*(?:=\s*[\"']?([^\"'\]]*)[\"']?)?\s*\]", token)
            attrs.append((am.group(1), am.group(2)) if am else (token, None))
        else:
            pseudos.append(token[1:])
    return {"tag": None if tag in (None, "*") else tag.lower(), "ids": ids, "classes": classes, "attrs": attrs, "pseudos": pseudos}


def parseSelector(selector):
    selector = selector.strip()
    tokens = re.findall(r">|\+|~|[^\s>+~]+", selector)
    parts, combinator = [], " "
    for token in tokens:
        if token in (">", "+", "~"):
            combinator = token
            continue
        compound = parseCompound(token)
        if compound is None:
            return None
        parts.append((combinator, compound))
        combinator = " "
    return parts


ignoredPseudos = {"hover", "focus", "active", "disabled", "checked", "first-child", "last-child", "nth-child", "focus-within", "visited", "before", "after", "placeholder", "selection"}


def matchCompound(node, c):
    if node.tag.startswith("#"):
        return False
    if c["tag"] and node.tag != c["tag"]:
        return False
    if any(i != node.attrs.get("id") for i in c["ids"]):
        return False
    cls = node.classes()
    if any(k not in cls for k in c["classes"]):
        return False
    for name, value in c["attrs"]:
        if name not in node.attrs or (value is not None and node.attrs[name] != value):
            return False
    for p in c["pseudos"]:
        if p == "root":
            if node.tag != "html" and node.parent is not None and node.parent.tag != "#root":
                return False
        elif p.split("(")[0] in ignoredPseudos:
            return False
    return True


def matchSelector(node, parts):
    if not matchCompound(node, parts[-1][1]):
        return False
    if len(parts) == 1:
        return True
    combinator = parts[-1][0]
    rest = parts[:-1]
    if combinator == ">":
        return node.parent is not None and matchSelector(node.parent, rest)
    if combinator == " ":
        p = node.parent
        while p is not None:
            if matchSelector(p, rest):
                return True
            p = p.parent
    return False


def parseStylesheet(text):
    rules = []
    text = stripAtRules(stripComments(text))
    for order, m in enumerate(re.finditer(r"([^{}]+)\{([^{}]*)\}", text)):
        decls = parseDecls(m.group(2))
        for selector in m.group(1).split(","):
            parts = parseSelector(selector)
            if parts is None:
                warn("selector '%s' is not supported and was skipped" % selector.strip())
                continue
            if any(c in ("+", "~") for c, _ in parts):
                warn("sibling combinator in '%s' is not supported and was skipped" % selector.strip())
                continue
            if any(p.split("(")[0] in ignoredPseudos for _, comp in parts for p in comp["pseudos"]):
                continue
            rule = Rule(selector.strip(), decls, order)
            rule.parts = parts
            ids = sum(len(c["ids"]) for _, c in parts)
            cls = sum(len(c["classes"]) + len(c["attrs"]) + len(c["pseudos"]) for _, c in parts)
            tags = sum(1 for _, c in parts if c["tag"])
            rule.specificity = (ids, cls, tags)
            rules.append(rule)
    rules.sort(key=lambda r: (r.specificity, r.order))
    return rules


# ----------------------------------------------------------------- computed style

tagDefaults = {
    "h1": {"font-size": "2em", "font-weight": "bold"},
    "h2": {"font-size": "1.5em", "font-weight": "bold"},
    "h3": {"font-size": "1.17em", "font-weight": "bold"},
    "h4": {"font-weight": "bold"},
    "h5": {"font-size": "0.83em", "font-weight": "bold"},
    "h6": {"font-size": "0.67em", "font-weight": "bold"},
    "b": {"font-weight": "bold"}, "strong": {"font-weight": "bold"}, "th": {"font-weight": "bold", "text-align": "center"},
    "i": {"font-style": "italic"}, "em": {"font-style": "italic"},
    "u": {"text-decoration": "underline"}, "code": {"font-family": "Consolas"}, "pre": {"font-family": "Consolas", "white-space": "pre"},
    "small": {"font-size": "0.83em"},
}
blockTags = {"html", "body", "div", "p", "h1", "h2", "h3", "h4", "h5", "h6", "ul", "ol", "li", "section", "header", "footer", "nav", "main", "aside", "article", "form", "fieldset", "table", "tr", "pre", "hr", "dl", "dt", "dd", "figure", "canvas", "svg", "video", "iframe", "thead", "tbody", "tfoot"}


def resolveVars(value, custom, depth=0):
    def sub(m):
        name, fallback = m.group(1), m.group(2)
        if name in custom and depth < 8:
            return resolveVars(custom[name], custom, depth + 1)
        return fallback.strip() if fallback else ""
    return re.sub(r"var\(\s*(--[\w-]+)\s*(?:,\s*([^)]*))?\)", sub, value)


def computeStyles(node, rules, parentStyle, parentCustom, baseFont, baseSize):
    if node.tag.startswith("#"):
        return
    decls = {}
    for rule in rules:
        if matchSelector(node, rule.parts):
            decls.update(rule.decls)
    decls.update(parseDecls(node.attrs.get("style", "")))
    custom = dict(parentCustom)
    for k, v in decls.items():
        if k.startswith("--"):
            custom[k] = v
    node.custom = custom

    style = {k: v for k, v in parentStyle.items() if k in inherited}
    style.update(tagDefaults.get(node.tag, {}))
    for prop, value in decls.items():
        if prop.startswith("--"):
            continue
        value = resolveVars(value, custom)
        if value.strip().lower() == "inherit":
            if prop in parentStyle:
                style[prop] = parentStyle[prop]
            continue
        for p, v in expand(prop, value):
            style[p] = v
    if "display" not in style:
        style["display"] = "block" if node.tag in blockTags else "inline"
    if "font-family" not in style:
        style["font-family"] = baseFont
    node.style = style
    node.fontSize = fontSize(style.get("font-size"), getattr(node.parent, "fontSize", baseSize), baseSize)
    for child in node.children:
        computeStyles(child, rules, style, custom, baseFont, baseSize)


def fontSize(value, parentSize, baseSize):
    if not value:
        return parentSize
    m = re.match(r"^([\d.]+)\s*(px|pt|em|rem|%)?$", value.strip())
    if not m:
        named = {"x-small": 10, "small": 12, "medium": 14, "large": 18, "x-large": 24}
        return named.get(value.strip(), parentSize)
    n, unit = float(m.group(1)), m.group(2) or "px"
    return {"px": n, "pt": n * 4 / 3, "em": n * parentSize, "rem": n * baseSize, "%": n * parentSize / 100}[unit]


def length(value, basis=None, em=12.0):
    if value is None:
        return None
    value = value.strip().lower()
    if value in ("", "auto", "none", "fit-content", "max-content", "min-content"):
        return None
    m = re.match(r"^(-?[\d.]+)\s*(px|pt|em|rem|%|vw|vh)?$", value)
    if not m:
        if "calc(" in value:
            warn("calc() is not evaluated (%s)" % value)
        return None
    n, unit = float(m.group(1)), m.group(2) or "px"
    if unit == "%":
        return ("pct", n / 100.0)
    return {"px": n, "pt": n * 4 / 3, "em": n * em, "rem": n * 16, "vw": n * 8, "vh": n * 6}[unit]


# ---------------------------------------------------------------------- colours

def clamp255(x):
    return max(0, min(255, int(round(x))))


def colorString(value, current="#000000"):
    v = value.strip().lower()
    if not v or v == "none":
        return None
    if v == "currentcolor":
        return current
    if v.startswith("#"):
        digits = v[1:]
        if len(digits) in (3, 4, 6, 8) and all(c in "0123456789abcdef" for c in digits):
            return "#" + digits
        warn("invalid hex colour %s" % value)
        return None
    m = re.match(r"^rgba?\(\s*([^)]*)\)$", v)
    if m:
        nums = re.split(r"[\s,/]+", m.group(1).strip())
        rgb = [(float(n[:-1]) * 2.55 if n.endswith("%") else float(n)) for n in nums[:3]]
        a = 1.0
        if len(nums) > 3:
            a = float(nums[3][:-1]) / 100 if nums[3].endswith("%") else float(nums[3])
        return "#%02x%02x%02x%02x" % (clamp255(rgb[0]), clamp255(rgb[1]), clamp255(rgb[2]), clamp255(a * 255))
    m = re.match(r"^hsla?\(\s*([^)]*)\)$", v)
    if m:
        nums = re.split(r"[\s,/]+", m.group(1).strip())
        h = float(nums[0].replace("deg", "")) % 360 / 360
        s, l = float(nums[1].rstrip("%")) / 100, float(nums[2].rstrip("%")) / 100
        a = float(nums[3].rstrip("%")) / (100 if nums[3].endswith("%") else 1) if len(nums) > 3 else 1.0

        def channel(p, q, t):
            t %= 1
            if t < 1 / 6:
                return p + (q - p) * 6 * t
            if t < 1 / 2:
                return q
            if t < 2 / 3:
                return p + (q - p) * (2 / 3 - t) * 6
            return p

        q = l * (1 + s) if l < 0.5 else l + s - l * s
        p = 2 * l - q
        return "#%02x%02x%02x%02x" % (clamp255(channel(p, q, h + 1 / 3) * 255), clamp255(channel(p, q, h) * 255), clamp255(channel(p, q, h - 1 / 3) * 255), clamp255(a * 255))
    names = knownColors()
    if v in names or (not names and v.isalpha()):
        return v
    warn("colour '%s' is not a known CSS colour name and was dropped" % value)
    return None


def parseGradient(value, current):
    m = re.match(r"^\s*(repeating-)?linear-gradient\((.*)\)\s*$", value, re.S | re.I)
    if not m:
        warn("only linear-gradient backgrounds are supported (%s)" % value[:40])
        return None
    args = [a.strip() for a in splitTop(m.group(2), ",")]
    angle = 180.0
    directions = {"to bottom": 180, "to top": 0, "to right": 90, "to left": 270, "to bottom right": 135, "to top right": 45, "to bottom left": 225, "to top left": 315}
    first = args[0].lower()
    if first in directions:
        angle = directions[first]
        args = args[1:]
    elif re.match(r"^-?[\d.]+deg$", first):
        angle = float(first[:-3])
        args = args[1:]
    stops = []
    for a in args:
        tm = re.match(r"^(.*?)\s+(-?[\d.]+%)$", a)
        color, pos = (tm.group(1), float(tm.group(2)[:-1]) / 100) if tm else (a, None)
        cs = colorString(color, current)
        if cs:
            stops.append([pos, cs])
    if len(stops) < 2:
        return None
    for i, s in enumerate(stops):
        if s[0] is None:
            s[0] = 0.0 if i == 0 else 1.0 if i == len(stops) - 1 else None
    i = 0
    while i < len(stops):
        if stops[i][0] is None:
            j = i
            while stops[j][0] is None:
                j += 1
            lo = stops[i - 1][0]
            for k in range(i, j):
                stops[k][0] = lo + (stops[j][0] - lo) * (k - i + 1) / (j - i + 1)
            i = j
        i += 1
    return {"angle": angle, "stops": stops}


def fillFor(v):
    spec = v.gradient
    if spec:
        a = math.radians(spec["angle"])
        dx, dy = math.sin(a), -math.cos(a)
        half = (abs(v.w * math.sin(a)) + abs(v.h * math.cos(a))) / 2
        cx, cy = v.w / 2, v.h / 2
        return {"type": "Fill", "kind": "Gradient", "gradient": {
            "type": "Gradient", "kind": "Linear",
            "linearStart": {"type": "Point", "x": round(cx - dx * half, 1), "y": round(cy - dy * half, 1)},
            "linearEnd": {"type": "Point", "x": round(cx + dx * half, 1), "y": round(cy + dy * half, 1)},
            "stops": [{"type": "GradientStop", "offset": round(s[0], 4), "color": s[1]} for s in spec["stops"]]}}
    if v.background:
        return {"type": "Fill", "kind": "Color", "color": v.background}
    return None


# ------------------------------------------------------------------- view records

class V:
    def __init__(self, cls, node=None, name=None):
        self.cls = cls
        self.node = node
        self.name = name
        self.props = {}
        self.children = []
        self.fixedW = self.fixedH = None
        self.mode = "leaf"
        self.padding = 0.0
        self.gap = 0.0
        self.justify = "Start"
        self.align = "Stretch"
        self.wrap = False
        self.weight = 0.0
        self.absolute = None
        self.background = None
        self.gradient = None
        self.borderWidth = 0.0
        self.borderColor = None
        self.radius = 0.0
        self.opacity = 1.0
        self.font = None
        self.textColor = None
        self.textAlign = None
        self.verticalTextAlign = None
        self.textAlignExplicit = False
        self.wordWrap = False
        self.text = None
        self.x = self.y = self.w = self.h = 0.0
        self.natural = True
        self.gridColumns = None
        self.captionHeight = 0.0


def fontObject(node):
    s = node.style
    family = s.get("font-family", "Segoe UI").split(",")[0].strip().strip("\"'")
    generic = {"sans-serif": "Segoe UI", "system-ui": "Segoe UI", "-apple-system": "Segoe UI", "blinkmacsystemfont": "Segoe UI", "serif": "Georgia",
               "monospace": "Consolas", "ui-monospace": "Consolas", "cursive": "Segoe UI", "fantasy": "Segoe UI"}
    installed = {"segoe ui", "arial", "calibri", "consolas", "times new roman", "verdana", "tahoma", "courier new", "georgia", "cascadia mono", "cascadia code", "trebuchet ms"}
    lower = family.lower()
    if lower in generic:
        family = generic[lower]
    elif lower not in installed:
        warn("font family '%s' is not a common Windows font; used 'Segoe UI'" % family)
        family = "Segoe UI"
    weight = s.get("font-weight", "normal")
    deco = s.get("text-decoration", "")
    return {"type": "Font", "name": family, "size": round(node.fontSize, 1) if node.fontSize != int(node.fontSize) else int(node.fontSize),
            "bold": weight == "bold" or (weight.isdigit() and int(weight) >= 600), "italic": s.get("font-style") == "italic",
            "strikeThrough": "line-through" in deco, "underlined": "underline" in deco}


def styleCommon(v, node, ctxEm):
    s = node.style
    current = colorString(s.get("color", "#000000")) or "#000000"
    v.textColor = colorString(s["color"], current) if "color" in s else None
    if "background-image" in s and "gradient(" in s["background-image"]:
        v.gradient = parseGradient(s["background-image"], current)
    elif "background-image" in s and "url(" in s["background-image"]:
        warn("background images are not converted (%s)" % (v.name or node.tag))
    if "background-color" in s:
        v.background = colorString(s["background-color"], current)
        if v.background in ("transparent", "#00000000"):
            v.background = None
    bw = length(s.get("border-width"), None, ctxEm)
    if s.get("border-style") in ("none", "hidden"):
        bw = 0
    if bw is None and s.get("border-style") in ("solid", "dashed", "dotted", "double"):
        bw = 1
    if isinstance(bw, float) and bw > 0:
        v.borderWidth = bw
        v.borderColor = colorString(s.get("border-color", current), current)
    r = length(s.get("border-radius", "").split()[0] if s.get("border-radius") else None, None, ctxEm)
    if isinstance(r, float):
        v.radius = r
        if len(s.get("border-radius", "").split()) > 1:
            warn("border-radius with several corner values uses only the first (%s)" % (v.name or node.tag))
    try:
        v.opacity = float(s.get("opacity", "1"))
    except ValueError:
        pass
    v.textAlign = {"left": "Left", "center": "Center", "right": "Right", "start": "Left", "end": "Right"}.get(s.get("text-align", "left"), "Left")
    v.textAlignExplicit = "text-align" in s   # an unstyled mockup reports "left", which is not a request for it
    v.verticalTextAlign = {"top": "Top", "middle": "Middle", "bottom": "Bottom"}.get(s.get("vertical-align", ""))
    v.wordWrap = s.get("white-space") not in ("nowrap", "pre")
    if "box-shadow" in s:
        warn("box-shadow has no ViewStyle equivalent and was dropped (%s)" % (v.name or node.tag))
    if "transform" in s:
        warn("transform is not supported and was ignored (%s)" % (v.name or node.tag))
    if any(("margin-" + side) in s and length(s["margin-" + side], None, ctxEm) not in (None, 0, 0.0) for side in ("top", "right", "bottom", "left")):
        warn("margins are ignored (use gap/padding); first seen on %s" % (v.name or node.tag))


def sizesFromStyle(v, node):
    s = node.style
    em = node.fontSize
    for attr, key in (("fixedW", "width"), ("fixedH", "height")):
        value = length(s.get(key), None, em)
        if value is None and key in node.attrs and node.attrs[key].isdigit():
            value = float(node.attrs[key])
        setattr(v, attr, value)
    for key in ("min-width", "min-height"):
        if key in s and v.fixedW is None and key == "min-width":
            m = length(s[key], None, em)
            if isinstance(m, float):
                v.fixedW = m
        if key in s and v.fixedH is None and key == "min-height":
            m = length(s[key], None, em)
            if isinstance(m, float):
                v.fixedH = m
    grow = s.get("flex-grow")
    if grow:
        try:
            v.weight = float(grow)
        except ValueError:
            pass
    if s.get("position") == "absolute":
        v.absolute = (length(s.get("left"), None, em), length(s.get("top"), None, em))
        if v.absolute[0] is None and v.absolute[1] is None:
            v.absolute = (0.0, 0.0)


def containerStyle(v, node):
    s = node.style
    em = node.fontSize
    display = s.get("display", "block")
    pad = [length(s.get("padding-" + side), None, em) for side in ("top", "right", "bottom", "left")]
    pad = [p if isinstance(p, float) else 0.0 for p in pad]
    if len(set(pad)) > 1:
        warn("asymmetric padding on %s approximated by its average" % (v.name or node.tag))
    v.padding = sum(pad) / 4
    gap = length(s.get("column-gap" if s.get("flex-direction", "row") in ("row", "row-reverse") else "row-gap"), None, em)
    if gap is None:
        gap = length(s.get("gap", "").split()[0] if s.get("gap") else None, None, em)
    v.gap = gap if isinstance(gap, float) else 0.0
    if display in ("flex", "inline-flex"):
        direction = s.get("flex-direction", "row")
        v.mode = "row" if direction in ("row", "row-reverse") else "col"
        if direction.endswith("reverse"):
            warn("reverse flex direction on %s keeps source order" % (v.name or node.tag))
        v.justify = {"flex-start": "Start", "start": "Start", "center": "Center", "flex-end": "End", "end": "End", "space-between": "SpaceBetween",
                     "space-around": "SpaceAround", "space-evenly": "SpaceEvenly"}.get(s.get("justify-content", "flex-start"), "Start")
        v.align = {"flex-start": "Start", "start": "Start", "center": "Center", "flex-end": "End", "end": "End", "stretch": "Stretch", "normal": "Stretch"}.get(s.get("align-items", "stretch"), "Stretch")
        v.wrap = s.get("flex-wrap", "nowrap") == "wrap"
    elif display == "grid":
        v.mode = "grid"
        tracks = s.get("grid-template-columns", "")
        m = re.match(r"repeat\(\s*(\d+)\s*,\s*([^)]+)\)", tracks.strip())
        if m:
            v.gridColumns = [m.group(2).strip()] * int(m.group(1))
        elif tracks.strip():
            v.gridColumns = tracks.split()
        else:
            v.gridColumns = ["1fr"]
            warn("grid without grid-template-columns on %s becomes a single column" % (v.name or node.tag))
        v.gap = v.gap
    else:
        v.mode = "block"


# ------------------------------------------------------------------ conversion

class Converter:
    def __init__(self, baseSize):
        self.baseSize = baseSize
        self.counts = {}

    def uniqueName(self, node, prefix):
        ident = node.attrs.get("id") or node.attrs.get("name")
        if ident:
            return re.sub(r"[^A-Za-z0-9_]", "_", ident)
        self.counts[prefix] = self.counts.get(prefix, 0) + 1
        return "%s%d" % (prefix, self.counts[prefix])

    def textOf(self, node):
        if node.tag == "#text":
            return node.text
        if node.tag == "br":
            return "\n"
        return " ".join(t for t in (self.textOf(c) for c in node.children) if t)

    def isTextOnly(self, node):
        return all(c.tag == "#text" or (c.tag in textTags and self.isTextOnly(c)) or c.tag == "br" for c in node.children)

    def label(self, node, text, name=None, hot=False):
        v = V("Label", node, name)
        v.text = text
        v.mode = "leaf"
        sizesFromStyle(v, node)
        styleCommon(v, node, node.fontSize)
        v.font = fontObject(node)
        v.hot = hot or node.tag == "a"
        if any(isinstance(length(node.style.get("padding-" + side), None, node.fontSize), float) and length(node.style.get("padding-" + side), None, node.fontSize) > 0 for side in ("top", "right", "bottom", "left")):
            warn("padding on text element '%s' is ignored (a Label has no inner padding)" % (name or text[:20]))
        return v

    def anonymousLabel(self, parent, text):
        fake = Node("span", parent=parent)
        fake.style = {k: v for k, v in parent.style.items() if k in inherited}
        fake.style["display"] = "inline"
        fake.fontSize = parent.fontSize
        return self.label(fake, text)

    def convertChildren(self, node):
        out = []
        for child in node.children:
            out.extend(self.convert(child))
        return out

    def convert(self, node):
        tag = node.tag
        if tag == "#text":
            return [self.anonymousLabel(node.parent, node.text)]
        if tag in skipTags or tag.startswith("#"):
            return []
        if node.style.get("display") == "none":
            return []
        if node.style.get("visibility") == "hidden":
            warn("visibility:hidden on %s was treated as visible" % (node.attrs.get("id") or tag))
        method = getattr(self, "do_" + tag, None)
        v = method(node) if method else self.generic(node)
        return v if isinstance(v, list) else [v]

    def generic(self, node):
        tag = node.tag
        if tag in textTags and self.isTextOnly(node) or (tag in ("p", "h1", "h2", "h3", "h4", "h5", "h6", "li", "dt", "dd", "legend", "span", "td", "th", "pre") and self.isTextOnly(node) and node.children):
            text = ("\u2022 " if tag == "li" else "") + self.textOf(node)
            v = self.label(node, text, node.attrs.get("id") and self.uniqueName(node, "label"))
            if tag == "pre":
                v.wordWrap = False
            return v
        return self.container(node)

    def container(self, node, cls="SubView"):
        v = V(cls, node, node.attrs.get("id") and self.uniqueName(node, "panel"))
        containerStyle(v, node)
        sizesFromStyle(v, node)
        styleCommon(v, node, node.fontSize)
        if node.style.get("overflow") in ("auto", "scroll") or node.style.get("overflow-y") in ("auto", "scroll"):
            v.cls = "ScrollView" if cls == "SubView" else cls
        v.children = self.convertChildren(node)
        if v.mode == "grid":
            v = self.gridToRows(v)
        if v.cls == "SubView" and not v.children and not (v.background or v.gradient or v.borderWidth) and v.fixedW is None and v.fixedH is None:
            return []
        return v

    def gridToRows(self, v):
        columns = v.gridColumns
        weights = []
        for t in columns:
            m = re.match(r"^([\d.]+)fr$", t)
            weights.append(float(m.group(1)) if m else 0.0)
        rows = []
        for i in range(0, len(v.children), len(columns)):
            row = V("SubView", v.node)
            row.mode = "row"
            row.gap = v.gap
            row.align = "Stretch"
            row.children = v.children[i:i + len(columns)]
            for child, t, w in zip(row.children, columns, weights):
                if w:
                    child.weight = w
                else:
                    px = length(t, None, v.node.fontSize)
                    if isinstance(px, float):
                        child.fixedW = px
            rows.append(row)
        v.mode = "col"
        v.children = rows
        warn("grid on %s approximated by nested flex rows (track sizes are not persistable)" % (v.name or v.node.tag))
        return v

    def do_html(self, node):
        return self.convertChildren(node)

    def do_body(self, node):
        return self.container(node)

    def do_br(self, node):
        return []

    def do_hr(self, node):
        v = V("SubView", node)
        sizesFromStyle(v, node)
        v.fixedH = v.fixedH or 1.0
        v.background = colorString(node.style.get("border-color", "#c8c8c8")) or "#c8c8c8"
        return v

    def do_button(self, node):
        v = V("Button", node, self.uniqueName(node, "button"))
        v.text = self.textOf(node) or node.attrs.get("value", "")
        sizesFromStyle(v, node)
        styleCommon(v, node, node.fontSize)
        v.font = fontObject(node)
        v.disabled = "disabled" in node.attrs
        if v.background or v.borderWidth or v.radius:
            warn("custom colours/borders on button '%s' cannot be reproduced (native chrome); only text and enabled state carry over" % v.name)
        return v

    def do_input(self, node):
        kind = node.attrs.get("type", "text").lower()
        name = self.uniqueName(node, kind)
        v = None
        if kind in ("button", "submit", "reset"):
            v = V("Button", node, name)
            v.text = node.attrs.get("value", kind.title())
            v.font = fontObject(node)
        elif kind in ("checkbox", "radio"):
            v = V("Toggle", node, name)
            v.radio = kind == "radio"
            v.checked = "checked" in node.attrs
        elif kind == "range":
            v = V("Slider", node, name)
            lo, hi = float(node.attrs.get("min", 0) or 0), float(node.attrs.get("max", 100) or 100)
            value = float(node.attrs.get("value", lo) or lo)
            if (lo, hi) != (0.0, 100.0):
                warn("slider '%s' range %g..%g is not persistable (Slider keeps 0..100); value rescaled" % (name, lo, hi))
            v.value = (value - lo) / (hi - lo) * 100.0 if hi != lo else 0.0
            v.step = float(node.attrs["step"]) / (hi - lo) * 100.0 if node.attrs.get("step") and hi != lo else None
        elif kind in ("hidden",):
            return []
        else:
            if kind not in ("text", "search", "email", "url", "tel", "password", "number"):
                warn("input type '%s' on '%s' converted to a TextField" % (kind, name))
            if kind == "password":
                warn("password masking is not available; '%s' is a plain TextField" % name)
            v = V("TextField", node, name)
            v.text = node.attrs.get("value", "")
            v.placeholder = node.attrs.get("placeholder", "")
            v.font = fontObject(node)
        sizesFromStyle(v, node)
        styleCommon(v, node, node.fontSize)
        v.disabled = "disabled" in node.attrs
        return v

    def do_textarea(self, node):
        v = V("TextControl", node, self.uniqueName(node, "text"))
        v.text = self.textOf(node)
        v.font = fontObject(node)
        sizesFromStyle(v, node)
        styleCommon(v, node, node.fontSize)
        v.fixedH = v.fixedH or 80.0
        v.disabled = "disabled" in node.attrs
        return v

    def do_select(self, node):
        v = V("DropDownList", node, self.uniqueName(node, "select"))
        sizesFromStyle(v, node)
        styleCommon(v, node, node.fontSize)
        options = [self.textOf(o) for o in node.children if o.tag == "option"]
        if options:
            warn("options of <select> '%s' (%s) cannot be saved; populate its model in code" % (v.name, ", ".join(options[:4])))
        v.disabled = "disabled" in node.attrs
        return v

    def do_progress(self, node):
        v = V("Progress", node, self.uniqueName(node, "progress"))
        mx = float(node.attrs.get("max", 1) or 1)
        v.value = max(0.0, min(1.0, float(node.attrs.get("value", 0) or 0) / mx))
        sizesFromStyle(v, node)
        styleCommon(v, node, node.fontSize)
        return v

    def do_img(self, node):
        v = V("Image", node, self.uniqueName(node, "image"))
        v.imagePath = node.attrs.get("src", "")
        sizesFromStyle(v, node)
        styleCommon(v, node, node.fontSize)
        warn("image '%s' needs a real file path (src=%s) before it will load" % (v.name, v.imagePath[:40]))
        fit = node.style.get("object-fit")
        v.fill = "Stretch" if fit in (None, "fill", "cover", "contain") else "Align"
        return v

    def do_label(self, node):
        boxes = [c for c in node.children if c.tag == "input" and c.attrs.get("type", "").lower() in ("checkbox", "radio")]
        if boxes:
            row = V("SubView", node)
            row.mode = "row"
            row.gap = 6.0
            row.align = "Center"
            sizesFromStyle(row, node)
            styleCommon(row, node, node.fontSize)
            row.children = self.convertChildren(node)
            return row
        return self.generic(node)

    def do_fieldset(self, node):
        legend = next((c for c in node.children if c.tag == "legend"), None)
        v = self.container(node, "GroupBox")
        if not v:
            v = V("GroupBox", node)
        v.cls = "GroupBox"
        v.text = self.textOf(legend) if legend else ""
        v.children = [c for c in v.children if not (c.node is not None and c.node.tag == "legend")]
        v.captionHeight = 20.0
        if v.mode == "block" and node.style.get("display", "block") == "block":
            v.mode = "col"
        return v

    def do_legend(self, node):
        return []

    def do_ul(self, node):
        v = self.container(node)
        if v and v.mode == "block":
            v.mode = "col"
        return v

    do_ol = do_ul

    def do_table(self, node):
        v = V("SubView", node, node.attrs.get("id") and self.uniqueName(node, "table"))
        v.mode = "col"
        containerStyle(v, node)
        v.mode = "col"
        sizesFromStyle(v, node)
        styleCommon(v, node, node.fontSize)
        rows = []

        def collect(n):
            for c in n.children:
                if c.tag == "tr":
                    rows.append(c)
                elif c.tag in ("thead", "tbody", "tfoot"):
                    collect(c)
        collect(node)
        for tr in rows:
            row = V("SubView", tr)
            row.mode = "row"
            row.gap = 8.0
            styleCommon(row, tr, tr.fontSize)
            for cell in tr.children:
                if cell.tag in ("td", "th"):
                    for cv in self.convert(cell):
                        cv.weight = cv.weight or 1.0
                        row.children.append(cv)
            v.children.append(row)
        warn("table on %s converted to nested flex rows with equal column weights" % (v.name or "the page"))
        return v

    def placeholder(self, node, what):
        viewType = node.attrs.get("data-newui-type", "SubView")
        v = V(viewType, node, node.attrs.get("id") and self.uniqueName(node, what))
        sizesFromStyle(v, node)
        styleCommon(v, node, node.fontSize)
        v.placeholder = True
        if "background-color" not in node.style and viewType == "SubView":
            v.background = "#8080801a"
        warn("<%s> %s is a placeholder %s; draw it in code" % (node.tag, v.name or "", viewType))
        return v

    def do_canvas(self, node):
        return self.placeholder(node, "canvas")

    do_svg = do_canvas
    do_video = do_canvas
    do_iframe = do_canvas


# ----------------------------------------------------------------- layout pass

textWidthFactor = 0.56


def textSize(text, font, avail, wrap):
    size = font["size"]
    charW = size * textWidthFactor * (1.08 if font["bold"] else 1.0)
    lines = text.split("\n")
    widths = [len(line) * charW for line in lines]
    natural = max(widths) if widths else 0
    if wrap and avail and natural > avail:
        count = sum(max(1, math.ceil(w / max(avail, charW))) for w in widths)
        return (avail, count * size * 1.45)
    return (natural + 2, len(lines) * size * 1.45)


def resolve(value, basis):
    if isinstance(value, tuple):
        return value[1] * basis if basis is not None else None
    return value


def leafSize(v, avail):
    fw = resolve(v.fixedW, avail)
    fh = v.fixedH if not isinstance(v.fixedH, tuple) else None
    if v.cls == "Label":
        w, h = textSize(v.text, v.font, fw or avail, v.wordWrap)
    elif v.cls == "Button":
        w, h = textSize(v.text, v.font, None, False)
        w, h = max(75.0, w + 28.0), max(26.0, h + 8.0)
    elif v.cls == "Toggle":
        w, h = 18.0, 18.0
    elif v.cls == "TextField":
        w, h = float(v.node.attrs.get("size", 20)) * 7.0 + 12 if v.node and v.node.attrs.get("size", "").isdigit() else 160.0, 24.0
    elif v.cls == "TextControl":
        w, h = 240.0, 80.0
    elif v.cls == "Slider":
        w, h = 160.0, 26.0
    elif v.cls == "Progress":
        w, h = 160.0, 16.0
    elif v.cls == "DropDownList":
        w, h = 140.0, 24.0
    elif v.cls == "Image":
        w, h = 64.0, 64.0
    elif getattr(v, "placeholder", False):
        w, h = 200.0, 120.0
    else:
        w, h = 100.0, 24.0
    return (fw if fw is not None else w, fh if fh is not None else h)


def intrinsic(v, avail):
    fw = resolve(v.fixedW, avail)
    if not v.children and (getattr(v, "placeholder", False) or (v.mode in ("leaf", "block") and v.cls not in ("SubView", "ScrollView", "GroupBox"))):
        return leafSize(v, avail)
    pad = v.padding
    inner = (fw if fw is not None else avail)
    inner = inner - 2 * pad if inner is not None else None
    kids = [c for c in v.children if c.absolute is None]
    caption = v.captionHeight
    if v.mode == "row":
        sizes = [intrinsic(c, None) for c in kids]
        w = sum(s[0] for s in sizes) + v.gap * max(0, len(kids) - 1) + 2 * pad
        h = (max([s[1] for s in sizes]) if sizes else 0) + 2 * pad + caption
    elif v.mode == "col":
        sizes = [intrinsic(c, inner) for c in kids]
        w = (max([s[0] for s in sizes]) if sizes else 0) + 2 * pad
        h = sum(s[1] for s in sizes) + v.gap * max(0, len(kids) - 1) + 2 * pad + caption
    else:
        lines = flowLines(kids, inner or 600.0, v.gap or 6.0)
        w = max([sum(s[0] for s in line) + 6.0 * max(0, len(line) - 1) for line in lines] or [0]) + 2 * pad
        h = sum(max(s[1] for s in line) for line in lines) + 6.0 * max(0, len(lines) - 1) + 2 * pad + caption
        if inner is None and not lines:
            w = 2 * pad
    fh = v.fixedH if not isinstance(v.fixedH, tuple) else None
    if fw is not None:
        w = fw
    elif avail is not None and v.mode in ("block", "col") and v.cls == "SubView":
        w = max(w, avail)
    if fh is not None:
        h = fh
    return (w, h)


def isBlockChild(c):
    return c.node is not None and c.node.style.get("display") in ("block", "flex", "grid", "list-item", "table") and c.cls != "Button"


def flowLines(kids, width, gap):
    lines, line, used = [], [], 0.0
    for c in kids:
        s = intrinsic(c, width) if not isBlockChild(c) else (width, intrinsic(c, width)[1])
        if isBlockChild(c):
            if line:
                lines.append(line)
            lines.append([s])
            line, used = [], 0.0
            continue
        if line and used + s[0] > width:
            lines.append(line)
            line, used = [], 0.0
        line.append(s)
        used += s[0] + gap
    if line:
        lines.append(line)
    return lines


def place(v, x, y, w, h):
    v.x, v.y, v.w, v.h = x, y, w, h
    if not v.children:
        return
    pad = v.padding
    ix, iy = pad, pad + v.captionHeight
    iw, ih = w - 2 * pad, h - 2 * pad - v.captionHeight
    kids = [c for c in v.children if c.absolute is None]
    for c in v.children:
        if c.absolute is not None:
            cw, ch = intrinsic(c, iw)
            place(c, resolve(c.absolute[0], iw) or 0.0, resolve(c.absolute[1], ih) or 0.0, cw, ch)
    if v.mode in ("row", "col"):
        row = v.mode == "row"
        main = iw if row else ih
        cross = ih if row else iw
        sizes = [intrinsic(c, iw if not row else None) for c in kids]
        if v.wrap and row:
            placeWrapped(v, kids, sizes, ix, iy, iw)
            return
        fixedMain = [(s[0] if row else s[1]) for s in sizes]
        grow = [c.weight for c in kids]
        free = main - sum(fixedMain) - v.gap * max(0, len(kids) - 1)
        total = sum(grow)
        if total > 0 and free > 0:
            for i, g in enumerate(grow):
                if g:
                    fixedMain[i] += free * g / total
            free = 0
        elif free < 0:
            def pinned(c):
                return isinstance(c.fixedH if row is False else c.fixedW, float)
            shrinkable = [i for i, g in enumerate(grow) if g] or [i for i, c in enumerate(kids) if not pinned(c)]
            base = sum(fixedMain[i] for i in shrinkable)
            deficit = -free
            for i in shrinkable:
                fixedMain[i] = max(0.0, fixedMain[i] - deficit * (fixedMain[i] / base if base else 0))
        offset, between = 0.0, v.gap
        used = sum(fixedMain) + v.gap * max(0, len(kids) - 1)
        spare = main - used
        if v.justify == "Center":
            offset = spare / 2
        elif v.justify == "End":
            offset = spare
        elif v.justify == "SpaceBetween" and len(kids) > 1:
            between = v.gap + spare / (len(kids) - 1)
        elif v.justify == "SpaceAround" and kids:
            between = v.gap + spare / len(kids)
            offset = spare / len(kids) / 2
        elif v.justify == "SpaceEvenly" and kids:
            between = v.gap + spare / (len(kids) + 1)
            offset = spare / (len(kids) + 1)
        pos = offset
        for c, s, m in zip(kids, sizes, fixedMain):
            cs = s[1] if row else s[0]
            fixedCross = (c.fixedH if row else c.fixedW)
            if v.align == "Stretch" and not (isinstance(fixedCross, float)):
                cs = cross
            elif isinstance(fixedCross, tuple):
                cs = fixedCross[1] * cross
            co = {"Start": 0.0, "Stretch": 0.0, "Center": (cross - cs) / 2, "End": cross - cs}[v.align]
            if row:
                place(c, ix + pos, iy + co, m, cs)
            else:
                place(c, ix + co, iy + pos, cs, m)
            pos += m + between
    else:
        lines = []
        line, used = [], 0.0
        for c in kids:
            s = (iw if isBlockChild(c) else intrinsic(c, iw)[0], intrinsic(c, iw)[1])
            if isBlockChild(c):
                if line:
                    lines.append(line)
                lines.append([(c, s)])
                line, used = [], 0.0
                continue
            if line and used + s[0] > iw:
                lines.append(line)
                line, used = [], 0.0
            line.append((c, s))
            used += s[0] + 6.0
        if line:
            lines.append(line)
        cy = 0.0
        for line in lines:
            cx = 0.0
            lh = max(s[1] for _, s in line)
            for c, s in line:
                cw = resolve(c.fixedW, iw) if c.fixedW is not None else s[0]
                if isBlockChild(c) and c.fixedW is None:
                    cw = iw
                place(c, ix + cx, iy + cy, cw, s[1])
                cx += cw + 6.0
            cy += lh + 6.0


def placeWrapped(v, kids, sizes, ix, iy, iw):
    cx = cy = lineH = 0.0
    line = max(0.0, v.gap)
    for c, s in zip(kids, sizes):
        if cx and cx + s[0] > iw:
            cx, cy, lineH = 0.0, cy + lineH + line, 0.0
        place(c, ix + cx, iy + cy, s[0], s[1])
        cx += s[0] + v.gap
        lineH = max(lineH, s[1])


# ------------------------------------------------------------------- emission

def rnd(x):
    return int(round(x))


def bounds(x, y, w, h):
    return {"type": "Rect", "pos": {"type": "Point", "x": rnd(x), "y": rnd(y)}, "size": {"type": "Size", "width": rnd(w), "height": rnd(h)}}


def viewStyle(v, styleType="ViewStyle", extra=None):
    st = {}
    fill = fillFor(v)
    if fill:
        st["backgroundFill"] = fill
    if v.borderWidth:
        st["borderWidth"] = rnd(v.borderWidth)
        if v.borderColor:
            st["borderFill"] = v.borderColor
    if v.radius:
        st["rectRadius"] = rnd(v.radius)
    if v.opacity != 1.0:
        st["opacity"] = v.opacity
    if extra:
        st.update(extra)
    if not st:
        return None
    return dict([("type", styleType)] + list(st.items()))


def alignProps(v, d, horizontal, vertical, defaultH="Center", defaultV="Middle", always=False):
    """Writes the control's two alignment properties from the CSS text-align / vertical-align, only when they are explicit and differ from
    the control's own default (a Label always carries its horizontal one: an unstyled mockup means left there)."""
    if v.textAlign and (always or (v.textAlignExplicit and v.textAlign != defaultH)):
        d[horizontal] = v.textAlign
    if v.verticalTextAlign and v.verticalTextAlign != defaultV:
        d[vertical] = v.verticalTextAlign


def emit(v, root=False):
    d = {"type": v.cls}
    if v.cls == "Label":
        d["text"] = v.text
        if getattr(v, "hot", False):
            d["hotLink"] = True
        alignProps(v, d, "textAlignment", "verticalTextAlignment", always=True)
    elif v.cls in ("Button", "ToolbarButton"):
        if v.text:
            d["text"] = v.text
        alignProps(v, d, "textAlignment", "verticalTextAlignment")
    elif v.cls == "Toggle":
        if v.checked:
            d["checked"] = True
        if v.radio:
            d["radioStyle"] = True
        alignProps(v, d, "glyphAlignment", "verticalGlyphAlignment", defaultH="Left")
    elif v.cls == "TextField":
        if v.text:
            d["text"] = v.text
        if getattr(v, "placeholder", ""):
            d["placeholder"] = v.placeholder
        d["font"] = v.font
        if v.textColor:
            d["textColor"] = v.textColor
        alignProps(v, d, "textAlignment", "verticalTextAlignment", defaultH="Left", defaultV="Top")
    elif v.cls == "TextControl":
        d["text"] = v.text
        d["font"] = v.font
        d["wordWrap"] = True
        if v.textColor:
            d["textColor"] = v.textColor
    elif v.cls == "Slider":
        d["value"] = rnd(v.value)
        if v.step:
            d["step"] = round(v.step, 3)
    elif v.cls == "Progress":
        d["value"] = round(v.value, 3)
    elif v.cls == "GroupBox":
        d["text"] = v.text
    elif v.cls == "Image":
        d["imagePath"] = v.imagePath
        d["imageFillMode"] = v.fill
    if getattr(v, "disabled", False):
        d["enabled"] = False
    d["bounds"] = bounds(0 if root else v.x, 0 if root else v.y, v.w, v.h)
    if v.cls in ("SubView", "RootView"):
        d["visible"] = True
    if v.name:
        d["name"] = v.name
    if v.children and v.mode in ("row", "col") and not v.wrap and v.cls != "GroupBox":
        layout = {"type": "FlexLayout", "orientation": "Horizontal" if v.mode == "row" else "Vertical"}
        if v.justify != "Start":
            layout["mainAxisAlignment"] = v.justify
        if v.align != "Stretch":
            layout["crossAxisAlignment"] = v.align
        if v.gap:
            layout["spacing"] = rnd(v.gap)
        if v.padding or v.captionHeight:
            layout["padding"] = rnd(v.padding)
        d["layout"] = layout
    elif v.children and v.wrap and v.mode == "row":
        d["layout"] = {"type": "FlexLayout", "orientation": "Horizontal", "wrap": True, "spacing": rnd(v.gap), "lineSpacing": rnd(v.gap), "padding": rnd(v.padding)}
    if v.weight:
        d["layoutParams"] = {"type": "FlexLayoutParams", "weight": v.weight}
    style = None
    if v.cls == "Label":
        extra = {"text": v.text, "wordWrap": v.wordWrap, "font": v.font}
        if v.textColor:
            extra["textColor"] = v.textColor
        if v.textAlign:
            extra["textAlignment"] = v.textAlign
        if v.verticalTextAlign and v.verticalTextAlign != "Middle":
            extra["verticalTextAlignment"] = v.verticalTextAlign
        style = viewStyle(v, "LabelStyle", extra)
    elif v.cls == "Button":
        style = viewStyle(v, "ThemedButtonStyle", {"font": v.font})
    elif v.cls in ("SubView", "RootView", "ScrollView", "GroupBox"):
        style = viewStyle(v)
    if style:
        d["style"] = style
    if v.cls in ("ScrollView",):
        d["contentSize"] = {"type": "Size", "width": rnd(v.w), "height": rnd(max([c.y + c.h for c in v.children] or [v.h]))}
    explicit = (v.fixedW is not None or v.fixedH is not None) and not root
    if explicit:
        d["desiredSize"] = {"type": "Size", "width": rnd(v.w), "height": rnd(v.h)}
        d["desiredSizeOverride"] = True
    if v.cls in ("SubView", "RootView", "ScrollView", "GroupBox") or v.children:
        d["childViews"] = [emit(c) for c in v.children]
    return d


# ------------------------------------------------------------------------ main

def slug(text):
    s = re.sub(r"[^A-Za-z0-9]+", " ", text).strip().split()
    if not s:
        return "mockup"
    return s[0].lower() + "".join(w.title() for w in s[1:])


def build(htmlText, args):
    dom = DomBuilder()
    dom.feed(htmlText)
    dom.close()
    rules = parseStylesheet("\n".join(dom.styles))
    html = next((c for c in dom.root.children if c.tag == "html"), dom.root)
    body = next((c for c in html.children if c.tag == "body"), None)
    if body is None:
        body = Node("body", parent=html)
        body.children = [c for c in html.children if c.tag not in ("head",)]
        for c in body.children:
            c.parent = body
    for top in dom.root.children:
        computeStyles(top, rules, {}, {}, args.font, args.base_font_size)
    html.fontSize = getattr(html, "fontSize", args.base_font_size)
    if body is not html and not getattr(body, "style", None):
        computeStyles(body, rules, html.style if html.style else {}, {}, args.font, args.base_font_size)

    conv = Converter(args.base_font_size)
    root = conv.container(body)
    if not root:
        root = V("SubView", body)
        root.mode = "block"
    if not isinstance(root, V):
        root = root[0]
    root.cls = "RootView" if not args.fragment else "SubView"
    root.name = "rootview" if not args.fragment else (args.name or "panel")
    sizesFromStyle(root, body)

    width = args.width or (root.fixedW if isinstance(root.fixedW, float) else None)
    height = args.height or (root.fixedH if isinstance(root.fixedH, float) else None)
    if not width or not height:
        iw, ih = intrinsic(root, width or 800.0)
        width = width or max(iw, 320.0)
        height = height or max(ih, 200.0)
    root.fixedW = root.fixedH = None
    place(root, 0, 0, width, height)

    title = dom.title.strip() or "Mockup"
    name = args.name or slug(title)
    meta = {"author": "", "date": datetime.date.today().isoformat(), "copyright": "", "version": "0.1.0.900"}
    rootDoc = emit(root, root=True)
    if args.fragment:
        return dict([("meta", meta)] + list(rootDoc.items()))
    rootDoc["name"] = "rootview"
    frame = {
        "meta": meta, "type": "Frame", "title": args.title or title, "name": name,
        "bounds": bounds(40, 40, width, height), "rootView": rootDoc,
    }
    return frame


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("html")
    ap.add_argument("-o", "--output")
    ap.add_argument("--name", help="Frame name (the .newui file name Bundle::loadFrame looks for)")
    ap.add_argument("--title")
    ap.add_argument("--width", type=float)
    ap.add_argument("--height", type=float)
    ap.add_argument("--fragment", action="store_true", help="write a bare SubView document instead of a Frame")
    ap.add_argument("--base-font-size", type=float, default=12.0, help="px size of unstyled text (newui controls default to 12)")
    ap.add_argument("--font", default="Segoe UI")
    ap.add_argument("--strict", action="store_true", help="exit with status 2 when anything had to be dropped")
    args = ap.parse_args()

    text = open(args.html, encoding="utf-8").read()
    doc = build(text, args)
    out = json5lite.dumps(doc) + "\n"
    if args.output:
        os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
        open(args.output, "w", encoding="utf-8", newline="\n").write(out)
    else:
        sys.stdout.write(out)
    for w in warnings:
        sys.stderr.write("warning: " + w + "\n")
    if args.strict and warnings:
        sys.exit(2)


if __name__ == "__main__":
    main()
