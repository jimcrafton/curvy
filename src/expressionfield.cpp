#include "curvy/expressionfield.h"

#include <cstdlib>

namespace curvy {

    FieldEntry parseFieldEntry(const std::string& text) {
        const std::size_t first = text.find_first_not_of(" \t");
        const std::size_t last = text.find_last_not_of(" \t");
        FieldEntry entry;
        if (first == std::string::npos) {
            return entry;
        }
        const std::string trimmed = text.substr(first, last - first + 1);
        if (trimmed[0] == '=') {
            const std::size_t body = trimmed.find_first_not_of(" \t", 1);
            entry.kind = FieldEntry::Kind::Expression;
            entry.expression = body == std::string::npos ? std::string() : trimmed.substr(body);
            return entry;
        }
        char* end = nullptr;
        const double number = std::strtod(trimmed.c_str(), &end);
        if (end != nullptr && *end == '\0' && end != trimmed.c_str()) {
            entry.kind = FieldEntry::Kind::Number;
            entry.number = number;
        } else {
            entry.kind = FieldEntry::Kind::Expression;
            entry.expression = trimmed;
        }
        return entry;
    }

    ExpressionField::ExpressionField() = default;

    void ExpressionField::setExpressionMode(bool on) {
        if (on == expression_) {
            return;
        }
        expression_ = on;
        setTextColor(on ? accent_ : plain_);
        style().markDirty();
    }

    void ExpressionField::setColours(const newui::Color& plain, const newui::Color& expression) {
        plain_ = plain;
        accent_ = expression;
        setTextColor(expression_ ? accent_ : plain_);
    }

    void ExpressionField::setKeyState(KeyState state) {
        if (state == keyState_) {
            return;
        }
        keyState_ = state;
        style().markDirty();
    }

    void ExpressionField::paint(BLContext& ctx) {
        TextField::paint(ctx);
        if (keyState_ != KeyState::None) {
            const newui::Rect bounds = getClientBounds();
            const double half = 4.0;
            const double cx = double(bounds.right()) - 10.0, cy = double(bounds.top()) + double(bounds.height()) * 0.5;
            BLPath diamond;
            diamond.move_to(cx, cy - half);
            diamond.line_to(cx + half, cy);
            diamond.line_to(cx, cy + half);
            diamond.line_to(cx - half, cy);
            diamond.close();
            ctx.set_fill_style(BLRgba(accent_.r, accent_.g, accent_.b, accent_.a));
            if (keyState_ == KeyState::OnKey) {
                ctx.fill_path(diamond);
            } else {
                ctx.set_stroke_width(1.2);
                ctx.set_stroke_style(BLRgba(accent_.r, accent_.g, accent_.b, accent_.a));
                ctx.stroke_path(diamond);
            }
        }
        if (expression_) {
            const newui::Rect bounds = getClientBounds();
            ctx.set_fill_style(BLRgba(accent_.r, accent_.g, accent_.b, accent_.a));
            ctx.fill_rect(BLRect(double(bounds.left()), double(bounds.top()), 2.0, double(bounds.height())));
        }
    }

}
