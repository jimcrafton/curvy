#include "curvy/paramgrid.h"

#include "curvy/graphtree.h"

#include <newui/cursor.h>
#include <newui/fontmanager.h>
#include <newui/keyboard_constants.h>
#include <newui/mouse_constants.h>
#include <newui/rootview.h>
#include <newui/utils.h>

#include <algorithm>
#include <cmath>

namespace curvy {

    using namespace newui;

    namespace {
        constexpr float kChipWidth = 28.0f, kChipHeight = 18.0f, kPercentWidth = 38.0f, kKnob = 6.0f, kDiamondHalf = 4.0f;

        BLRgba32 rgb(std::uint32_t color, std::uint32_t alpha = 0xFF) { return BLRgba32((color & 0x00FFFFFFu) | (alpha << 24)); }

        void drawText(BLContext& ctx, const std::string& text, const Rect& box, std::uint32_t color, bool right = false) {
            if (text.empty()) {
                return;
            }
            Font font = FontManager::getSystemFont(SystemUIFont::Message);
            BLFont* blFont = font.blFont();
            if (blFont == nullptr || !blFont->is_valid()) {
                return;
            }
            BLGlyphBuffer glyphs;
            glyphs.set_utf8_text(text.c_str(), text.size());
            blFont->shape(glyphs);
            BLTextMetrics metrics;
            blFont->get_text_metrics(glyphs, metrics);
            const BLFontMetrics& fm = blFont->metrics();
            const double width = metrics.advance.x;
            const double x = right ? std::max(double(box.left()), double(box.right()) - width) : double(box.left());
            ctx.save();
            ctx.clip_to_rect(BLRect(box.left(), box.top(), box.width(), box.height()));
            ctx.set_fill_style(rgb(color));
            ctx.fill_utf8_text(BLPoint(x, box.top() + box.height() * 0.5 + (fm.ascent - fm.descent) * 0.5), *blFont, text.c_str(), text.size());
            ctx.restore();
        }

        void diamondPath(BLPath& path, double cx, double cy, double half) {
            path.move_to(cx, cy - half);
            path.line_to(cx + half, cy);
            path.line_to(cx, cy + half);
            path.line_to(cx - half, cy);
            path.close();
        }
    }

    ParamGrid::ParamGrid() {
        setVisible(true);
        editor_ = new ExpressionField();
        editor_->setName("graphDetailEditor");
        editor_->setVisible(false);
        addChild(editor_);

        scrubber_.onBegin += [this](NumberScrubber&, double& value) {
            return scrubbing_ != kNone ? onScrubBegin.syncCallFirst(*this, scrubbing_, value) : SyncReturn(SyncReturn::Ignored);
        };
        scrubber_.onChange += [this](NumberScrubber&, double value) {
            if (scrubbing_ != kNone) {
                onScrubChange(*this, scrubbing_, value);
            }
            return SyncReturn::Handled;
        };
        scrubber_.onEnd += [this](NumberScrubber&, bool didChange) {
            const std::size_t row = scrubbing_;
            scrubbing_ = kNone;
            if (row != kNone) {
                onScrubEnd(*this, row, didChange);
            }
            return SyncReturn::Handled;
        };
        scrubber_.onUnit += [this](NumberScrubber&, double& unit) {
            return scrubbing_ != kNone ? onScrubUnit.syncCallFirst(*this, scrubbing_, unit) : SyncReturn(SyncReturn::Ignored);
        };

        editor_->onReturnPressed += [this](TextField&) {
            endEdit(true);
            return SyncReturn::Handled;
        };
        editor_->onLostFocus += [this](View&) {
            endEdit(true);
            return SyncReturn::Handled;
        };
        editor_->onKeyDown += [this](View&, std::uint32_t, int, int, std::uint32_t vk) {
            if (vk == vkEscape) {
                endEdit(false);
                return SyncReturn::Handled;
            }
            if (vk == vkLetterK && (::GetKeyState(VK_CONTROL) & 0x8000) != 0 && editing_ != kNone) {
                onKeyToggled(*this, editing_);
                return SyncReturn::Handled;
            }
            return SyncReturn::Ignored;
        };

        onMouseDown += [this](View&, const Point& at, std::uint32_t buttons, std::uint32_t keys) {
            if ((buttons & mbmLeftButton) == 0) {
                return SyncReturn::Ignored;
            }
            if (editing_ != kNone) {
                endEdit(true);   // a click anywhere else ends the entry (the field may not lose focus by itself)
            }
            const std::size_t index = rowAt(at.y);
            if (index == kNone) {
                return SyncReturn::Ignored;
            }
            const Row& row = rows_[index];
            if (at.x < kMargin + kCaptionWidth) {
                if (row.scrub) {
                    scrubbing_ = index;
                    if (!scrubber_.press(at.x)) {
                        scrubbing_ = kNone;
                        return SyncReturn::Ignored;
                    }
                    return SyncReturn::Handled;
                }
                return SyncReturn::Ignored;
            }
            if (row.kind == Kind::Slider) {
                sliding_ = index;
                edit(index).percent = percentAt(index, at.x);
                changed();
                return SyncReturn::Handled;
            }
            if (row.kind == Kind::Gradient || (row.kind == Kind::Colour && at.x < kMargin + kCaptionWidth + kChipWidth + 6.0f)) {
                onChipClicked(*this, index);
                return SyncReturn::Handled;
            }
            if (row.kind == Kind::Choice) {
                if (row.editable && !row.options.empty()) {
                    const auto found = std::find(row.options.begin(), row.options.end(), row.text);
                    const std::size_t current = found == row.options.end() ? 0 : std::size_t(found - row.options.begin());
                    const std::size_t count = row.options.size();
                    const std::size_t next = (keys & kmShift) != 0 ? (current + count - 1) % count : (found == row.options.end() ? 0 : (current + 1) % count);
                    const std::string name = row.options[next];   // the row may be rebuilt by the handler
                    onCommit(*this, index, name);
                }
                return SyncReturn::Handled;
            }
            if (row.keyable && diamondRect(index).contains(at)) {
                onKeyToggled(*this, index);
                return SyncReturn::Handled;
            }
            if (row.editable) {
                beginEdit(index);
                return SyncReturn::Handled;
            }
            return SyncReturn::Ignored;
        };
        onMouseMove += [this](View&, const Point& at, std::uint32_t, std::uint32_t keys) {
            if (scrubber_.active()) {
                scrubber_.move(at.x, keys, 1.0, -1e9, 1e9);
                return SyncReturn::Handled;
            }
            if (sliding_ != kNone) {
                edit(sliding_).percent = percentAt(sliding_, at.x);
                changed();
                return SyncReturn::Handled;
            }
            const std::size_t index = rowAt(at.y);
            const bool overCaption = index != kNone && at.x < kMargin + kCaptionWidth && rows_[index].scrub;
            setCursor(Cursor(overCaption ? CursorKind::SizeWE : CursorKind::Arrow));
            if (index != hover_) {
                hover_ = index;
                style().markDirty();
            }
            return SyncReturn::Ignored;
        };
        onMouseUp += [this](View&, const Point&, std::uint32_t, std::uint32_t) {
            if (scrubber_.active()) {
                scrubber_.release();
                return SyncReturn::Handled;
            }
            if (sliding_ != kNone) {
                const std::size_t index = sliding_;
                sliding_ = kNone;
                onSliderReleased(*this, index, rows_[index].percent);
                return SyncReturn::Handled;
            }
            return SyncReturn::Ignored;
        };
    }

