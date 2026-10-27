#include "curvy/canvasview.h"
#include "curvy/textoutline.h"

#include <newui/keyboard_constants.h>

#include <algorithm>
#include <cmath>

// The Text tool: click to start text (or click existing text to edit it), type, Esc or click elsewhere to finish.
// Everything is drawn and edited right on the canvas - a caret and a thin box, no separate editor window.

namespace curvy {

    using namespace newui;

    namespace {
        constexpr double kMinTextSize = 4.0;
        constexpr double kMaxTextSize = 600.0;

        void putText(SketchDocument& document, std::size_t id, const std::string& text, const std::string& font, double size, const BLMatrix2D& matrix) {
            const SketchObject* object = document.findById(id);
            if (object == nullptr) {
                return;
            }
            TextState state = SketchDocument::textStateOf(*object);
            state.text = text;
            state.font = font;
            state.size = size;
            state.matrix = matrix;
            document.setObjectText(id, state);
        }

        // UTF-8 of one Unicode code point.
        std::string utf8Of(unsigned long code) {
            std::string out;
            if (code < 0x80) {
                out += char(code);
            } else if (code < 0x800) {
                out += char(0xC0 | (code >> 6));
                out += char(0x80 | (code & 0x3F));
            } else if (code < 0x10000) {
                out += char(0xE0 | (code >> 12));
                out += char(0x80 | ((code >> 6) & 0x3F));
                out += char(0x80 | (code & 0x3F));
            } else {
                out += char(0xF0 | (code >> 18));
                out += char(0x80 | ((code >> 12) & 0x3F));
                out += char(0x80 | ((code >> 6) & 0x3F));
                out += char(0x80 | (code & 0x3F));
            }
            return out;
        }

        // Byte index of the character before / after the one at `index` (UTF-8 continuation bytes start with 10).
        std::size_t previousIndex(const std::string& s, std::size_t index) {
            if (index == 0) {
                return 0;
            }
            --index;
            while (index > 0 && (static_cast<unsigned char>(s[index]) & 0xC0) == 0x80) {
                --index;
            }
            return index;
        }

        std::size_t nextIndex(const std::string& s, std::size_t index) {
            if (index >= s.size()) {
                return s.size();
            }
            ++index;
            while (index < s.size() && (static_cast<unsigned char>(s[index]) & 0xC0) == 0x80) {
                ++index;
            }
            return index;
        }

        // The start of the line `index` is on, and the end of it (the position of its '\n' or the end of the text).
        std::size_t lineStart(const std::string& s, std::size_t index) {
            std::size_t at = s.rfind('\n', index == 0 ? std::string::npos : index - 1);
            return index == 0 || at == std::string::npos ? 0 : at + 1;
        }

        std::size_t lineEnd(const std::string& s, std::size_t index) {
            std::size_t at = s.find('\n', index);
            return at == std::string::npos ? s.size() : at;
        }

        std::size_t characterCount(const std::string& s, std::size_t from, std::size_t to) {
            std::size_t count = 0;
            for (std::size_t i = from; i < to; i = nextIndex(s, i)) {
                ++count;
            }
            return count;
        }

        bool blank(const std::string& s) {
            return s.find_first_not_of(" \t\r\n") == std::string::npos;
        }
    }

    // ---- finding and starting text ----

    SketchObject* CanvasView::textObjectAt(const BLPoint& doc) const {
        if (document_ == nullptr) {
            return nullptr;
        }
        double pad = 3.0 / double(zoom_);
        for (std::size_t i = document_->objectCount(); i-- > 0;) {
            SketchObject& object = document_->object(i);
            if (!object.isText() || !object.isVisible() || object.isLocked()) {
                continue;
            }
            BLBox box = object.geometryBounds();
            if (doc.x >= box.x0 - pad && doc.x <= box.x1 + pad && doc.y >= box.y0 - pad && doc.y <= box.y1 + pad) {
                return &object;
            }
        }
        return nullptr;
    }

