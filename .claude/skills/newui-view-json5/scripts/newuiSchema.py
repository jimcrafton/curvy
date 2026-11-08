#!/usr/bin/env python3
"""Lists the reflected classes, properties and enums newui registers, from reflectgen's generated .cpp.

usage: newuiSchema.py [--newui DIR] [--generated FILE] [--class NAME | --enum NAME] [--json]

Without arguments it prints every class with its base and the enum names. With --class it prints that
class's own and inherited properties, fields and collections and whether reflection can construct it.
With --enum it prints the enum's value names. Class names drop the leading "newui::" (so gfx classes are
"gfx::Fill"); a bare name falls back to the gfx:: one. The generated file is searched under
<newui>/build/generated and <newui>/out/build/*/generated unless --generated is given.
"""
import argparse
import glob
import json
import os
import re
import sys

defaultNewui = os.environ.get("NEWUI_DIR", r"D:\code\newui")

registerRe = re.compile(r"void register_\w+(?:Reflection|Enum)\(\)\s*\{(.*?)\n\}", re.S)
classRe = re.compile(r"ClassBuilder<([\w:]+)>\s+builder;")
baseRe = re.compile(r"\.base<([\w:]+)>\(\)")
memberRe = re.compile(r"\.(property|field|propertyCollection|collection)\(\s*\"(\w+)\"")
enumRe = re.compile(r"EnumBuilder<([\w:]+)>\(\s*\"(\w+)\"")
enumValueRe = re.compile(r"\.addValue\(\s*\"(\w+)\"")


def qualified(name):
    return name[len("newui::"):] if name.startswith("newui::") else name


def findGenerated(newuiDir):
    found = []
    for pattern in (os.path.join(newuiDir, "build", "generated", "newui_reflection_generated.cpp"),
                    os.path.join(newuiDir, "out", "build", "*", "generated", "newui_reflection_generated.cpp")):
        found.extend(glob.glob(pattern))
    if not found:
        sys.exit("no newui_reflection_generated.cpp found under " + newuiDir + "; build the newui target first or pass --generated")
    return max(found, key=os.path.getmtime)


def parse(path):
    text = open(path, encoding="utf-8", errors="replace").read()
    classes = {}
    enums = {}
    for m in registerRe.finditer(text):
        body = m.group(1)
        cm = classRe.search(body)
        if cm:
            base = baseRe.search(body)
            members = {"property": [], "field": [], "collection": []}
            for mm in memberRe.finditer(body):
                kind = "collection" if "ollection" in mm.group(1) else mm.group(1)
                members[kind].append(mm.group(2))
            classes[qualified(cm.group(1))] = {
                "base": qualified(base.group(1)) if base else None,
                "properties": members["property"],
                "fields": members["field"],
                "collections": members["collection"],
                "constructible": ".constructor<>()" in body,
                "stringValue": ".stringValue(" in body,
            }
            continue
        em = enumRe.search(body)
        if em:
            enums[em.group(2)] = {"values": enumValueRe.findall(body), "flags": ".flags()" in body}
    return classes, enums


def chain(classes, name):
    out = []
    while name and name in classes:
        out.append(name)
        name = classes[name]["base"]
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--newui", default=defaultNewui)
    ap.add_argument("--generated")
    ap.add_argument("--class", dest="cls")
    ap.add_argument("--enum", dest="enum")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    path = args.generated or findGenerated(args.newui)
    classes, enums = parse(path)

    if args.enum:
        if args.enum not in enums:
            sys.exit("unknown enum " + args.enum + "; known: " + ", ".join(sorted(enums)))
        entry = enums[args.enum]
        print(json.dumps(entry) if args.json else ", ".join(entry["values"]) + ("  (flags)" if entry["flags"] else ""))
        return

    if args.cls:
        name = args.cls if args.cls in classes else "gfx::" + args.cls
        if name not in classes:
            sys.exit("unknown class " + args.cls)
        result = {"class": name, "constructible": classes[name]["constructible"], "stringValue": classes[name]["stringValue"],
                  "chain": chain(classes, name), "members": {}}
        for c in result["chain"]:
            result["members"][c] = {k: classes[c][k] for k in ("properties", "fields", "collections")}
        if args.json:
            print(json.dumps(result, indent=2))
            return
        flags = ("constructible" if result["constructible"] else "NOT constructible by reflection") + (", string value" if result["stringValue"] else "")
        print(name + " (" + flags + ") chain: " + " -> ".join(result["chain"]))
        for c in result["chain"]:
            for k in ("properties", "fields", "collections"):
                if result["members"][c][k]:
                    print("  " + c + " " + k + ": " + ", ".join(result["members"][c][k]))
        return

    if args.json:
        print(json.dumps({"classes": classes, "enums": enums}, indent=1))
        return
    print("# generated: " + path)
    for name in sorted(classes):
        base = classes[name]["base"]
        print(name + (" : " + base if base else ""))
    print("# enums: " + ", ".join(sorted(enums)))


if __name__ == "__main__":
    main()