    void ParamGrid::setPalette(const Palette& palette) {
        palette_ = palette;
        editor_->style().setBackgroundColor(Color(0xFF000000u | (palette.field & 0x00FFFFFFu), true));
        editor_->setColours(rgb(palette.text), rgb(palette.accent));
        style().markDirty();
    }

    void ParamGrid::resize(std::size_t count) {
        endEdit(false);
        rows_.assign(count, Row());
        changed();
    }

    void ParamGrid::clearRows() {
        endEdit(false);
        for (Row& row : rows_) {
            row = Row();
        }
        changed();
    }

    void ParamGrid::setGapAfter(std::size_t index, float height) {
        gapRow_ = height > 0.0f ? index : kNone;
        gap_ = height;
        changed();
    }

    void ParamGrid::changed() {
        layoutRows();
        style().markDirty();
    }

    void ParamGrid::layoutRows() {
        tops_.assign(rows_.size(), 0.0f);
        float y = kTop;
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            if (!rows_[i].shown) {
                continue;
            }
            tops_[i] = y;
            y += kRowHeight;
            if (i == gapRow_) {
                y += gap_;
            }
        }
        contentHeight_ = y;
        if (editing_ != kNone && editing_ < rows_.size()) {
            editor_->setBounds(valueRect(editing_));
        }
    }

    std::size_t ParamGrid::rowAt(float y) const {
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            if (rows_[i].shown && y >= tops_[i] && y < tops_[i] + kControlHeight + 2.0f) {
                return i;
            }
        }
        return kNone;
    }

    Rect ParamGrid::valueRect(std::size_t index) const {
        const float left = kMargin + kCaptionWidth;
        const float width = std::max(40.0f, bounds().size().width - left - kMargin);
        const float top = tops_[index];
        const Row& row = rows_[index];
        if (row.kind == Kind::Colour) {
            return Rect(left + kChipWidth + 6.0f, top, std::max(40.0f, width - kChipWidth - 6.0f), kControlHeight);
        }
        return Rect(left, top, width, kControlHeight);
    }

    Rect ParamGrid::diamondRect(std::size_t index) const {
        const Rect value = valueRect(index);
        return Rect(value.right() - 20.0f, value.top(), 20.0f, kControlHeight);
    }

    Rect ParamGrid::sliderTrack(std::size_t index) const {
        const float left = kMargin + kCaptionWidth;
        const float right = bounds().size().width - kMargin - kPercentWidth - 4.0f;
        return Rect(left, tops_[index], std::max(20.0f, right - left), kControlHeight);
    }

    double ParamGrid::percentAt(std::size_t index, float x) const {
        const Rect track = sliderTrack(index);
        const float usable = std::max(1.0f, track.width() - 2.0f * kKnob);
        return std::clamp(double((x - track.left() - kKnob) / usable) * 100.0, 0.0, 100.0);
    }

    bool ParamGrid::beginEdit(std::size_t index) {
        if (index >= rows_.size() || !rows_[index].shown || !rows_[index].editable || (rows_[index].kind != Kind::Text && rows_[index].kind != Kind::Colour)) {
            return false;
        }
        endEdit(true);
        const Row& row = rows_[index];
        editing_ = index;
        editor_->setBounds(valueRect(index));
        editor_->setExpressionMode(row.expression);
        editor_->setKeyState(row.key);
        editor_->setText(utf8ToWide(row.text));
        editor_->setVisible(true);
        if (RootView* root = rootView()) {
            root->setFocusedSubView(editor_);
        }
        editor_->controller().selectAll();
        style().markDirty();
        return true;
    }

    void ParamGrid::endEdit(bool commit) {
        if (editing_ == kNone) {
            return;
        }
        const std::size_t index = editing_;
        const std::string text = wideToUtf8(editor_->text());
        RootView* root = rootView();
        const bool hadFocus = root != nullptr && root->focusedSubView() == editor_;
        editing_ = kNone;
        editor_->setVisible(false);
        style().markDirty();
        if (commit) {
            onCommit(*this, index, text);
        }
        if (hadFocus) {
            onEditEnded(*this);   // otherwise Ctrl+Z would go to the hidden field
        }
    }

    void ParamGrid::paint(BLContext& ctx) {
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            const Row& row = rows_[i];
            if (!row.shown) {
                continue;
            }
            const float top = tops_[i];
            drawText(ctx, row.caption, Rect(kMargin, top, kCaptionWidth - 4.0f, kControlHeight), palette_.caption);
            if (row.kind == Kind::Gradient) {
                paintGradientChip(ctx, kMargin + kCaptionWidth, top + 1.0f, valueRect(i).width(), kChipHeight, row.stops, row.radial);
                continue;
            }
            if (row.kind == Kind::Slider) {
                const Rect track = sliderTrack(i);
                const double mid = track.top() + track.height() * 0.5;
                const double left = track.left() + kKnob, right = track.right() - kKnob;
                const double at = left + (right - left) * std::clamp(row.percent, 0.0, 100.0) / 100.0;
                ctx.save();
                ctx.fill_round_rect(BLRoundRect(left, mid - 2.0, right - left, 4.0, 2.0), rgb(palette_.field));
                ctx.fill_round_rect(BLRoundRect(left, mid - 2.0, at - left, 4.0, 2.0), rgb(palette_.accent));
                ctx.fill_circle(BLCircle(at, mid, kKnob - 1.0), rgb(palette_.text));
                ctx.restore();
                char percent[16];
                std::snprintf(percent, sizeof(percent), "%d%%", int(std::lround(row.percent)));
                drawText(ctx, percent, Rect(bounds().size().width - kMargin - kPercentWidth, top, kPercentWidth, kControlHeight), palette_.text, true);
                continue;
            }
            if (row.kind == Kind::Colour) {
                paintColourChip(ctx, kMargin + kCaptionWidth, top + 1.0f, kChipWidth, kChipHeight, row.chipColor, row.chipAlpha);
            }
            const Rect value = valueRect(i);
            ctx.fill_rect(BLRect(value.left(), value.top(), value.width(), value.height()), rgb(palette_.field));
            if (i == editing_) {
                continue;   // the editor is drawn over it
            }
            drawText(ctx, row.text, Rect(value.left() + 4.0f, value.top(), value.width() - 26.0f, value.height()),
                     row.editable ? (row.expression ? palette_.accent : palette_.text) : palette_.caption);
            if (row.expression) {
                ctx.fill_rect(BLRect(value.left(), value.top(), 2.0, value.height()), rgb(palette_.accent));
            }
            if (row.kind == Kind::Choice && row.editable) {
                BLPath chevron;
                const double cx = value.right() - 10.0, cy = value.top() + value.height() * 0.5;
                chevron.move_to(cx - 4.0, cy - 2.0);
                chevron.line_to(cx + 4.0, cy - 2.0);
                chevron.line_to(cx, cy + 3.0);
                chevron.close();
                ctx.fill_path(chevron, rgb(palette_.caption));
            }
            if (row.key != ExpressionField::KeyState::None || (row.keyable && i == hover_)) {
                BLPath diamond;
                diamondPath(diamond, value.right() - 10.0, value.top() + value.height() * 0.5, kDiamondHalf);
                if (row.key == ExpressionField::KeyState::OnKey) {
                    ctx.fill_path(diamond, rgb(palette_.accent));
                } else {
                    ctx.set_stroke_width(1.2);
                    ctx.stroke_path(diamond, rgb(palette_.accent, row.key == ExpressionField::KeyState::None ? 0x66 : 0xFF));
                }
            }
        }
    }

}
