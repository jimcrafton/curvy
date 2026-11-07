#include "curvy/loadingoverlay.h"
#include "curvy/theme.h"

#include <newui/fontmanager.h>

#include <algorithm>
#include <cmath>

namespace curvy {

    using namespace newui;

    namespace {
        constexpr float kCardWidth = 380.0f, kCardHeight = 150.0f, kPadding = 26.0f;
        constexpr float kRingRadius = 34.0f, kRingWidth = 7.0f;
        constexpr float kPi = 3.14159265f;
        constexpr float kTurnPerTick = 0.19f;   // the controller ticks about 30 times a second: a turn in a bit over a second

        Color colour(std::uint32_t rgb, std::uint32_t alpha = 255) { return Color(Theme::argb(rgb, alpha), true); }

        void strokeWith(shapes::Shape& shape, const Color& line, float width) {
            shape.style().fill().setKind(gfx::PaintKind::None);
            shape.style().stroke().setColor(line);
            shape.style().stroke().setKind(gfx::PaintKind::Color);
            shape.style().stroke().setWidth(width);
        }

        shapes::Text* makeText(shapes::ShapeLayer& layer, float size, bool bold, std::uint32_t rgb) {
            auto* shape = new shapes::Text();
            shape->font() = FontManager::getSystemFont(SystemUIFont::Message);
            shape->font().setSize(size);
            shape->font().setBold(bold);
            shape->style().fill().setColor(colour(rgb));
            shape->style().fill().setKind(gfx::PaintKind::Color);
            layer.addShape(shape);
            return shape;
        }
    }

    LoadingOverlay::LoadingOverlay() {
        const Theme& theme = Theme::current();
        setFillColor(Color(0.0f, 0.0f, 0.0f, 1.0f));   // dims the window behind the card
        setOpacity(0.55f);
        setCompositingOp(CompSrcOver);

        card_ = new shapes::RoundRect();
        card_->setRadiusX(10.0f);
        card_->setRadiusY(10.0f);
        card_->style().fill().setColor(colour(theme.bgSurface, 0xF5));
        card_->style().fill().setKind(gfx::PaintKind::Color);
        card_->style().stroke().setColor(colour(theme.border));
        card_->style().stroke().setKind(gfx::PaintKind::Color);
        card_->style().stroke().setWidth(1.0f);
        card_->style().dropShadow().setEnabled(true);
        card_->style().dropShadow().setColor(colour(0x000000, 0x90));
        card_->style().dropShadow().setSoftness(16.0f);
        card_->style().dropShadow().setOffset(Point(0.0f, 8.0f));
        shapeLayer().addShape(card_);

        track_ = new shapes::Circle();
        track_->setRadius(kRingRadius);
        strokeWith(*track_, colour(theme.slot), kRingWidth);
        shapeLayer().addShape(track_);

        arc_ = new shapes::Arc();
        arc_->setRadius(kRingRadius);
        strokeWith(*arc_, colour(theme.accent), kRingWidth);
        shapeLayer().addShape(arc_);

        title_ = makeText(shapeLayer(), 16.0f, true, theme.textPrimary);
        percent_ = makeText(shapeLayer(), 13.0f, false, theme.textMuted);
        hint_ = makeText(shapeLayer(), 12.0f, false, theme.textMuted);
        hint_->setText("Esc cancels");
        update(-1.0f);
    }

    void LoadingOverlay::setTitle(const std::string& title) {
        title_->setText(title);
        layout();
    }

    void LoadingOverlay::setCancelling() {
        cancelling_ = true;
        hint_->setText("Cancelling...");
        layout();
    }

    void LoadingOverlay::update(float fraction) {
        fraction_ = fraction;
        if (fraction < 0.0f) {
            turn_ = std::fmod(turn_ + kTurnPerTick, 2.0f * kPi);
            arc_->setStartRadians(turn_);
            arc_->setSweepRadians(kPi * 1.1f);
            percent_->setText(cancelling_ ? std::string() : std::string("Working..."));
        } else {
            const float done = std::min(1.0f, fraction);
            arc_->setStartRadians(-kPi * 0.5f);   // from twelve o'clock, clockwise
            arc_->setSweepRadians(std::max(0.01f, done * 2.0f * kPi));
            percent_->setText(std::to_string(int(done * 100.0f + 0.5f)) + "%");
        }
        layout();
    }

    void LoadingOverlay::viewSized(const Rect& bounds) {
        bounds_ = bounds;
        layout();
    }

    void LoadingOverlay::layout() {
        const float left = (bounds_.size().width - kCardWidth) * 0.5f;
        const float top = (bounds_.size().height - kCardHeight) * 0.5f;
        card_->setX(left);
        card_->setY(top);
        card_->setWidth(kCardWidth);
        card_->setHeight(kCardHeight);

        const float cx = left + kPadding + kRingRadius + kRingWidth, cy = top + kCardHeight * 0.5f;
        for (shapes::Circle* ring : {static_cast<shapes::Circle*>(track_), static_cast<shapes::Circle*>(arc_)}) {
            ring->setCenterX(cx);
            ring->setCenterY(cy);
        }
        const float textX = cx + kRingRadius + kRingWidth + 22.0f;
        title_->setX(textX);
        title_->setY(cy - 12.0f);
        percent_->setX(textX);
        percent_->setY(cy + 10.0f);
        hint_->setX(textX);
        hint_->setY(cy + 34.0f);
    }

}
