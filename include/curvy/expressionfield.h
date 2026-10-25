#pragma once

#include <newui/controls.h>

#include <string>

namespace curvy {

    // What was typed into a number field: a number, or an expression (`fx`) that gives the number its value at any time.
    struct FieldEntry {
        enum class Kind { Nothing, Number, Expression };
        Kind kind = Kind::Nothing;
        double number = 0.0;
        std::string expression;   // empty with Kind::Expression: take the expression away
    };

    // Reads field text: empty means nothing was entered; "=" followed by text is an expression ("=" alone clears one); text that is a whole number is a
    // number; any other text is an expression, so "sin(time) * 50" works without the "=".
    FieldEntry parseFieldEntry(const std::string& text);

    // A text field for a number that may be driven by an expression. While one drives it the field shows "=expression" in the accent colour with
    // a bar down its left edge; the owner sets that state (setExpressionMode) and reads what was typed with parseFieldEntry().
    // @reflect ignore=true
    class ExpressionField : public newui::TextField {
    public:
        ExpressionField();

        void setExpressionMode(bool on);
        bool expressionMode() const { return expression_; }
        // The colours of plain and expression text (and of the bar).
        void setColours(const newui::Color& plain, const newui::Color& expression);

        // A keyed number shows a diamond at the right edge: filled on a key of the current time, hollow between keys.
        enum class KeyState { None, Between, OnKey };
        void setKeyState(KeyState state);
        KeyState keyState() const { return keyState_; }

        void paint(BLContext& ctx) override;

    private:
        KeyState keyState_ = KeyState::None;
        bool expression_ = false;
        newui::Color plain_;
        newui::Color accent_;
    };

}
