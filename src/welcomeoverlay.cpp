#include "curvy/welcomeoverlay.h"
#include "curvy/theme.h"

#include <newui/fontmanager.h>

namespace curvy {

    using namespace newui;

    namespace {
        constexpr float kCardWidth = 600.0f, kCardHeight = 470.0f, kPadding = 28.0f;
        constexpr float kPitch = 44.0f, kGap = 9.0f;      // a key is kPitch wide minus kGap
        constexpr float kKeyboardWidth = 11.2f;          // in pitches: the bottom row, Ctrl Alt Space Alt Ctrl
        constexpr float kLip = 3.0f;                     // the darker edge under a key cap

        Color colour(std::uint32_t rgb, std::uint32_t alpha = 255) { return Color(Theme::argb(rgb, alpha), true); }

        void paintWith(shapes::Shape& shape, const Color& fill) {
            shape.style().fill().setColor(fill);
            shape.style().fill().setKind(gfx::PaintKind::Color);
        }

        void strokeWith(shapes::Shape& shape, const Color& line, float width = 1.0f) {
            shape.style().stroke().setColor(line);
            shape.style().stroke().setKind(gfx::PaintKind::Color);
            shape.style().stroke().setWidth(width);
        }

        // The keyboard's outline: a blue roughly opposite the amber accent, so the lit Space bar stands out from it.
        std::uint32_t outlineBlue(const Theme& theme) { return theme.isDark ? 0x4F86C6 : 0x2F6DB5; }

        // A colour a step towards black (factor < 1), for the edge under a key cap.
        std::uint32_t darker(std::uint32_t rgb, float factor) {
            auto channel = [&](int shift) { return std::uint32_t(float((rgb >> shift) & 0xFF) * factor); };
            return (channel(16) << 16) | (channel(8) << 8) | channel(0);
        }
    }

