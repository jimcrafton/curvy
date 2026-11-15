#include "curvy/paletteextract.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace curvy {

    namespace {

        using Rgb = std::array<float, 3>;   // 0..255

        constexpr int maxSide = 128;
        constexpr float mergeDistance = 28.0f;
        constexpr float nearWhite = 0.95f;
        constexpr float nearBlack = 0.06f;

        struct Box {
            std::vector<Rgb> pixels;
        };

        struct Found {
            Rgb color;
            std::size_t count;
        };

        float lightness(const Rgb& c) {
            return (0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2]) / 255.0f;
        }

        float hue(const Rgb& c) {
            const float r = c[0] / 255.0f, g = c[1] / 255.0f, b = c[2] / 255.0f;
            const float high = std::max({r, g, b}), low = std::min({r, g, b}), spread = high - low;
            if (spread < 0.001f) {
                return 0.0f;
            }
            float h = 0.0f;
            if (high == r) {
                h = std::fmod((g - b) / spread + 6.0f, 6.0f);
            } else if (high == g) {
                h = (b - r) / spread + 2.0f;
            } else {
                h = (r - g) / spread + 4.0f;
            }
            return h * 60.0f;
        }

        float distance(const Rgb& a, const Rgb& b) {
            return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
        }

        // The channel with the widest spread, and that spread.
        int widestChannel(const Box& box, float& spread) {
            int widest = 0;
            spread = 0.0f;
            for (int channel = 0; channel < 3; ++channel) {
                float low = 255.0f, high = 0.0f;
                for (const Rgb& p : box.pixels) {
                    low = std::min(low, p[channel]);
                    high = std::max(high, p[channel]);
                }
                if (high - low > spread) {
                    spread = high - low;
                    widest = channel;
                }
            }
            return widest;
        }

        Found average(const Box& box) {
            Rgb sum{0.0f, 0.0f, 0.0f};
            for (const Rgb& p : box.pixels) {
                for (int i = 0; i < 3; ++i) {
                    sum[i] += p[i];
                }
            }
            const float n = float(box.pixels.size());
            return Found{Rgb{sum[0] / n, sum[1] / n, sum[2] / n}, box.pixels.size()};
        }

        void joinClose(std::vector<Found>& found) {
            for (;;) {
                std::size_t first = 0, second = 0;
                float closest = mergeDistance;
                for (std::size_t i = 0; i < found.size(); ++i) {
                    for (std::size_t j = i + 1; j < found.size(); ++j) {
                        const float d = distance(found[i].color, found[j].color);
                        if (d < closest) {
                            closest = d;
                            first = i;
                            second = j;
                        }
                    }
                }
                if (closest >= mergeDistance) {
                    return;
                }
                Found& a = found[first];
                const Found& b = found[second];
                const float total = float(a.count + b.count);
                for (int k = 0; k < 3; ++k) {
                    a.color[k] = (a.color[k] * float(a.count) + b.color[k] * float(b.count)) / total;
                }
                a.count += b.count;
                found.erase(found.begin() + std::ptrdiff_t(second));
            }
        }

    }

    std::vector<Swatch> PaletteExtract::fromPixels(const std::uint8_t* rgba, int width, int height, const PaletteExtractOptions& options) {
        std::vector<Swatch> result;
        if (rgba == nullptr || width <= 0 || height <= 0) {
            return result;
        }
        const int count = std::clamp(options.count, 1, 64);
        const int step = std::max(1, std::max(width, height) / maxSide);

        Box all;
        for (int y = 0; y < height; y += step) {
            for (int x = 0; x < width; x += step) {
                const std::uint8_t* p = rgba + (std::size_t(y) * std::size_t(width) + std::size_t(x)) * 4;
                if (p[3] < 128) {
                    continue;
                }
                const Rgb c{float(p[0]), float(p[1]), float(p[2])};
                if (options.ignoreNearWhiteAndBlack && (lightness(c) > nearWhite || lightness(c) < nearBlack)) {
                    continue;
                }
                all.pixels.push_back(c);
            }
        }
        if (all.pixels.empty()) {
            return result;
        }

        std::vector<Box> boxes;
        boxes.push_back(std::move(all));
        while (int(boxes.size()) < count) {
            std::size_t pick = boxes.size();
            int pickChannel = 0;
            float best = 0.0f;
            for (std::size_t i = 0; i < boxes.size(); ++i) {
                float spread = 0.0f;
                const int channel = widestChannel(boxes[i], spread);
                const float score = spread * std::sqrt(float(boxes[i].pixels.size()));
                if (boxes[i].pixels.size() > 1 && spread > 0.0f && score > best) {
                    best = score;
                    pick = i;
                    pickChannel = channel;
                }
            }
            if (pick == boxes.size()) {
                break;
            }
            std::vector<Rgb>& pixels = boxes[pick].pixels;
            std::sort(pixels.begin(), pixels.end(), [pickChannel](const Rgb& a, const Rgb& b) {
                return a[pickChannel] != b[pickChannel] ? a[pickChannel] < b[pickChannel] : a < b;
            });
            // The median, moved to the nearest place where the channel changes: a run of equal pixels must not be cut in two.
            std::size_t half = pixels.size() / 2;
            for (std::size_t offset = 0;; ++offset) {
                if (half >= offset && half - offset > 0 && pixels[half - offset - 1][pickChannel] != pixels[half - offset][pickChannel]) {
                    half -= offset;
                    break;
                }
                if (half + offset < pixels.size() && pixels[half + offset - 1][pickChannel] != pixels[half + offset][pickChannel]) {
                    half += offset;
                    break;
                }
            }
            Box upper;
            upper.pixels.assign(pixels.begin() + std::ptrdiff_t(half), pixels.end());
            pixels.resize(half);
            boxes.push_back(std::move(upper));
        }

        std::vector<Found> found;
        for (const Box& box : boxes) {
            found.push_back(average(box));
        }
        if (options.mergeNearDuplicates) {
            joinClose(found);
        }

        using Order = PaletteExtractOptions::Order;
        std::stable_sort(found.begin(), found.end(), [&options](const Found& a, const Found& b) {
            switch (options.order) {
                case Order::Hue:
                    return hue(a.color) < hue(b.color);
                case Order::Lightness:
                    return lightness(a.color) < lightness(b.color);
                default:
                    return a.count > b.count;
            }
        });

        for (const Found& f : found) {
            Swatch swatch;
            swatch.r = std::round(f.color[0]) / 255.0f;
            swatch.g = std::round(f.color[1]) / 255.0f;
            swatch.b = std::round(f.color[2]) / 255.0f;
            result.push_back(swatch);
        }
        return result;
    }

}
