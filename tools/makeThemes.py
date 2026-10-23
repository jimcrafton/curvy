"""Writes Resources/Themes/dark.theme and light.theme for curvy.

newui's ThemeData (3rdparty/newui/include/newui/themedata.h) loads "Themes/dark.theme" / "Themes/light.theme" from Resources/ and
UIColorManager::colorFor() and the themed controls consult it first. newui ships files captured from the Windows visual style
(3rdparty/newui/Resources/Themes). This script keeps their part sizes and insets and recolours them with curvy's tokens
(docs/dark mode.docx, include/curvy/theme.h), so every native-style control follows the same palette as the workspace.

    python tools/makeThemes.py

Colours are remapped by value from the Windows palette; a colour with no entry is reported and left alone.
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, "3rdparty", "newui", "Resources", "Themes")
OUTPUT = os.path.join(ROOT, "Resources", "Themes")

# Tokens, as in include/curvy/theme.h.
DARK = dict(bgApp="121212", bgSurface="1E1E1E", slot="2E2E2E", border="4A4A4A", text="F0F0F0", muted="A0A0A0", disabled="6E6E6E",
            accent="FFB454", accentDeep="5C3D12", link="FFC77A", onAccent="101010", hot="383838", press="454545")
LIGHT = dict(bgApp="F5F5F5", bgSurface="FFFFFF", slot="F0F0F0", border="C8C8C8", text="1A1A1A", muted="636363", disabled="9A9A9A",
             accent="B35C00", accentDeep="F6DDBE", link="8F4A00", onAccent="FFFFFF", hot="E6E6E6", press="D8D8D8")

# Windows palette (as captured) -> token.
DARK_MAP = {
    "000000": "bgSurface", "0f0f0f": "bgApp", "13171c": "bgSurface", "1c1c1c": "slot", "232323": "slot",
    "6f747d": "border", "4c4f52": "border", "4c4e54": "border",
    "3871b3": "accent", "122f4e": "accentDeep",
    "ffffff": "text", "929292": "muted", "a8a8a8": "muted", "7c7c7c": "disabled", "3399ff": "link",
}
LIGHT_MAP = {
    "ffffff": "bgSurface", "f0f0f0": "bgApp", "e3e7ec": "slot", "e3e3e3": "slot", "dcdcdc": "slot",
    "828790": "border", "adb0b3": "border", "abadb3": "border",
    "4c85c7": "accent", "b1ceed": "accentDeep",
    "000000": "text", "6d6d6d": "muted", "575757": "muted", "838383": "disabled", "0066cc": "link",
}

ROLES = {
    "WindowBackground": "bgApp", "ControlBackground": "bgSurface", "WindowText": "text", "ControlText": "text",
    "ControlBorder": "border", "DisabledText": "disabled", "LinkText": "link",
}


def colours(tokens, **named):
    """{'fillColor': 'slot', ...} -> the file's colour dict; 'none' is transparent, a 6-digit hex is taken as it is."""
    out = {}
    for key, token in named.items():
        value = "00000000" if token == "none" else (token + "ff" if token not in tokens and len(token) == 6 else tokens.get(token, token) + ("ff" if token in tokens else ""))
        out[key] = "#" + value.lower()
    return out


def states(data, tokens):
    """newui draws push buttons, toolbar buttons, check boxes and radio buttons from these colours when a theme is loaded, so
    every state gets its own (Windows' bitmaps carry none to sample)."""
    parts = data["parts"]

    def put(cls, part, state, **named):
        entry = parts.setdefault(cls, {}).setdefault(part, {}).setdefault(state, {})
        entry.setdefault("colors", {}).update(colours(tokens, **named))

    push = {"PBS_NORMAL": dict(fillColor="slot", borderColor="border", textColor="text"),
            "PBS_HOT": dict(fillColor="hot", borderColor="muted", textColor="text"),
            "PBS_PRESSED": dict(fillColor="accentDeep", borderColor="accent", textColor="text"),
            "PBS_DISABLED": dict(fillColor="bgSurface", borderColor="slot", textColor="disabled")}
    for state, named in push.items():
        put("BUTTON", "BP_PUSHBUTTON", state, **named)

    bar = {"TS_NORMAL": dict(fillColor="none", borderColor="none", textColor="text"),
           "TS_HOT": dict(fillColor="hot", borderColor="none", textColor="text"),
           "TS_PRESSED": dict(fillColor="press", borderColor="none", textColor="text"),
           "TS_CHECKED": dict(fillColor="accentDeep", borderColor="accent", textColor="text"),
           "TS_HOTCHECKED": dict(fillColor="accentDeep", borderColor="accent", textColor="text"),
           "TS_DISABLED": dict(fillColor="none", borderColor="none", textColor="disabled")}
    for state, named in bar.items():
        put("TOOLBAR", "TP_BUTTON", state, **named)

    for part, prefix in (("BP_CHECKBOX", "CBS"), ("BP_RADIOBUTTON", "RBS")):
        for suffix, named in {
                "UNCHECKEDNORMAL": dict(fillColor="bgSurface", borderColor="muted"),
                "UNCHECKEDHOT": dict(fillColor="slot", borderColor="text"),
                "UNCHECKEDPRESSED": dict(fillColor="press", borderColor="text"),
                "UNCHECKEDDISABLED": dict(fillColor="bgSurface", borderColor="disabled"),
                "CHECKEDNORMAL": dict(fillColor="accent", borderColor="accent", textColor="onAccent"),
                "CHECKEDHOT": dict(fillColor="accent", borderColor="text", textColor="onAccent"),
                "CHECKEDPRESSED": dict(fillColor="accent", borderColor="text", textColor="onAccent"),
                "CHECKEDDISABLED": dict(fillColor="disabled", borderColor="disabled", textColor="bgSurface")}.items():
            put("BUTTON", part, prefix + "_" + suffix, **named)


