#!/usr/bin/env python3
"""Checks .newui (JSON5 View/Frame) documents against newui's registered reflection data, without building newui.

usage: validateNewui.py FILE [FILE ...] [--newui DIR] [--generated FILE] [--quiet]

Reports errors (the file will not load as intended) and warnings (it loads, but probably not as meant):
  - unknown "type" tags and classes reflection cannot construct (errors); property names a class does not have
    (warnings: the reader skips unknown keys, so old files still load, but a typo is silently ignored)
  - enum values that are not declared, Color strings that do not parse
  - plain SubView/RootView nodes without visible: true (they default to invisible)
  - malformed bounds/size, childViews that are not arrays, Frame without name or rootView
Exit status is 1 when any file has errors. Needs the generated newui_reflection_generated.cpp (build newui once)
and falls back to structural checks only when it cannot find it.
"""
import argparse
import os
import re
import sys

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import json5lite  # noqa: E402
import newuiSchema  # noqa: E402

enumProperties = {
    "orientation": "Orientation", "mainAxisAlignment": "MainAxisAlignment", "crossAxisAlignment": "CrossAxisAlignment",
    "textAlignment": "TextAlignment", "strokeStyle": "StrokeStyle", "imageFillMode": "ImageFillMode", "imageAlignment": "ImageAlignment",
}
colorProperties = {"textColor", "borderFill", "highlightFill", "color"}
rolePattern = re.compile(r"^(WindowBackground|WindowText|ControlBackground|ControlText|ControlBorder|DisabledText|HighlightBackground|HighlightText|LinkText|LinkHoverText)$", re.I)
hexPattern = re.compile(r"^#?([0-9a-fA-F]{3,4}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$")
alwaysAllowed = {"type", "delegates", "foo"}
frameKeys = {"meta", "rootView", "animations"}


class Checker:
    def __init__(self, classes, enums, colorNames):
        self.classes = classes
        self.enums = enums
        self.colorNames = colorNames
        self.errors = []
        self.warnings = []
        self.names = {}
        shortNames = {}
        for q in classes:
            short = q.split("::")[-1]
            if short not in shortNames or "::" not in q:
                shortNames[short] = q
        self.short = shortNames

    def error(self, path, message):
        self.errors.append("%s: %s" % (path or "<root>", message))

    def warn(self, path, message):
        self.warnings.append("%s: %s" % (path or "<root>", message))

    def members(self, qualified):
        names = set(alwaysAllowed)
        if qualified == "Frame":
            names |= frameKeys
        q = qualified
        while q and q in self.classes:
            c = self.classes[q]
            names.update(c["properties"], c["fields"], c["collections"])
            q = c["base"]
        return names

    def derives(self, qualified, ancestor):
        q = qualified
        while q:
            if q == ancestor:
                return True
            q = self.classes.get(q, {}).get("base")
        return False

    def color(self, path, value):
        if isinstance(value, dict):
            return
        if not isinstance(value, str):
            self.error(path, "a colour must be a string or a {type:\"Color\"} object")
        elif not (hexPattern.match(value) or rolePattern.match(value) or value.lower() in self.colorNames or (not self.colorNames and value.isalpha())):
            self.error(path, "colour %r is not hex, a CSS colour name or a UIColorRole name" % value)

    def checkSize(self, path, value):
        if not isinstance(value, dict):
            self.error(path, "must be an object")
            return
        for k in ("width", "height"):
            if k in value and (not isinstance(value[k], (int, float)) or value[k] < 0):
                self.error(path + "." + k, "must be a non-negative number")

    def checkBounds(self, path, value):
        if not isinstance(value, dict):
            self.error(path, "bounds must be a Rect object")
            return
        if "pos" in value and not isinstance(value["pos"], dict):
            self.error(path + ".pos", "must be a Point object")
        if "size" in value:
            self.checkSize(path + ".size", value["size"])

    def visit(self, path, value, expectedBase=None, asRoot=False):
        if not isinstance(value, dict):
            self.error(path, "expected an object")
            return
        tag = value.get("type")
        if tag is None:
            self.error(path, "object has no \"type\" tag, so it is skipped on load")
            return
        qualified = self.short.get(tag)
        if self.classes and qualified is None:
            self.error(path, "unknown type %r (not registered with reflection)" % tag)
            return
        if qualified is not None:
            c = self.classes[qualified]
            if not c["constructible"] and not (tag == "RootView" and expectedBase == "rootView"):
                self.error(path, "type %r cannot be constructed by reflection (no explicit default constructor)" % tag)
            allowed = self.members(qualified)
            for key in value:
                if key not in allowed:
                    hint = {"bkgFillStyle": " (old name; use backgroundFill)", "hilightFillStyle": " (old name; use highlightFill)"}.get(key, "")
                    self.warn(path + "." + key, "%s has no registered property %r%s; the reader skips it" % (tag, key, hint))
            if self.derives(qualified, "SubView") or self.derives(qualified, "View"):
                isControl = self.derives(qualified, "Control")
                if tag in ("SubView", "RootView") and value.get("visible") is not True:
                    self.error(path, "%s without visible: true stays invisible together with its whole subtree" % tag)
                if "name" in value:
                    if value["name"] in self.names:
                        self.warn(path, "name %r is also used at %s; View::findView returns the first" % (value["name"], self.names[value["name"]]))
                    self.names.setdefault(value["name"], path)
                if "childViews" in value and not isinstance(value["childViews"], list):
                    self.error(path + ".childViews", "must be an array")
                if "bounds" in value:
                    self.checkBounds(path + ".bounds", value["bounds"])
                elif not isControl or "bounds" not in value:
                    self.warn(path, "no bounds; the view keeps its constructor default size (0 x 0 for most) unless a FlexLayout assigns one")
        for key, child in value.items():
            childPath = (path + "." if path else "") + key
            if key in enumProperties and isinstance(child, str) and enumProperties[key] in self.enums:
                if child not in self.enums[enumProperties[key]]["values"]:
                    self.error(childPath, "%r is not a %s value (%s)" % (child, enumProperties[key], ", ".join(self.enums[enumProperties[key]]["values"])))
            if key in colorProperties:
                self.color(childPath, child)
            elif key == "kind" and tag == "Fill" and "PaintKind" in self.enums and child not in self.enums["PaintKind"]["values"]:
                self.error(childPath, "%r is not a PaintKind value" % child)
            elif key == "kind" and tag == "Gradient" and "GradientKind" in self.enums and child not in self.enums["GradientKind"]["values"]:
                self.error(childPath, "%r is not a GradientKind value" % child)
            if key == "childViews" and isinstance(child, list):
                for i, element in enumerate(child):
                    self.visit("%s[%d]" % (childPath, i), element)
            elif key == "rootView":
                self.visit(childPath, child, "rootView")
            elif isinstance(child, dict) and "type" in child and key not in ("meta", "delegates"):
                self.visit(childPath, child)
            elif isinstance(child, list) and key not in ("flags", "childViews", "animations"):
                for i, element in enumerate(child):
                    if isinstance(element, dict) and "type" in element:
                        self.visit("%s[%d]" % (childPath, i), element)


