#pragma once

#include <newui/font.h>

#include <string>
#include <vector>

namespace curvy {

    // The installed faces that look like monospace fonts (by name: Mono, Code, Console, Consolas, Courier, Hack, ... minus italics and
    // Monotype's script faces), sorted and without duplicates. newui lists every weight as a face of its own, named after its font file
    // ("JetBrainsMono-Medium"), so a heavier or lighter weight is a different entry.
    std::vector<std::string> installedMonospaceFonts();

    // What "Automatic" means: the first installed of a list of sturdy faces (the system default, Cascadia Mono, draws very thin strokes),
    // ending with Consolas, which ships with Windows.
    std::string automaticEditorFont();

    // The font for the script editor: `chosen` when it is installed, else the automatic one.
    newui::Font scriptEditorFont(const std::string& chosen, int size);

}
