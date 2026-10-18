#include "curvy/imagetrace.h"

#include "curvy/shapetools.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace curvy {

    namespace {
        struct Rgb {
            int r = 0, g = 0, b = 0;
        };

        // A box of colors for the median cut: the pixel indices in it.
        struct ColorBox {
            std::vector<std::uint32_t> colors;   // 0xRRGGBB, one per opaque pixel (sampled)
        };

        int channelOf(std::uint32_t c, int channel) {
            return int((c >> (16 - 8 * channel)) & 0xFFu);
        }

        // The palette of at most `count` colors that the median cut of `samples` gives.
        std::vector<Rgb> medianCut(std::vector<std::uint32_t> samples, int count) {
            std::vector<ColorBox> boxes(1);
            boxes[0].colors = std::move(samples);
            while (int(boxes.size()) < count) {
                // Split the box with the widest spread of any channel.
                int best = -1, bestChannel = 0, bestRange = 0;
                for (std::size_t i = 0; i < boxes.size(); ++i) {
                    if (boxes[i].colors.size() < 2) {
                        continue;
                    }
                    for (int ch = 0; ch < 3; ++ch) {
                        int lo = 255, hi = 0;
                        for (std::uint32_t c : boxes[i].colors) {
                            lo = std::min(lo, channelOf(c, ch));
                            hi = std::max(hi, channelOf(c, ch));
                        }
                        if (hi - lo > bestRange) {
                            bestRange = hi - lo;
                            best = int(i);
                            bestChannel = ch;
                        }
                    }
                }
                if (best < 0) {
                    break;   // nothing left to tell apart
                }
                std::vector<std::uint32_t>& colors = boxes[std::size_t(best)].colors;
                // Cut across the middle of the widest spread (not at the median count: a color that fills most of the picture would
                // then swallow its neighbours' boxes and small, different colors would never get a box of their own).
                int lo = 255, hi = 0;
                for (std::uint32_t c : colors) {
                    lo = std::min(lo, channelOf(c, bestChannel));
                    hi = std::max(hi, channelOf(c, bestChannel));
                }
                const int middle = (lo + hi) / 2;
                ColorBox upper, lower;
                for (std::uint32_t c : colors) {
                    (channelOf(c, bestChannel) > middle ? upper : lower).colors.push_back(c);
                }
                colors = std::move(lower.colors);
                boxes.push_back(std::move(upper));
            }
            std::vector<Rgb> palette;
            for (const ColorBox& box : boxes) {
                if (box.colors.empty()) {
                    continue;
                }
                double r = 0, g = 0, b = 0;
                for (std::uint32_t c : box.colors) {
                    r += channelOf(c, 0);
                    g += channelOf(c, 1);
                    b += channelOf(c, 2);
                }
                double n = double(box.colors.size());
                palette.push_back({int(r / n + 0.5), int(g / n + 0.5), int(b / n + 0.5)});
            }
            return palette;
        }

        // The corners of the outlines of the set pixels of `mask` (width x height). Outer outlines run clockwise on the screen, holes the
        // other way, so the non-zero rule fills exactly the set pixels. Only the corners are kept (not every pixel edge).
        std::vector<std::vector<BLPoint>> maskOutlines(const std::vector<std::uint8_t>& mask, int width, int height) {
            struct Edge {
                int from, to, dir;
                bool used;
            };
            const int stride = width + 1;
            auto vertex = [stride](int x, int y) { return y * stride + x; };
            auto set = [&](int x, int y) { return x >= 0 && y >= 0 && x < width && y < height && mask[std::size_t(y * width + x)] != 0; };
            std::vector<Edge> edges;
            std::vector<std::array<int, 4>> out(std::size_t(stride) * std::size_t(height + 1), std::array<int, 4>{-1, -1, -1, -1});
            auto add = [&](int x0, int y0, int x1, int y1, int dir) {
                int from = vertex(x0, y0);
                out[std::size_t(from)][std::size_t(dir)] = int(edges.size());
                edges.push_back({from, vertex(x1, y1), dir, false});
            };
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    if (!set(x, y)) {
                        continue;
                    }
                    if (!set(x, y - 1)) { add(x, y, x + 1, y, 0); }
                    if (!set(x + 1, y)) { add(x + 1, y, x + 1, y + 1, 1); }
                    if (!set(x, y + 1)) { add(x + 1, y + 1, x, y + 1, 2); }
                    if (!set(x - 1, y)) { add(x, y + 1, x, y, 3); }
                }
            }
            std::vector<std::vector<BLPoint>> loops;
            for (std::size_t first = 0; first < edges.size(); ++first) {
                if (edges[first].used) {
                    continue;
                }
                std::vector<BLPoint> corners;
                int current = int(first);
                int previousDir = -1;
                for (;;) {
                    Edge& e = edges[std::size_t(current)];
                    e.used = true;
                    if (e.dir != previousDir) {
                        corners.push_back(BLPoint(double(e.from % stride), double(e.from / stride)));
                    }
                    previousDir = e.dir;
                    int next = -1;
                    for (int turn : {1, 0, 3}) {   // right first, so pixels that only touch at a corner stay apart
                        int candidate = out[std::size_t(e.to)][std::size_t((e.dir + turn) % 4)];
                        if (candidate >= 0 && (!edges[std::size_t(candidate)].used || candidate == int(first))) {
                            next = candidate;
                            break;
                        }
                    }
                    if (next < 0 || next == int(first)) {
                        break;
                    }
                    current = next;
                }
                if (corners.size() >= 3) {
                    // The loop's first corner may lie in the middle of a straight run: harmless.
                    loops.push_back(std::move(corners));
                }
            }
            return loops;
        }

        double polygonArea(const std::vector<BLPoint>& loop) {
            double a = 0.0;
            for (std::size_t i = 0; i < loop.size(); ++i) {
                const BLPoint& p = loop[i];
                const BLPoint& q = loop[(i + 1) % loop.size()];
                a += p.x * q.y - q.x * p.y;
            }
            return a * 0.5;
        }

        // One pass of corner cutting: every corner is replaced by two points a quarter of the way along its two edges, so the right
        // angles of the pixel steps become gentle bends the curve fit can follow.
        std::vector<BLPoint> cutCorners(const std::vector<BLPoint>& loop) {
            std::vector<BLPoint> out;
            for (std::size_t i = 0; i < loop.size(); ++i) {
                const BLPoint& p = loop[i];
                const BLPoint& q = loop[(i + 1) % loop.size()];
                out.push_back(BLPoint(p.x * 0.75 + q.x * 0.25, p.y * 0.75 + q.y * 0.25));
                out.push_back(BLPoint(p.x * 0.25 + q.x * 0.75, p.y * 0.25 + q.y * 0.75));
            }
            return out;
        }

        void addLoop(BLPath& path, const std::vector<BLPoint>& corners, double tolerance) {
            std::vector<BLPoint> smooth = cutCorners(corners);
            smooth.push_back(smooth.front());   // a closed line as an open one that ends where it began
            std::vector<PenAnchor> anchors = fitCurve(smooth, tolerance);
            if (anchors.size() < 3) {
                return;
            }
            if (std::hypot(anchors.back().point.x - anchors.front().point.x, anchors.back().point.y - anchors.front().point.y) < 1e-6) {
                anchors.front().in = anchors.back().in;
                anchors.front().hasIn = anchors.back().hasIn;
                anchors.pop_back();
            }
            path.add_path(penPath(anchors, true));
        }
    }

    TraceResult traceRgba(const std::vector<std::uint32_t>& pixels, int width, int height, const TraceOptions& options) {
        TraceResult result;
        if (width <= 0 || height <= 0 || pixels.size() < std::size_t(width) * std::size_t(height)) {
            return result;
        }
        // Shrink by a whole factor, averaging each block, so big pictures stay quick to trace.
        int factor = 1;
        while (std::max(width, height) / factor > std::max(16, options.maxSize)) {
            ++factor;
        }
        const int w = std::max(1, width / factor), h = std::max(1, height / factor);
        std::vector<std::uint32_t> rgb(std::size_t(w) * std::size_t(h));
        std::vector<std::uint8_t> opaque(rgb.size(), 0);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                double r = 0, g = 0, b = 0, a = 0;
                for (int dy = 0; dy < factor; ++dy) {
                    for (int dx = 0; dx < factor; ++dx) {
                        std::uint32_t p = pixels[std::size_t(y * factor + dy) * std::size_t(width) + std::size_t(x * factor + dx)];
                        double alpha = double(p >> 24) / 255.0;
                        r += double((p >> 16) & 0xFFu) * alpha;
                        g += double((p >> 8) & 0xFFu) * alpha;
                        b += double(p & 0xFFu) * alpha;
                        a += alpha;
                    }
                }
                double n = double(factor * factor);
                std::size_t i = std::size_t(y * w + x);
                if (a / n >= 0.5) {
                    const std::uint32_t red = std::uint32_t(r / a + 0.5), green = std::uint32_t(g / a + 0.5), blue = std::uint32_t(b / a + 0.5);
                    if (options.ignoreWhite && red >= 240u && green >= 240u && blue >= 240u) {
                        continue;   // background
                    }
                    opaque[i] = 1;
                    rgb[i] = (red << 16) | (green << 8) | blue;
                }
            }
        }
        result.width = w;
        result.height = h;

        // The palette, from a sample of the opaque pixels.
        std::vector<std::uint32_t> samples;
        std::size_t step = std::max<std::size_t>(1, rgb.size() / 30000);
        for (std::size_t i = 0; i < rgb.size(); i += step) {
            if (opaque[i]) {
                samples.push_back(rgb[i]);
            }
        }
        if (samples.empty()) {
            return result;
        }
        std::vector<Rgb> palette = medianCut(samples, std::max(2, std::min(32, options.colors)));
        // The median cut splits by count, so a color that fills most of the picture drags its box's average toward itself; a few
        // rounds of moving each color to the average of the samples nearest to it put every color where its pixels really are.
        for (int round = 0; round < 6; ++round) {
            std::vector<double> sum(palette.size() * 3, 0.0);
            std::vector<std::size_t> n(palette.size(), 0);
            for (std::uint32_t c : samples) {
                int r = channelOf(c, 0), g = channelOf(c, 1), b = channelOf(c, 2);
                std::size_t best = 0;
                int bestDistance = 1 << 30;
                for (std::size_t k = 0; k < palette.size(); ++k) {
                    int dr = r - palette[k].r, dg = g - palette[k].g, db = b - palette[k].b;
                    int d = dr * dr + dg * dg + db * db;
                    if (d < bestDistance) {
                        bestDistance = d;
                        best = k;
                    }
                }
                sum[best * 3] += r;
                sum[best * 3 + 1] += g;
                sum[best * 3 + 2] += b;
                ++n[best];
            }
            for (std::size_t k = 0; k < palette.size(); ++k) {
                if (n[k] > 0) {
                    palette[k] = {int(sum[k * 3] / double(n[k]) + 0.5), int(sum[k * 3 + 1] / double(n[k]) + 0.5), int(sum[k * 3 + 2] / double(n[k]) + 0.5)};
                }
            }
        }
        std::vector<int> assigned(rgb.size(), -1);
        std::vector<std::size_t> counts(palette.size(), 0);
        for (std::size_t i = 0; i < rgb.size(); ++i) {
            if (!opaque[i]) {
                continue;
            }
            int r = channelOf(rgb[i], 0), g = channelOf(rgb[i], 1), b = channelOf(rgb[i], 2);
            int best = 0, bestDistance = 1 << 30;
            for (std::size_t k = 0; k < palette.size(); ++k) {
                int dr = r - palette[k].r, dg = g - palette[k].g, db = b - palette[k].b;
                int d = dr * dr + dg * dg + db * db;
                if (d < bestDistance) {
                    bestDistance = d;
                    best = int(k);
                }
            }
            assigned[i] = best;
            ++counts[std::size_t(best)];
        }
        // Largest area first: that color is the bottom layer.
        std::vector<int> order;
        for (std::size_t k = 0; k < palette.size(); ++k) {
            if (counts[k] > 0) {
                order.push_back(int(k));
            }
        }
        std::stable_sort(order.begin(), order.end(), [&counts](int a, int b) { return counts[std::size_t(a)] > counts[std::size_t(b)]; });
        std::vector<int> rank(palette.size(), -1);
        for (std::size_t i = 0; i < order.size(); ++i) {
            rank[std::size_t(order[i])] = int(i);
        }
        for (std::size_t layer = 0; layer < order.size(); ++layer) {
            std::vector<std::uint8_t> mask(rgb.size(), 0);
            std::size_t any = 0;
            for (std::size_t i = 0; i < rgb.size(); ++i) {
                if (assigned[i] >= 0 && rank[std::size_t(assigned[i])] >= int(layer)) {
                    mask[i] = 1;
                    ++any;
                }
            }
            if (any == 0) {
                continue;
            }
            TracedShape shape;
            const Rgb& c = palette[std::size_t(order[layer])];
            shape.rgb = (std::uint32_t(c.r) << 16) | (std::uint32_t(c.g) << 8) | std::uint32_t(c.b);
            for (const std::vector<BLPoint>& loop : maskOutlines(mask, w, h)) {
                if (std::fabs(polygonArea(loop)) >= options.minArea) {
                    addLoop(shape.path, loop, options.fitTolerance);
                }
            }
            if (shape.path.size() > 0) {
                result.shapes.push_back(std::move(shape));
            }
        }
        return result;
    }

    bool loadImagePixels(const std::string& path, std::vector<std::uint32_t>& pixels, int& width, int& height) {
        BLImage image;
        if (image.read_from_file(path.c_str()) != BL_SUCCESS || image.is_empty()) {
            return false;
        }
        if (image.convert(BL_FORMAT_PRGB32) != BL_SUCCESS) {
            return false;
        }
        BLImageData data;
        if (image.get_data(&data) != BL_SUCCESS || data.size.w <= 0 || data.size.h <= 0) {
            return false;
        }
        width = data.size.w;
        height = data.size.h;
        pixels.assign(std::size_t(width) * std::size_t(height), 0);
        for (int y = 0; y < height; ++y) {
            const std::uint32_t* row = reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(data.pixel_data) + std::ptrdiff_t(y) * data.stride);
            for (int x = 0; x < width; ++x) {
                std::uint32_t p = row[x];
                std::uint32_t a = p >> 24;
                if (a > 0 && a < 255) {   // premultiplied -> straight
                    std::uint32_t r = std::min<std::uint32_t>(255, ((p >> 16) & 0xFFu) * 255 / a);
                    std::uint32_t g = std::min<std::uint32_t>(255, ((p >> 8) & 0xFFu) * 255 / a);
                    std::uint32_t b = std::min<std::uint32_t>(255, (p & 0xFFu) * 255 / a);
                    p = (a << 24) | (r << 16) | (g << 8) | b;
                }
                pixels[std::size_t(y) * std::size_t(width) + std::size_t(x)] = p;
            }
        }
        return true;
    }

}