    WelcomeOverlay::WelcomeOverlay() {
        const Theme& theme = Theme::current();
        setFillColor(Color(0.0f, 0.0f, 0.0f, 1.0f));   // dims the window behind the card
        setOpacity(0.6f);
        setCompositingOp(CompSrcOver);

        card_ = new shapes::RoundRect();
        card_->setRadiusX(10.0f);
        card_->setRadiusY(10.0f);
        paintWith(*card_, colour(theme.bgSurface, 0xF5));
        strokeWith(*card_, colour(theme.border));
        card_->style().dropShadow().setEnabled(true);
        card_->style().dropShadow().setColor(colour(outlineBlue(theme), 0xA0));
        card_->style().dropShadow().setSoftness(16.0f);
        card_->style().dropShadow().setOffset(Point(0.0f, 8.0f));
        shapeLayer().addShape(card_);

        title_ = addText("Welcome to curvy", 22.0f, true, theme.textPrimary);
        lead_ = addText("Everything starts with the Space bar.", 14.0f, false, theme.textMuted);

        plate_ = new shapes::RoundRect();
        plate_->setRadiusX(8.0f);
        plate_->setRadiusY(8.0f);
        paintWith(*plate_, colour(theme.bgApp));
        strokeWith(*plate_, colour(outlineBlue(theme), 0x90));
        shapeLayer().addShape(plate_);

        const char* rows[3] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
        const float indent[3] = {0.6f, 1.1f, 1.6f};
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; rows[r][c] != '\0'; ++c) {
                addKey(std::string(1, rows[r][c]), float(r), indent[r] + float(c), 1.0f, false);
            }
        }
        addKey("Ctrl", 3.0f, 0.0f, 1.4f, false);
        addKey("Alt", 3.0f, 1.4f, 1.4f, false);
        addKey("Space", 3.0f, 2.8f, 5.6f, true);
        addKey("Alt", 3.0f, 8.4f, 1.4f, false);
        addKey("Ctrl", 3.0f, 9.8f, 1.4f, false);

        ring_ = new shapes::RoundRect();
        ring_->setRadiusX(9.0f);
        ring_->setRadiusY(9.0f);
        strokeWith(*ring_, colour(theme.accent, 0xB0), 1.5f);
        shapeLayer().addShape(ring_);

        for (const char* text : {"Tap Space to open the ring menu right where your pen is.",
                                 "Move to a slot and lift, or press the letter on it.",
                                 "Hold Space and move the pen to pan the canvas.",
                                 "Tap Space now to start your first document."}) {
            lines_.push_back(addText(text, 13.0f, false, theme.textPrimary));
        }
        note_ = addText("Press any key or click anywhere to close. This is shown once.", 12.0f, false, theme.textMuted);
    }

    shapes::Text* WelcomeOverlay::addText(const std::string& text, float size, bool bold, std::uint32_t rgb) {
        auto* shape = new shapes::Text();
        shape->setText(text);
        shape->font() = FontManager::getSystemFont(SystemUIFont::Message);
        shape->font().setSize(size);
        shape->font().setBold(bold);
        paintWith(*shape, colour(rgb));
        shapeLayer().addShape(shape);
        return shape;
    }

    void WelcomeOverlay::addKey(const std::string& label, float row, float column, float span, bool space) {
        const Theme& theme = Theme::current();
        Key key;
        key.row = row;
        key.column = column;
        key.span = span;
        key.lip = new shapes::RoundRect();
        key.face = new shapes::RoundRect();
        for (shapes::RoundRect* shape : {key.lip, key.face}) {
            shape->setRadiusX(6.0f);
            shape->setRadiusY(6.0f);
        }
        if (space) {
            paintWith(*key.lip, colour(darker(theme.accent, 0.62f)));
            paintWith(*key.face, colour(theme.accent));
            strokeWith(*key.face, colour(theme.accentHover));
            // A tiny glow: a soft amber halo around the lit key.
            key.lip->style().glow().setEnabled(true);
            key.lip->style().glow().setColor(colour(theme.accent));
            key.lip->style().glow().setAmount(0.4f);
            key.lip->style().glow().setSoftness(9.0f);
        } else {
            paintWith(*key.lip, colour(darker(theme.slot, 0.55f)));
            paintWith(*key.face, colour(theme.slot));
            strokeWith(*key.face, colour(outlineBlue(theme)), 1.2f);
        }
        shapeLayer().addShape(key.lip);
        shapeLayer().addShape(key.face);
        key.label = addText(label, label.size() > 1 ? 12.0f : 13.0f, space, space ? theme.onAccent : theme.textMuted);
        keys_.push_back(key);
    }

    void WelcomeOverlay::place(shapes::Text& text, float x, float y, bool centred, float boxWidth) const {
        const TextMetrics metrics = text.font().measureText(text.text());
        text.setX(centred ? x + (boxWidth - metrics.width) * 0.5f : x);
        text.setY(y);
    }

    void WelcomeOverlay::viewSized(const Rect& bounds) {
        const float left = (bounds.size().width - kCardWidth) * 0.5f;
        const float top = (bounds.size().height - kCardHeight) * 0.5f;
        card_->setX(left);
        card_->setY(top);
        card_->setWidth(kCardWidth);
        card_->setHeight(kCardHeight);

        place(*title_, left + kPadding, top + kPadding + 22.0f, false);
        place(*lead_, left + kPadding, top + kPadding + 52.0f, false);

        const float boardW = kKeyboardWidth * kPitch;
        const float boardX = left + (kCardWidth - boardW) * 0.5f;
        const float boardY = top + 104.0f;
        plate_->setX(boardX - 14.0f);
        plate_->setY(boardY - 12.0f);
        plate_->setWidth(boardW + 28.0f - kGap);
        plate_->setHeight(4.0f * kPitch + 24.0f - kGap + kLip);

        for (const Key& key : keys_) {
            const float x = boardX + key.column * kPitch;
            const float y = boardY + key.row * kPitch;
            const float w = key.span * kPitch - kGap;
            const float h = kPitch - kGap;
            key.lip->setX(x);
            key.lip->setY(y + kLip);
            key.lip->setWidth(w);
            key.lip->setHeight(h);
            key.face->setX(x);
            key.face->setY(y);
            key.face->setWidth(w);
            key.face->setHeight(h);
            const TextMetrics metrics = key.label->font().measureText(key.label->text());
            place(*key.label, x, y + (h + metrics.ascent - metrics.descent) * 0.5f, true, w);
            if (key.face->style().glow().enabled() || key.lip->style().glow().enabled()) {
                ring_->setX(x - 5.0f);
                ring_->setY(y - 5.0f);
                ring_->setWidth(w + 10.0f);
                ring_->setHeight(h + kLip + 10.0f);
            }
        }

        float y = top + 320.0f;
        for (shapes::Text* line : lines_) {
            place(*line, left + kPadding, y, false);
            y += 24.0f;
        }
        place(*note_, left + kPadding, top + kCardHeight - kPadding + 4.0f, false);
    }

}