def kindOf(state):
    """What a state name stands for, from its spelling (CBS_CHECKEDHOT, TS_PRESSED, LISS_HOTSELECTED ...)."""
    if "DISABLED" in state:
        return "disabled"
    if "PRESSED" in state or "PUSHED" in state:
        return "pressed"
    if "SELECTED" in state:
        return "hotselected" if "HOT" in state else "selected"
    if "HOT" in state or "HOVER" in state:
        return "hot"
    if "FOCUSED" in state:
        return "focused"
    if "READONLY" in state:
        return "readonly"
    return "normal"


# (class, part) -> {kind: colours}; kinds missing from a family fall back to "normal". Each family is what a flat, themed look needs:
# newui's paintFromThemeData draws these parts (boxes, grooves, arrows) from the fill, border and glyph (text) colours.
def families():
    none = dict(fillColor="none", borderColor="none")
    groove = {"normal": dict(fillColor="border", borderColor="none")}
    thumb = {"normal": dict(fillColor="muted", borderColor="none"), "hot": dict(fillColor="text", borderColor="none"),
             "pressed": dict(fillColor="accent", borderColor="none"), "disabled": dict(fillColor="disabled", borderColor="none")}
    scrollThumb = {"normal": dict(fillColor="border", borderColor="none"), "hot": dict(fillColor="disabled", borderColor="none"),
                   "pressed": dict(fillColor="muted", borderColor="none"), "disabled": dict(fillColor="slot", borderColor="none")}
    scrollTrack = {"normal": dict(fillColor="bgSurface", borderColor="none")}
    arrow = {"normal": dict(fillColor="none", borderColor="none", textColor="muted"),
             "hot": dict(fillColor="hot", borderColor="none", textColor="text"),
             "pressed": dict(fillColor="press", borderColor="none", textColor="text"),
             "disabled": dict(fillColor="none", borderColor="none", textColor="disabled")}
    spin = {"normal": dict(fillColor="slot", borderColor="border", textColor="text"),
            "hot": dict(fillColor="hot", borderColor="muted", textColor="text"),
            "pressed": dict(fillColor="press", borderColor="muted", textColor="text"),
            "disabled": dict(fillColor="bgSurface", borderColor="slot", textColor="disabled")}
    tab = {"normal": dict(fillColor="bgApp", borderColor="border"), "hot": dict(fillColor="hot", borderColor="border"),
           "selected": dict(fillColor="bgSurface", borderColor="accent"), "hotselected": dict(fillColor="bgSurface", borderColor="accent"),
           "disabled": dict(fillColor="bgApp", borderColor="slot")}
    edit = {"normal": dict(fillColor="bgSurface", borderColor="border"), "hot": dict(fillColor="bgSurface", borderColor="muted"),
            "focused": dict(fillColor="bgSurface", borderColor="accent"), "readonly": dict(fillColor="bgApp", borderColor="border"),
            "disabled": dict(fillColor="bgApp", borderColor="slot", textColor="disabled")}
    item = {"normal": dict(none), "hot": dict(fillColor="hot", borderColor="none"), "selected": dict(fillColor="accentDeep", borderColor="none"),
            "hotselected": dict(fillColor="accentDeep", borderColor="none"), "disabled": dict(none)}
    glyph = {"normal": dict(fillColor="none", borderColor="none", textColor="muted")}
    header = {"normal": dict(fillColor="slot", borderColor="border"), "hot": dict(fillColor="hot", borderColor="border"),
              "pressed": dict(fillColor="press", borderColor="border")}
    menuItem = {"normal": dict(none, textColor="text"), "hot": dict(fillColor="hot", borderColor="none", textColor="text"),
                "pressed": dict(fillColor="press", borderColor="none", textColor="text"), "disabled": dict(none, textColor="disabled")}
    tbar = {"normal": dict(fillColor="none", borderColor="none", textColor="text"), "hot": dict(fillColor="hot", borderColor="none", textColor="text"),
            "pressed": dict(fillColor="press", borderColor="none", textColor="text"), "disabled": dict(fillColor="none", borderColor="none", textColor="disabled")}
    tics = {"normal": dict(fillColor="none", borderColor="none")}
    out = {("BUTTON", "BP_GROUPBOX"): {"normal": dict(fillColor="none", borderColor="border", textColor="text"),
                                       "disabled": dict(fillColor="none", borderColor="slot", textColor="disabled")},
           ("TOOLBAR", "TP_SPLITBUTTON"): tbar, ("TOOLBAR", "TP_SPLITBUTTONDROPDOWN"): tbar,
           ("TRACKBAR", "TKP_TICS"): tics, ("TRACKBAR", "TKP_TICSVERT"): tics,
           ("STATUS", "SP_PANE"): {"normal": dict(fillColor="bgSurface", borderColor="none")},
           ("REBAR", "RP_BAND"): {"normal": dict(fillColor="bgApp", borderColor="none")},
           ("REBAR", "RP_CHEVRON"): arrow, ("REBAR", "RP_CHEVRONVERT"): arrow,
           ("TRACKBAR", "TKP_TRACK"): groove, ("TRACKBAR", "TKP_TRACKVERT"): groove,
           ("TRACKBAR", "TKP_THUMB"): thumb, ("TRACKBAR", "TKP_THUMBVERT"): thumb,
           ("SCROLLBAR", "SBP_THUMBBTNHORZ"): scrollThumb, ("SCROLLBAR", "SBP_THUMBBTNVERT"): scrollThumb,
           ("SCROLLBAR", "SBP_ARROWBTN"): arrow,
           ("PROGRESS", "PP_BAR"): groove, ("PROGRESS", "PP_BARVERT"): groove,
           ("PROGRESS", "PP_FILL"): {"normal": dict(fillColor="accent", borderColor="none")},
           ("PROGRESS", "PP_FILLVERT"): {"normal": dict(fillColor="accent", borderColor="none")},
           ("TAB", "TABP_PANE"): {"normal": dict(fillColor="bgSurface", borderColor="border")},
           ("EDIT", "EP_EDITTEXT"): edit,
           ("LISTVIEW", "LVP_LISTITEM"): item, ("TREEVIEW", "TVP_TREEITEM"): item, ("TREEVIEW", "TVP_GLYPH"): glyph,
           ("HEADER", "HP_HEADERITEM"): header, ("HEADER", "HP_HEADERSORTARROW"): glyph,
           ("SPIN", "SPNP_UP"): spin, ("SPIN", "SPNP_DOWN"): spin,
           ("MENU", "MENU_BARITEM"): menuItem, ("MENU", "MENU_BARBACKGROUND"): {"normal": dict(fillColor="bgApp", borderColor="none")},
           ("TOOLTIP", "TTP_STANDARD"): {"normal": dict(fillColor="bgSurface", borderColor="border", textColor="text")}}
    for part in ("SBP_LOWERTRACKHORZ", "SBP_UPPERTRACKHORZ", "SBP_LOWERTRACKVERT", "SBP_UPPERTRACKVERT"):
        out[("SCROLLBAR", part)] = scrollTrack
    for part in ("TABP_TABITEM", "TABP_TABITEMLEFTEDGE", "TABP_TABITEMRIGHTEDGE", "TABP_TABITEMBOTHEDGE", "TABP_TOPTABITEM",
                 "TABP_TOPTABITEMLEFTEDGE", "TABP_TOPTABITEMRIGHTEDGE", "TABP_TOPTABITEMBOTHEDGE"):
        out[("TAB", part)] = tab
    return out


