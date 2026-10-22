#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace curvy {

    // The workspace's colour tokens (docs/dark mode.docx): semantic names, 0xRRGGBB, one set per mode. Nothing in the interface uses pure
    // black or white for structure. Everything drawn by the workspace (hint bar, rings, cards, the pasteboard) takes its colours from
    // Theme::current(); newui's own roles (rulers, native controls) are set from the same tokens by applyWorkspaceTheme().
    struct Theme {
        std::uint32_t bgApp;         // behind panels and bars
        std::uint32_t bgSurface;     // floating cards, ring hub, dialogs
        std::uint32_t bgCanvas;      // the pasteboard around the artboards
        std::uint32_t slot;          // a ring slot at rest (a step above bgSurface)
        std::uint32_t border;
        std::uint32_t textPrimary;
        std::uint32_t textMuted;
        std::uint32_t iconNeutral;
        std::uint32_t accent;        // active tool, hot ring slot, selection
        std::uint32_t accentHover;
        std::uint32_t onAccent;      // text on an accent fill
        std::uint32_t highlight;     // the amber of values, the ring hub and "this is being changed"
        bool isDark;

        static const Theme& dark() {
            static const Theme theme{0x121212, 0x1E1E1E, 0x2A2A2A, 0x2E2E2E, 0x4A4A4A, 0xF0F0F0, 0xA0A0A0, 0xC5C5C5, 0x3A86FF, 0x5C9DFF, 0x101010, 0xFFB454, true};
            return theme;
        }
        static const Theme& light() {
            static const Theme theme{0xF5F5F5, 0xFFFFFF, 0xEAEAEA, 0xF0F0F0, 0xC8C8C8, 0x1A1A1A, 0x636363, 0x4A4A4A, 0x0056D2, 0x0043A4, 0xFFFFFF, 0xB35C00, false};
            return theme;
        }
        static const Theme*& currentPointer() {
            static const Theme* current = &dark();
            return current;
        }
        static const Theme& current() { return *currentPointer(); }
        static void setCurrent(const Theme& theme) { currentPointer() = &theme; }

        // An opaque 0xAARRGGBB for Blend2D, with `alpha` 0..255.
        static std::uint32_t argb(std::uint32_t rgb, std::uint32_t alpha = 255) { return (alpha << 24) | (rgb & 0xFFFFFFu); }

        // WCAG 2.x contrast ratio of two colours (1 .. 21).
        static double contrast(std::uint32_t a, std::uint32_t b) {
            auto channel = [](std::uint32_t value) {
                const double c = double(value & 0xFF) / 255.0;
                return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
            };
            auto luminance = [&](std::uint32_t rgb) { return 0.2126 * channel(rgb >> 16) + 0.7152 * channel(rgb >> 8) + 0.0722 * channel(rgb); };
            const double la = luminance(a), lb = luminance(b);
            return ((std::max)(la, lb) + 0.05) / ((std::min)(la, lb) + 0.05);
        }
    };

}
