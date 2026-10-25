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

    void ExpressionField::paint(BLContext& ctx) {
        TextField::paint(ctx);
        if (expression_) {
            const newui::Rect bounds = getClientBounds();
            ctx.set_fill_style(BLRgba(accent_.r, accent_.g, accent_.b, accent_.a));
            ctx.fill_rect(BLRect(double(bounds.left()), double(bounds.top()), 2.0, double(bounds.height())));
        }
    }

}
