#include "curvy/swatchstrip.h"

#include "curvy/graphpanel.h"
#include "curvy/theme.h"

#include <newui/menus.h>
#include <newui/mouse_constants.h>
#include <newui/rootview.h>
#include <newui/viewstyle.h>

#include <windows.h>

#include <algorithm>
#include <cmath>

namespace curvy {

    using namespace newui;

    // ---- SwatchTiles ----

    SwatchTiles::SwatchTiles() {
        setName("SwatchTiles");
        setVisible(true);
        onMouseDown.add(this, &SwatchTiles::handleMouseDown);
        onMouseMove.add(this, &SwatchTiles::handleMouseMove);
        onMouseUp.add(this, &SwatchTiles::handleMouseUp);
        onMouseLeft.add(this, &SwatchTiles::handleMouseLeft);
    }

    void SwatchTiles::setSwatches(std::vector<StripSwatch> swatches) {
        swatches_ = std::move(swatches);
        hover_ = kNone;
        pressed_ = kNone;
        dragging_ = false;
        style().markDirty();
    }

    void SwatchTiles::setCurrent(float r, float g, float b, float a) {
        current_.r = r;
        current_.g = g;
        current_.b = b;
        current_.a = a;
        style().markDirty();
    }

    void SwatchTiles::setEditable(bool editable) {
        editable_ = editable;
        style().markDirty();
    }

    std::size_t SwatchTiles::columnsFor(float width) const {
        return (std::max)(std::size_t(1), std::size_t((std::max)(0.0f, std::floor((width + kGap) / (kTile + kGap)))));
    }

    float SwatchTiles::heightFor(float width) const {
        const std::size_t columns = columnsFor(width);
        const std::size_t rows = (tileCount() + columns - 1) / columns;
        return float(rows) * kTile + float(rows - 1) * kGap;
    }

    int SwatchTiles::tileAt(const Point& pt) const {
        const Rect client = getClientBounds();
        const float stride = kTile + kGap;
        const float x = pt.x - client.left(), y = pt.y - client.top();
        if (x < 0.0f || y < 0.0f) {
            return kNone;
        }
        const std::size_t columns = columnsFor(client.width());
        const std::size_t column = std::size_t(x / stride), row = std::size_t(y / stride);
        if (column >= columns) {
            return kNone;
        }
        const std::size_t index = row * columns + column;
        return index < tileCount() ? int(index) : kNone;
    }

    Point SwatchTiles::tileCenter(std::size_t index) const {
        const Rect client = getClientBounds();
        const std::size_t columns = columnsFor(client.width());
        const float stride = kTile + kGap;
        return Point(client.left() + float(index % columns) * stride + kTile * 0.5f, client.top() + float(index / columns) * stride + kTile * 0.5f);
    }

    void SwatchTiles::setHover(int index) {
        const int shown = index >= 0 && std::size_t(index) < swatches_.size() ? index : kNone;
        if (shown != hover_) {
            hover_ = shown;
            style().markDirty();
            if (onHover) {
                onHover(hover_);
            }
        }
    }

    void SwatchTiles::pressTile(int index, bool right, const Point& pt) {
        pressed_ = kNone;
        dragging_ = false;
        if (index == kNone) {
            return;
        }
        if (right) {
            if (std::size_t(index) < swatches_.size() && onMenu) {
                onMenu(std::size_t(index));
            }
            return;
        }
        pressed_ = index;
        pressPoint_ = pt;
    }

    void SwatchTiles::moveTo(const Point& pt, bool buttonDown) {
        if (!buttonDown) {
            pressed_ = kNone;
            dragging_ = false;
            setHover(tileAt(pt));
            return;
        }
        if (pressed_ == kNone || std::size_t(pressed_) >= swatches_.size() || !editable_) {
            return;
        }
        if (!dragging_ && std::hypot(pt.x - pressPoint_.x, pt.y - pressPoint_.y) > 4.0f) {
            dragging_ = true;
        }
        if (dragging_) {
            int target = tileAt(pt);
            if (target != kNone && std::size_t(target) >= swatches_.size()) {
                target = int(swatches_.size()) - 1;   // over the "+" tile: the end
            }
            dropTarget_ = target;
            style().markDirty();
        }
    }