    SketchObject* CanvasView::textObject() const {
        if (!textEditing_) {
            return nullptr;
        }
        return textId_ != 0 && document_ != nullptr ? document_->findById(textId_) : textDraft_.get();
    }

    // The byte position in the text nearest to the document point `doc` (the click that puts the caret).
    std::size_t CanvasView::textIndexAt(const BLPoint& doc) const {
        const SketchObject* object = textObject();
        if (object == nullptr) {
            return 0;
        }
        BLMatrix2D inverse = BLMatrix2D::make_identity();
        if (object->textWarp().active() || BLMatrix2D::invert(inverse, object->textMatrix()) != BL_SUCCESS) {   // (a click in warped text puts the caret at the end)
            return textBuffer_.size();
        }
        BLPoint local = inverse.map_point(doc);
        double size = object->fontSize();
        double lineHeight = textLineHeight(object->fontName(), size, object->textStyle());
        long line = std::max(0L, long(std::floor((local.y + 0.8 * size) / lineHeight)));
        std::size_t start = 0;
        for (long i = 0; i < line; ++i) {
            std::size_t end = textBuffer_.find('\n', start);
            if (end == std::string::npos) {
                line = i;
                break;
            }
            start = end + 1;
        }
        std::size_t end = lineEnd(textBuffer_, start);
        // The nearest gap between two characters of that line.
        std::size_t best = start;
        double bestDistance = 1e300;
        for (std::size_t i = start;; i = nextIndex(textBuffer_, i)) {
            double x = textLineOffset(textBuffer_, std::size_t(line), object->fontName(), size, object->textStyle()) +
                       textAdvance(textBuffer_.substr(start, i - start), object->fontName(), size, object->textStyle());
            double distance = std::fabs(x - local.x);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = i;
            }
            if (i >= end) {
                break;
            }
        }
        return best;
    }

    void CanvasView::beginText(const newui::Point& pt) {
        if (document_ == nullptr) {
            return;
        }
        commitText();
        BLPoint doc = toDocument(pt);
        SketchObject* hit = textObjectAt(doc);
        textEditing_ = true;
        textHighSurrogate_ = 0;
        if (hit != nullptr) {
            textId_ = hit->id();
            textBefore_ = {hit->text(), hit->fontName(), hit->fontSize(), hit->textMatrix()};
            textBuffer_ = hit->text();
            textSize_ = hit->fontSize();
            document_->setSelection({textId_});
            textCaret_ = textIndexAt(doc);
        } else {
            textId_ = 0;
            textBuffer_.clear();
            textCaret_ = 0;
            textDraft_.reset(SketchObject::makeText("", "", textSize_, doc));
            document_->clearSelection();
        }
        style().markDirty();
    }

    // ---- editing ----

    // Rebuilds the outline from the buffer after any change.
    void CanvasView::refreshText() {
        SketchObject* object = textObject();
        if (object == nullptr) {
            return;
        }
        if (textId_ != 0 && document_ != nullptr) {
            TextState state = SketchDocument::textStateOf(*object);
            state.text = textBuffer_;
            state.size = textSize_;
            state.matrix = object->textMatrix();
            document_->setObjectText(textId_, state);
            document_->touch();
        } else {
            SketchObject::Prototype::setTextState(*object, textBuffer_, object->fontName(), textSize_, object->textMatrix());
        }
        style().markDirty();
    }

    void CanvasView::textInsert(const std::string& utf8) {
        if (!textEditing_ || utf8.empty()) {
            return;
        }
        textBuffer_.insert(textCaret_, utf8);
        textCaret_ += utf8.size();
        refreshText();
    }

    void CanvasView::setTextSize(double size) {
        textSize_ = std::max(kMinTextSize, std::min(kMaxTextSize, size));
        if (textEditing_) {
            refreshText();
        }
        style().markDirty();
    }

    // A typed character (WM_CHAR, a UTF-16 unit): printable ones go in at the caret.
    SyncReturn CanvasView::handleKeyPress(View& /*sender*/, std::uint32_t keyMask, int keyChar, int /*repeatCount*/, std::uint32_t /*vkCode*/) {
        if (!textEditing_) {
            return SyncReturn::Ignored;
        }
        if ((keyMask & kmCtrl) != 0 || keyChar < 0x20 || keyChar == 0x7F) {
            return SyncReturn::Handled;   // Backspace, Enter and Esc arrive here too - handled as keys in textKeyDown()
        }
        unsigned long unit = static_cast<unsigned long>(keyChar) & 0xFFFFul;
        if (unit >= 0xD800 && unit < 0xDC00) {   // the first half of a surrogate pair waits for the second
            textHighSurrogate_ = unit;
            return SyncReturn::Handled;
        }
        if (unit >= 0xDC00 && unit < 0xE000 && textHighSurrogate_ != 0) {
            unit = 0x10000 + ((textHighSurrogate_ - 0xD800) << 10) + (unit - 0xDC00);
        }
        textHighSurrogate_ = 0;
        textInsert(utf8Of(unit));
        return SyncReturn::Handled;
    }

    // Keys while editing: everything is consumed so Delete or an arrow never reaches the selected object.
    bool CanvasView::textKeyDown(std::uint32_t keyMask, std::uint32_t vkCode) {
        if (!textEditing_) {
            return false;
        }
        bool ctrl = (keyMask & kmCtrl) != 0;
        if (ctrl) {
            if (vkCode == vkUpArrow) {
                setTextSize(textSize_ + (textSize_ >= 24.0 ? 4.0 : 2.0));
            } else if (vkCode == vkDownArrow) {
                setTextSize(textSize_ - (textSize_ > 24.0 ? 4.0 : 2.0));
            }
            return true;
        }
        switch (vkCode) {
            case vkEscape:
                commitText();
                return true;
            case vkReturn:
                textInsert("\n");
                return true;
            case vkBackSpace:
                if (textCaret_ > 0) {
                    std::size_t from = previousIndex(textBuffer_, textCaret_);
                    textBuffer_.erase(from, textCaret_ - from);
                    textCaret_ = from;
                    refreshText();
                }
                return true;
            case vkDelete:
                if (textCaret_ < textBuffer_.size()) {
                    textBuffer_.erase(textCaret_, nextIndex(textBuffer_, textCaret_) - textCaret_);
                    refreshText();
                }
                return true;
            case vkLeftArrow:
                textCaret_ = previousIndex(textBuffer_, textCaret_);
                break;
            case vkRightArrow:
                textCaret_ = nextIndex(textBuffer_, textCaret_);
                break;
            case vkHome:
                textCaret_ = lineStart(textBuffer_, textCaret_);
                break;
            case vkEnd:
                textCaret_ = lineEnd(textBuffer_, textCaret_);
                break;
            case vkUpArrow:
            case vkDownArrow: {
                // The same column (in characters) on the line above / below; the ends of the text stay put.
                std::size_t start = lineStart(textBuffer_, textCaret_);
                std::size_t column = characterCount(textBuffer_, start, textCaret_);
                std::size_t target;
                if (vkCode == vkUpArrow) {
                    if (start == 0) {
                        textCaret_ = 0;
                        style().markDirty();
                        return true;
                    }
                    target = lineStart(textBuffer_, start - 1);
                } else {
                    std::size_t end = lineEnd(textBuffer_, textCaret_);
                    if (end >= textBuffer_.size()) {
                        textCaret_ = textBuffer_.size();
                        style().markDirty();
                        return true;
                    }
                    target = end + 1;
                }
                std::size_t limit = lineEnd(textBuffer_, target);
                std::size_t at = target;
                for (std::size_t i = 0; i < column && at < limit; ++i) {
                    at = nextIndex(textBuffer_, at);
                }
                textCaret_ = at;
                break;
            }
            default:
                return true;
        }
        style().markDirty();
        return true;
    }

    // ---- finishing ----

    void CanvasView::commitText() {
        if (!textEditing_) {
            return;
        }
        textEditing_ = false;
        textHighSurrogate_ = 0;
        std::unique_ptr<SketchObject> draft = std::move(textDraft_);
        if (document_ == nullptr) {
            return;
        }
        if (textId_ == 0) {
            if (draft && !blank(textBuffer_)) {
                draft->setName("Text");
                draft->setFill(SketchPaint::solid(SketchColor::rgb(0.0f, 0.0f, 0.0f).convertedTo(document_->colorMode())));
                draft->setStroke(SketchPaint::none());
                document_->addUndoable(draft.release(), "Add Text");
            }
            style().markDirty();
            return;
        }
        SketchObject* object = document_->findById(textId_);
        if (object == nullptr) {
            return;
        }
        std::size_t id = textId_;
        TextSnapshot before = textBefore_;
        TextSnapshot after{object->text(), object->fontName(), object->fontSize(), object->textMatrix()};
        if (blank(after.text)) {
            // Emptied: the text goes away (undo brings it back as it was).
            putText(*document_, id, before.text, before.font, before.size, before.matrix);
            document_->setSelection({id});
            document_->deleteSelection();
        } else if (after.text != before.text || after.size != before.size) {
            SketchDocument* document = document_;
            auto apply = [document, id](const TextSnapshot& state) {
                putText(*document, id, state.text, state.font, state.size, state.matrix);
                document->touch();
            };
            document_->undoStack().push({"Edit Text", [apply, after]() { apply(after); }, [apply, before]() { apply(before); }});
        }
        textId_ = 0;
        style().markDirty();
    }

    // ---- painting: the thin box and the caret ----

    void CanvasView::paintTextEditor(BLContext& ctx) const {
        const SketchObject* object = textObject();
        if (object == nullptr) {
            return;
        }
        ctx.save();
        if (textId_ == 0) {
            // The new text is not in the document yet: draw its outline here, in view pixels.
            BLPath outline = object->geometry();
            BLPoint origin = toView(BLPoint(0.0, 0.0));
            BLMatrix2D toViewMatrix(double(zoom_), 0.0, 0.0, double(zoom_), origin.x, origin.y);
            outline.transform(toViewMatrix);
            ctx.set_fill_style(BLRgba32(0xFF000000u));
            ctx.fill_path(outline);
        }
        // The caret: a vertical line at the caret position, in the text's own (possibly turned or scaled) frame.
        std::size_t start = lineStart(textBuffer_, textCaret_);
        double size = object->fontSize();
        double lineHeight = textLineHeight(object->fontName(), size, object->textStyle());
        std::size_t line = std::count(textBuffer_.begin(), textBuffer_.begin() + std::ptrdiff_t(start), '\n');
        double x = textLineOffset(textBuffer_, line, object->fontName(), size, object->textStyle()) +
                   textAdvance(textBuffer_.substr(start, textCaret_ - start), object->fontName(), size, object->textStyle());
        double baseline = double(line) * lineHeight;
        BLPoint top = toView(object->mapTextPoint(x, baseline - 0.8 * size));
        BLPoint bottom = toView(object->mapTextPoint(x, baseline + 0.25 * size));
        ctx.set_stroke_style(BLRgba32(0xFF1E6FE0u));
        ctx.set_stroke_width(1.5);
        BLPath caret;
        caret.move_to(top.x, top.y);
        caret.line_to(bottom.x, bottom.y);
        ctx.stroke_path(caret);
        // A thin box around the text so it is clear what is being edited.
        BLBox box = object->geometryBounds();
        if (box.x1 > box.x0 && box.y1 > box.y0) {
            BLPoint a = toView(BLPoint(box.x0, box.y0)), b = toView(BLPoint(box.x1, box.y1));
            ctx.set_stroke_width(1.0);
            ctx.set_stroke_style(BLRgba32(0x801E6FE0u));
            ctx.stroke_rect(BLRect(a.x - 3.0, a.y - 3.0, b.x - a.x + 6.0, b.y - a.y + 6.0));
        }
        ctx.restore();
    }

}