def namedColors(newuiDir):
    path = os.path.join(newuiDir, "include", "newui", "color_constants.h")
    if os.path.exists(path):
        return set(re.findall(r'\{"([a-z]+)",\s*0x', open(path, encoding="utf-8", errors="replace").read()))
    return set()


def checkDocument(checker, doc):
    if not isinstance(doc, dict):
        checker.error("", "the document root must be an object")
        return
    if "meta" in doc and not isinstance(doc["meta"], dict):
        checker.error("meta", "must be an object")
    if doc.get("type") == "Frame":
        if not doc.get("name"):
            checker.error("", "Frame has no name; Bundle::loadFrame resolves Resources/<name>.newui from it")
        if "rootView" not in doc:
            checker.error("", "Frame has no rootView")
    elif "type" not in doc:
        checker.warn("", "no root \"type\"; this reads as a multi-object document (named top-level objects)")
        for key, value in doc.items():
            if key != "meta":
                checker.visit(key, value)
        return
    checker.visit("", doc)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="+")
    ap.add_argument("--newui", default=newuiSchema.defaultNewui)
    ap.add_argument("--generated")
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--strict", action="store_true", help="exit 1 on warnings too")
    args = ap.parse_args()

    classes, enums = {}, {}
    try:
        schemaPath = args.generated or newuiSchema.findGenerated(args.newui)
        classes, enums = newuiSchema.parse(schemaPath)
        if not args.quiet:
            print("schema: %s (%d classes)" % (schemaPath, len(classes)))
    except SystemExit as e:
        sys.stderr.write("note: %s\n      only structural checks run\n" % e)

    failed = False
    for file in args.files:
        try:
            doc = json5lite.parse(open(file, encoding="utf-8").read())
        except (OSError, json5lite.Json5Error) as e:
            print("%s: cannot read: %s" % (file, e))
            failed = True
            continue
        checker = Checker(classes, enums, namedColors(args.newui))
        checkDocument(checker, doc)
        for message in checker.errors:
            print("%s: error: %s" % (file, message))
        for message in checker.warnings:
            print("%s: warning: %s" % (file, message))
        if not args.quiet:
            print("%s: %d error(s), %d warning(s)" % (file, len(checker.errors), len(checker.warnings)))
        failed = failed or bool(checker.errors) or (args.strict and bool(checker.warnings))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