    void SwatchTiles::releaseAt(const Point& pt) {
        const int pressed = pressed_;
        const bool dragged = dragging_;
        pressed_ = kNone;
        dragging_ = false;
        style().markDirty();
        if (pressed == kNone) {
            return;
        }
        int target = tileAt(pt);
        if (dragged) {
            if (target != kNone && std::size_t(target) >= swatches_.size()) {
                target = int(swatches_.size()) - 1;
            }
            if (target != kNone && target != pressed && onMove) {
                onMove(std::size_t(pressed), std::size_t(target));
            }
            return;
        }
        if (target != pressed) {
            return;
        }
        if (std::size_t(pressed) < swatches_.size()) {
            if (onUse) {
                onUse(std::size_t(pressed));
            }
        } else if (editable_ && onAdd) {
            onAdd();
        }
    }

    SyncReturn SwatchTiles::handleMouseDown(View&, const Point& pt, std::uint32_t buttons, std::uint32_t) {
        const bool right = (buttons & mbmRightButton) != 0;
        if (!right && (buttons & mbmLeftButton) == 0) {
            return SyncReturn::Ignored;
        }
        pressTile(tileAt(pt), right, pt);
        return SyncReturn::Handled;
    }

    SyncReturn SwatchTiles::handleMouseMove(View&, const Point& pt, std::uint32_t buttons, std::uint32_t) {
        moveTo(pt, (buttons & mbmLeftButton) != 0);
        return SyncReturn::Handled;
    }

    SyncReturn SwatchTiles::handleMouseUp(View&, const Point& pt, std::uint32_t buttons, std::uint32_t) {
        if ((buttons & mbmLeftButton) == 0 && pressed_ == kNone) {
            return SyncReturn::Ignored;
        }
        releaseAt(pt);
        return SyncReturn::Handled;
    }

    SyncReturn SwatchTiles::handleMouseLeft(View&, const Point&, std::uint32_t, std::uint32_t) {
        if (!dragging_) {
            setHover(kNone);
        }
        return SyncReturn::Handled;
    }