def statesForFamilies(data, tokens):
    for (cls, part), kinds in families().items():
        for state, entry in data["parts"].get(cls, {}).get(part, {}).items():
            named = kinds.get(kindOf(state), kinds["normal"])
            entry.setdefault("colors", {}).update(colours(tokens, **named))
    # a stray hand-picked colour for a partly mapped file
    fill = data["parts"].get("PROGRESS", {})
    for part, red in (("PP_FILL", "e5484d"), ("PP_FILLVERT", "e5484d")):
        for state, entry in fill.get(part, {}).items():
            if state.endswith("ERROR") or "_ERROR" in state:
                entry["colors"].update(colours(tokens, fillColor=red))
            elif "PAUSED" in state:
                entry["colors"].update(colours(tokens, fillColor="muted"))


def recolour(name, tokens, mapping):
    with open(os.path.join(SOURCE, name + ".theme"), encoding="utf-8") as f:
        text = f.read()
    unmapped = set()

    def swap(match):
        key = match.group(1).lower()
        if key not in mapping:
            unmapped.add(key)
            return match.group(0)
        return '"#%sff"' % tokens[mapping[key]].lower()

    text = re.sub(r'"#([0-9a-fA-F]{6})ff"', swap, text)
    data = json.loads(text)
    states(data, tokens)
    statesForFamilies(data, tokens)
    for role, token in ROLES.items():
        data["roles"][role] = "#%sff" % tokens[token].lower()
    os.makedirs(OUTPUT, exist_ok=True)
    with open(os.path.join(OUTPUT, name + ".theme"), "w", encoding="utf-8") as f:
        json.dump(data, f, indent=4)
    if unmapped:
        print("%s: unmapped colours left as they were: %s" % (name, ", ".join(sorted(unmapped))))
    print("wrote %s.theme" % name)


recolour("dark", DARK, DARK_MAP)
recolour("light", LIGHT, LIGHT_MAP)
sys.exit(0)
