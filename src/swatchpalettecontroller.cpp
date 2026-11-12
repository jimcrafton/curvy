#include "curvy/swatchpalettecontroller.h"

#include <stdexcept>

namespace curvy {

    SwatchPaletteController::SwatchPaletteController(SketchDocument& document, SwatchStrip& strip) : document_(document), strip_(strip) {
        document_.onPalettesChanged = [this]() { refresh(); };

        strip_.onSelectPalette = [this](std::size_t index) {
            guarded([&]() {
                recent_ = index == 0;
                if (!recent_) {
                    document_.setActivePalette(index - 1);
                }
                refresh();
            });
        };
        strip_.onUse = [this](std::size_t index) {
            const std::vector<Swatch>& list = shown();
            if (index < list.size() && onUse) {
                onUse(list[index]);
            }
        };
        strip_.onAdd = [this]() {
            guarded([&]() {
                if (!recent_) {
                    Swatch added = current_;
                    added.name.clear();
                    document_.addSwatch(document_.activePalette(), added);
                }
            });
        };
        strip_.onReplace = [this](std::size_t index) {
            guarded([&]() {
                Swatch replacement = current_;
                replacement.name.clear();
                document_.replaceSwatch(document_.activePalette(), index, replacement);
            });
        };
        strip_.onSetLinked = [this](std::size_t index, bool linked) {
            guarded([&]() {
                document_.setSwatchLinked(document_.activePalette(), index, linked);
                say(linked ? "Linked: using it on a colour ties the colour to the swatch" : "Unlinked: colours keep their values");
            });
        };
        strip_.onRenameSwatch =[this](std::size_t index, const std::string& name) {
            guarded([&]() { document_.renameSwatch(document_.activePalette(), index, name); });
        };
        strip_.onDeleteSwatch = [this](std::size_t index) {
            guarded([&]() { document_.deleteSwatch(document_.activePalette(), index); });
        };
        strip_.onMove = [this](std::size_t from, std::size_t to) {
            guarded([&]() { document_.moveSwatch(document_.activePalette(), from, to); });
        };
        strip_.onSaveToPalette = [this](std::size_t index) {
            guarded([&]() {
                const std::vector<Swatch>& recent = document_.recentColors();
                if (index < recent.size()) {
                    document_.addSwatch(document_.activePalette(), recent[index]);
                    say("Saved to the palette " + document_.palettes()[document_.activePalette()].name);
                }
            });
        };
        strip_.onNewPalette = [this]() {
            guarded([&]() {
                const std::size_t added = document_.addPalette("Palette");
                recent_ = false;
                document_.setActivePalette(added);
            });
        };
        strip_.onDuplicatePalette = [this]() {
            guarded([&]() {
                const std::size_t copy = document_.duplicatePalette(document_.activePalette());
                recent_ = false;
                document_.setActivePalette(copy);
            });
        };
        strip_.onRenamePalette = [this](const std::string& name) {
            guarded([&]() { document_.renamePalette(document_.activePalette(), name); });
        };
        strip_.onDeletePalette = [this]() {
            guarded([&]() { document_.deletePalette(document_.activePalette()); });
        };
        refresh();
    }

    SwatchPaletteController::~SwatchPaletteController() {
        document_.onPalettesChanged = nullptr;
    }

    void SwatchPaletteController::guarded(const std::function<void()>& edit) {
        try {
            edit();
        } catch (const std::runtime_error& problem) {
            say(problem.what());
            refresh();
        }
    }

    void SwatchPaletteController::say(const std::string& message) {
        if (onMessage) {
            onMessage(message);
        }
    }

    const std::vector<Swatch>& SwatchPaletteController::shown() const {
        return recent_ ? document_.recentColors() : document_.palettes()[document_.activePalette()].swatches;
    }

    void SwatchPaletteController::setCurrent(const Swatch& color) {
        current_ = color;
        strip_.setCurrent(color.r, color.g, color.b, color.a);
    }

    void SwatchPaletteController::refresh() {
        if (refreshing_) {
            return;
        }
        refreshing_ = true;
        std::vector<StripPalette> palettes;
        palettes.push_back(StripPalette{"Recent", true});
        for (const Palette& palette : document_.palettes()) {
            palettes.push_back(StripPalette{palette.name, false});
        }
        strip_.setPalettes(std::move(palettes), recent_ ? 0 : document_.activePalette() + 1);
        std::vector<StripSwatch> swatches;
        for (const Swatch& swatch : shown()) {
            swatches.push_back(StripSwatch{swatch.r, swatch.g, swatch.b, swatch.a, swatch.name, swatch.link != 0});
        }
        strip_.setSwatches(std::move(swatches));
        strip_.setCurrent(current_.r, current_.g, current_.b, current_.a);
        refreshing_ = false;
    }

}