    void SwatchTiles::paint(BLContext& ctx) {
        const Theme& theme = Theme::current();
        const Rect client = getClientBounds();
        const std::size_t columns = columnsFor(client.width());
        const float stride = kTile + kGap;
        const auto byte = [](float v) { return std::uint32_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
        ctx.save();
        for (std::size_t i = 0; i < tileCount(); ++i) {
            const double x = double(client.left()) + double(i % columns) * double(stride), y = double(client.top()) + double(i / columns) * double(stride);
            const BLRect box(x, y, double(kTile), double(kTile));
            if (i == swatches_.size()) {   // the "+" tile
                ctx.set_fill_style(BLRgba32(Theme::argb(theme.slot, editable_ ? 255 : 90)));
                ctx.fill_rect(box);
                ctx.set_stroke_style(BLRgba32(Theme::argb(theme.textMuted, editable_ ? 255 : 90)));
                ctx.set_stroke_width(1.0);
                ctx.stroke_rect(BLRect(x + 0.5, y + 0.5, double(kTile) - 1.0, double(kTile) - 1.0));
                ctx.stroke_line(BLLine(x + 5.0, y + double(kTile) * 0.5, x + double(kTile) - 5.0, y + double(kTile) * 0.5));
                ctx.stroke_line(BLLine(x + double(kTile) * 0.5, y + 5.0, x + double(kTile) * 0.5, y + double(kTile) - 5.0));
                continue;
            }
            const StripSwatch& swatch = swatches_[i];
            if (swatch.a < 1.0f) {   // a checker under a see-through colour
                ctx.set_fill_style(BLRgba32(0xFFFFFFFFu));
                ctx.fill_rect(box);
                ctx.set_fill_style(BLRgba32(0xFFBBBBBBu));
                ctx.fill_rect(BLRect(x, y, double(kTile) * 0.5, double(kTile) * 0.5));
                ctx.fill_rect(BLRect(x + double(kTile) * 0.5, y + double(kTile) * 0.5, double(kTile) * 0.5, double(kTile) * 0.5));
            }
            const bool lifted = dragging_ && int(i) == pressed_;
            ctx.set_fill_style(BLRgba32((byte(swatch.a * (lifted ? 0.35f : 1.0f)) << 24) | (byte(swatch.r) << 16) | (byte(swatch.g) << 8) | byte(swatch.b)));
            ctx.fill_rect(box);
            ctx.set_stroke_style(BLRgba32(0x80000000u));
            ctx.set_stroke_width(1.0);
            ctx.stroke_rect(BLRect(x + 0.5, y + 0.5, double(kTile) - 1.0, double(kTile) - 1.0));
            if (swatch.linked) {   // the corner triangle that marks a global swatch
                BLPath corner;
                corner.move_to(x + double(kTile) - 7.0, y);
                corner.line_to(x + double(kTile), y);
                corner.line_to(x + double(kTile), y + 7.0);
                corner.close();
                ctx.set_fill_style(BLRgba32(0xFFFFFFFFu));
                ctx.fill_path(corner);
                ctx.set_stroke_style(BLRgba32(0xB0000000u));
                ctx.set_stroke_width(1.0);
                ctx.stroke_path(corner);
            }
            const bool isCurrent = byte(swatch.r) == byte(current_.r) && byte(swatch.g) == byte(current_.g) && byte(swatch.b) == byte(current_.b) && byte(swatch.a) == byte(current_.a);
            if (isCurrent || int(i) == hover_) {
                ctx.set_stroke_style(BLRgba32(Theme::argb(isCurrent ? theme.accent : theme.textPrimary)));
                ctx.set_stroke_width(2.0);
                ctx.stroke_rect(BLRect(x - 1.0, y - 1.0, double(kTile) + 2.0, double(kTile) + 2.0));
            }
        }
        if (dragging_ && dropTarget_ != kNone && dropTarget_ != pressed_) {   // where the lifted swatch will land
            const double x = double(client.left()) + double(std::size_t(dropTarget_) % columns) * double(stride) - (dropTarget_ > pressed_ ? -double(kTile) - 1.5 : 1.5);
            const double y = double(client.top()) + double(std::size_t(dropTarget_) / columns) * double(stride);
            ctx.set_fill_style(BLRgba32(Theme::argb(theme.accent)));
            ctx.fill_rect(BLRect(x - 1.0, y - 1.0, 2.0, double(kTile) + 2.0));
        }
        ctx.restore();
    }

    // ---- SwatchStrip ----

    namespace {
        constexpr float kHeadHeight = 22.0f, kHeadGap = 6.0f, kFootHeight = 16.0f, kBarGap = 4.0f, kBarPad = 6.0f, kMoreWidth = 24.0f, kArrowWidth = 18.0f;
    }

    DropArrow::DropArrow() {
        setName("DropArrow");
        setVisible(true);
    }

    void DropArrow::paint(BLContext& ctx) {
        const Rect client = getClientBounds();
        const double cx = double(client.left()) + double(client.width()) * 0.5, cy = double(client.top()) + double(client.height()) * 0.5;
        ctx.set_fill_style(BLRgba32(Theme::argb(Theme::current().textPrimary)));
        BLPath triangle;
        triangle.move_to(cx - 4.0, cy - 2.0);
        triangle.line_to(cx + 4.0, cy - 2.0);
        triangle.line_to(cx, cy + 3.0);
        triangle.close();
        ctx.fill_path(triangle);
    }

    SwatchStrip::SwatchStrip() {
        setName("SwatchStrip");
        setVisible(true);
        const Theme& theme = Theme::current();

        paletteButton_ = new Button();
        addChild(paletteButton_);
        paletteButton_->onClick += [this](Control&) {
            showMenu(paletteListMenu());
            return SyncReturn::Handled;
        };
        arrow_ = new DropArrow();   // on top of the button, so a press on it opens the list too
        addChild(arrow_);
        arrow_->onMouseDown += [this](View&, const Point&, std::uint32_t buttons, std::uint32_t) {
            if ((buttons & mbmLeftButton) == 0) {
                return SyncReturn(SyncReturn::Ignored);
            }
            showMenu(paletteListMenu());
            return SyncReturn(SyncReturn::Handled);
        };
        moreButton_ = new Button();
        moreButton_->setText("...");
        addChild(moreButton_);
        moreButton_->onClick += [this](Control&) {
            showMenu(paletteMoreMenu());
            return SyncReturn::Handled;
        };

        tiles_ = new SwatchTiles();
        addChild(tiles_);
        tiles_->onHover = [this](int index) { setHoverText(index); };
        tiles_->onUse = [this](std::size_t index) {
            if (onUse) {
                onUse(index);
            }
        };
        tiles_->onAdd = [this]() {
            if (onAdd) {
                onAdd();
            }
        };
        tiles_->onMenu = [this](std::size_t index) { showMenu(swatchMenu(index)); };
        tiles_->onMove = [this](std::size_t from, std::size_t to) {
            if (onMove) {
                onMove(from, to);
            }
        };

        foot_ = new Label();
        foot_->setTextAlignment(TextAlignment::Left);
        foot_->setTextColor(BLRgba32(Theme::argb(theme.textMuted)));
        addChild(foot_);

        bar_ = new SubView();
        bar_->setVisible(false);
        bar_->style().setBackgroundColor(BLRgba32(Theme::argb(theme.slot)));
        bar_->style().setBorderFill(newui::Color(1.0f, 0.71f, 0.33f));
        bar_->style().setBorderWidth(1.0f);
        addChild(bar_);
        barText_ = new Label();
        barText_->setTextAlignment(TextAlignment::Left);
        barText_->setTextColor(BLRgba32(Theme::argb(theme.textPrimary)));
        if (auto* labelStyle = dynamic_cast<LabelStyle*>(&barText_->style())) {
            labelStyle->setWordWrap(true);
        }
        bar_->addChild(barText_);
        barField_ = new TextField();
        barField_->setTextColor(BLRgba32(Theme::argb(theme.textPrimary)));
        barField_->caret().setColor(theme.textColor());
        bar_->addChild(barField_);
        barOk_ = new Button();
        barOk_->setText("OK");
        bar_->addChild(barOk_);
        barCancel_ = new Button();
        barCancel_->setText("Cancel");
        bar_->addChild(barCancel_);
        barOk_->onClick += [this](Control&) {
            const std::wstring text = barField_->text();
            answer(true, std::string(text.begin(), text.end()));
            return SyncReturn::Handled;
        };
        barCancel_->onClick += [this](Control&) {
            answer(false);
            return SyncReturn::Handled;
        };
        barField_->onReturnPressed += [this](TextField&) {
            const std::wstring text = barField_->text();
            answer(true, std::string(text.begin(), text.end()));
            return SyncReturn::Handled;
        };

        setPalettes({}, 0);
    }

    void SwatchStrip::refreshPaletteButton() {
        paletteButton_->setText(active_ < palettes_.size() ? palettes_[active_].name : std::string("No palette"));
    }

    std::string SwatchStrip::paletteButtonText() const {
        return active_ < palettes_.size() ? palettes_[active_].name : std::string();
    }

    void SwatchStrip::setPalettes(std::vector<StripPalette> palettes, std::size_t active) {
        palettes_ = std::move(palettes);
        active_ = palettes_.empty() ? 0 : (std::min)(active, palettes_.size() - 1);
        refreshPaletteButton();
        tiles_->setEditable(!activeIsAutomatic());
        closeBar();
    }

    void SwatchStrip::setSwatches(std::vector<StripSwatch> swatches) {
        tiles_->setSwatches(std::move(swatches));
        setHoverText(SwatchTiles::kNone);
        changedHeight();
    }

    void SwatchStrip::setCurrent(float r, float g, float b, float a) {
        tiles_->setCurrent(r, g, b, a);
    }

    void SwatchStrip::setHoverText(int index) {
        const auto& swatches = tiles_->swatches();
        if (index >= 0 && std::size_t(index) < swatches.size()) {
            const StripSwatch& s = swatches[std::size_t(index)];
            const std::string hex = s.a < 1.0f ? GraphPanel::colorText(s.r, s.g, s.b, s.a) : GraphPanel::colorText(s.r, s.g, s.b);
            foot_->setText((s.name.empty() ? hex : s.name + "  " + hex) + (s.linked ? "  linked" : ""));
        } else {
            foot_->setText(std::to_string(swatches.size()) + (swatches.size() == 1 ? " colour" : " colours"));
        }
    }

    std::string SwatchStrip::hoverText() const {
        return foot_->text();
    }

    float SwatchStrip::layoutFor(float width) {
        width_ = width;
        const float buttonWidth = (std::max)(20.0f, width - kMoreWidth - 4.0f);
        paletteButton_->setBounds(Rect(0.0f, 0.0f, buttonWidth, kHeadHeight));
        arrow_->setBounds(Rect(buttonWidth - kArrowWidth - 2.0f, 0.0f, kArrowWidth, kHeadHeight));
        moreButton_->setBounds(Rect(width - kMoreWidth, 0.0f, kMoreWidth, kHeadHeight));
        float y = kHeadHeight + kHeadGap;
        const float gridHeight = tiles_->heightFor(width);
        tiles_->setBounds(Rect(0.0f, y, width, gridHeight));
        y += gridHeight + kBarGap;
        foot_->setBounds(Rect(0.0f, y, width, kFootHeight));
        y += kFootHeight;
        bar_->setVisible(asking_);
        if (asking_) {
            y += kBarGap;
            const float textHeight = askingText_ ? 16.0f : 32.0f;
            float inner = kBarPad;
            barText_->setBounds(Rect(kBarPad, inner, width - 2.0f * kBarPad, textHeight));
            inner += textHeight + 2.0f;
            barField_->setVisible(askingText_);
            if (askingText_) {
                barField_->setBounds(Rect(kBarPad, inner, width - 2.0f * kBarPad, 22.0f));
                inner += 22.0f + 4.0f;
            }
            barOk_->setBounds(Rect(kBarPad, inner, 70.0f, 24.0f));
            barCancel_->setBounds(Rect(kBarPad + 76.0f, inner, 70.0f, 24.0f));
            inner += 24.0f + kBarPad;
            bar_->setBounds(Rect(0.0f, y, width, inner));
            y += inner;
        }
        lastHeight_ = y;
        setBounds(Rect(bounds().left(), bounds().top(), width, y));
        style().markDirty();
        return y;
    }

    void SwatchStrip::changedHeight() {
        const float before = lastHeight_;
        layoutFor(width_);
        if (lastHeight_ != before && onHeightChanged) {
            onHeightChanged();
        }
    }

    void SwatchStrip::closeBar() {
        const bool was = asking_;
        asking_ = false;
        yes_ = nullptr;
        typed_ = nullptr;
        if (was) {
            changedHeight();
        }
    }

    void SwatchStrip::askConfirm(const std::string& question, std::function<void()> onYes) {
        asking_ = true;
        askingText_ = false;
        questionText_ = question;
        barText_->setText(question);
        barOk_->setText("OK");
        yes_ = std::move(onYes);
        typed_ = nullptr;
        changedHeight();
    }

    void SwatchStrip::askText(const std::string& title, const std::string& initial, std::function<void(const std::string&)> onOk) {
        asking_ = true;
        askingText_ = true;
        questionText_ = title;
        barText_->setText(title);
        barField_->setText(std::wstring(initial.begin(), initial.end()));
        barOk_->setText("OK");
        typed_ = std::move(onOk);
        yes_ = nullptr;
        changedHeight();
    }

    const std::string& SwatchStrip::question() const {
        return questionText_;
    }

    void SwatchStrip::answer(bool ok, const std::string& text) {
        if (!asking_) {
            return;
        }
        const bool typedAnswer = askingText_;
        std::function<void()> yes = std::move(yes_);
        std::function<void(const std::string&)> typed = std::move(typed_);
        closeBar();
        if (!ok) {
            return;
        }
        if (typedAnswer && typed) {
            typed(text);
        } else if (!typedAnswer && yes) {
            yes();
        }
    }

    std::vector<StripMenuEntry> SwatchStrip::paletteListMenu() const {
        std::vector<StripMenuEntry> entries;
        for (std::size_t i = 0; i < palettes_.size(); ++i) {
            StripMenuEntry entry;
            entry.text = palettes_[i].name + (palettes_[i].automatic ? " (automatic)" : "");
            entry.checked = i == active_;
            entry.run = [this, i]() {
                if (onSelectPalette) {
                    onSelectPalette(i);
                }
            };
            entries.push_back(entry);
        }
        return entries;
    }

    std::vector<StripMenuEntry> SwatchStrip::paletteMoreMenu() {
        const bool automatic = activeIsAutomatic();
        const std::size_t real = std::size_t(std::count_if(palettes_.begin(), palettes_.end(), [](const StripPalette& p) { return !p.automatic; }));
        std::vector<StripMenuEntry> entries;
        StripMenuEntry add;
        add.text = "New palette";
        add.run = [this]() {
            if (onNewPalette) {
                onNewPalette();
            }
        };
        entries.push_back(add);
        StripMenuEntry duplicate;
        duplicate.text = "Duplicate";
        duplicate.enabled = !automatic;
        duplicate.run = [this]() {
            if (onDuplicatePalette) {
                onDuplicatePalette();
            }
        };
        entries.push_back(duplicate);
        StripMenuEntry rename;
        rename.text = "Rename...";
        rename.enabled = !automatic;
        rename.run = [this]() {
            askText("Rename the palette", active_ < palettes_.size() ? palettes_[active_].name : "", [this](const std::string& name) {
                if (onRenamePalette) {
                    onRenamePalette(name);
                }
            });
        };
        entries.push_back(rename);
        StripMenuEntry line;
        line.separator = true;
        entries.push_back(line);
        StripMenuEntry remove;
        remove.text = "Delete...";
        remove.enabled = !automatic && real >= 2;
        remove.run = [this]() {
            const std::size_t count = tiles_->swatches().size();
            askConfirm("Delete the palette \"" + paletteButtonText() + "\" and its " + std::to_string(count) + (count == 1 ? " colour" : " colours") +
                           "? You can undo this.",
                       [this]() {
                           if (onDeletePalette) {
                               onDeletePalette();
                           }
                       });
        };
        entries.push_back(remove);
        return entries;
    }

    std::vector<StripMenuEntry> SwatchStrip::swatchMenu(std::size_t index) {
        std::vector<StripMenuEntry> entries;
        StripMenuEntry use;
        use.text = "Use";
        use.run = [this, index]() {
            if (onUse) {
                onUse(index);
            }
        };
        entries.push_back(use);
        if (activeIsAutomatic()) {
            StripMenuEntry save;
            save.text = "Save to palette";
            save.run = [this, index]() {
                if (onSaveToPalette) {
                    onSaveToPalette(index);
                }
            };
            entries.push_back(save);
            return entries;
        }
        StripMenuEntry replace;
        replace.text = "Replace with current colour";
        replace.run = [this, index]() {
            if (onReplace) {
                onReplace(index);
            }
        };
        entries.push_back(replace);
        StripMenuEntry link;
        const bool isLinked = index < tiles_->swatches().size() && tiles_->swatches()[index].linked;
        link.text = isLinked ? "Unlink" : "Link";
        link.run = [this, index, isLinked]() {
            if (onSetLinked) {
                onSetLinked(index, !isLinked);
            }
        };
        entries.push_back(link);
        StripMenuEntry rename;
        rename.text = "Rename...";
        rename.run = [this, index]() {
            const auto& swatches = tiles_->swatches();
            askText("Rename the swatch", index < swatches.size() ? swatches[index].name : "", [this, index](const std::string& name) {
                if (onRenameSwatch) {
                    onRenameSwatch(index, name);
                }
            });
        };
        entries.push_back(rename);
        StripMenuEntry line;
        line.separator = true;
        entries.push_back(line);
        StripMenuEntry remove;
        remove.text = "Delete";
        remove.run = [this, index]() {
            if (onDeleteSwatch) {
                onDeleteSwatch(index);
            }
        };
        entries.push_back(remove);
        return entries;
    }

    // The native popup blocks until a line is chosen or it is dismissed; the chosen line has run by the time it returns. Without a window (a test)
    // nothing is shown.
    void SwatchStrip::showMenu(const std::vector<StripMenuEntry>& entries) {
        RootView* root = rootView();
        if (root == nullptr || root->windowHandle() == nullptr) {
            return;
        }
        MenuItem menu;
        for (const StripMenuEntry& entry : entries) {
            if (entry.separator) {
                menu.addChild(MenuItem::Separator());
                continue;
            }
            MenuItem* item = menu.addChild(std::make_unique<MenuItem>(entry.text));
            item->setChecked(entry.checked);
            item->state().setEnabled(entry.enabled);
            item->onClick += [run = entry.run](MenuItem&) {
                if (run) {
                    run();
                }
                return SyncReturn::Handled;
            };
        }
        POINT at{};
        ::GetCursorPos(&at);
        ContextMenu popup;
        popup.show(root->windowHandle(), menu, at.x, at.y);
    }

}
