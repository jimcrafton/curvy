#include "curvy/swatchimport.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace curvy {

    namespace {

        float clamp01(float value) {
            return std::min(1.0f, std::max(0.0f, value));
        }

        // Reads big-endian numbers and throws std::runtime_error instead of running past the end of the file.
        class Reader {
        public:
            explicit Reader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}

            std::size_t remaining() const {
                return bytes_.size() - at_;
            }

            std::uint16_t u16() {
                need(2);
                const std::uint16_t value = std::uint16_t((bytes_[at_] << 8) | bytes_[at_ + 1]);
                at_ += 2;
                return value;
            }

            std::uint32_t u32() {
                const std::uint32_t high = u16();
                return (high << 16) | u16();
            }

            float f32() {
                const std::uint32_t bits = u32();
                float value = 0.0f;
                std::memcpy(&value, &bits, sizeof(value));
                return value;
            }

            std::string tag() {
                need(4);
                const std::string text(reinterpret_cast<const char*>(&bytes_[at_]), 4);
                at_ += 4;
                return text;
            }

            // `units` UTF-16 code units, the last one usually a zero; returned as UTF-8 without the zero.
            std::string utf16(std::size_t units) {
                std::string text;
                for (std::size_t i = 0; i < units; ++i) {
                    std::uint32_t unit = u16();
                    if (unit >= 0xD800 && unit < 0xDC00 && i + 1 < units) {
                        const std::uint32_t low = u16();
                        ++i;
                        unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                    }
                    if (unit == 0) {
                        continue;
                    }
                    append(text, unit);
                }
                return text;
            }

            void skip(std::size_t count) {
                need(count);
                at_ += count;
            }

        private:
            void need(std::size_t count) const {
                if (remaining() < count) {
                    throw std::runtime_error("the file ends early");
                }
            }

            static void append(std::string& text, std::uint32_t code) {
                if (code < 0x80) {
                    text += char(code);
                } else if (code < 0x800) {
                    text += char(0xC0 | (code >> 6));
                    text += char(0x80 | (code & 0x3F));
                } else if (code < 0x10000) {
                    text += char(0xE0 | (code >> 12));
                    text += char(0x80 | ((code >> 6) & 0x3F));
                    text += char(0x80 | (code & 0x3F));
                } else {
                    text += char(0xF0 | (code >> 18));
                    text += char(0x80 | ((code >> 12) & 0x3F));
                    text += char(0x80 | ((code >> 6) & 0x3F));
                    text += char(0x80 | (code & 0x3F));
                }
            }

            const std::vector<std::uint8_t>& bytes_;
            std::size_t at_ = 0;
        };

        Swatch swatchOf(float r, float g, float b, const std::string& name) {
            Swatch swatch;
            swatch.r = clamp01(r);
            swatch.g = clamp01(g);
            swatch.b = clamp01(b);
            swatch.name = name;
            return swatch;
        }

        // The ACO colour spaces. Returns false for one that is not understood (the colour is skipped).
        bool acoColor(std::uint16_t space, std::uint16_t w, std::uint16_t x, std::uint16_t y, std::uint16_t z, const std::string& name, Swatch& out) {
            float r = 0.0f;
            float g = 0.0f;
            float b = 0.0f;
            switch (space) {
            case 0:
                out = swatchOf(w / 65535.0f, x / 65535.0f, y / 65535.0f, name);
                return true;
            case 1:
                SwatchImport::hsbToRgb(w / 65535.0f * 360.0f, x / 65535.0f, y / 65535.0f, r, g, b);
                out = swatchOf(r, g, b, name);
                return true;
            case 2:   // CMYK is stored inverted: 0 is full ink
                SwatchImport::cmykToRgb(1.0f - w / 65535.0f, 1.0f - x / 65535.0f, 1.0f - y / 65535.0f, 1.0f - z / 65535.0f, r, g, b);
                out = swatchOf(r, g, b, name);
                return true;
            case 7:
                SwatchImport::labToRgb(w / 100.0f, std::int16_t(x) / 100.0f, std::int16_t(y) / 100.0f, r, g, b);
                out = swatchOf(r, g, b, name);
                return true;
            case 8:
                out = swatchOf(w / 10000.0f, w / 10000.0f, w / 10000.0f, name);
                return true;
            default:
                return false;
            }
        }

        std::string failure(const std::exception& error) {
            return error.what();
        }

    }

    std::size_t SwatchImportResult::colorCount() const {
        std::size_t count = 0;
        for (const Palette& palette : palettes) {
            count += palette.swatches.size();
        }
        return count;
    }

    void SwatchImport::cmykToRgb(float c, float m, float y, float k, float& r, float& g, float& b) {
        r = clamp01((1.0f - clamp01(c)) * (1.0f - clamp01(k)));
        g = clamp01((1.0f - clamp01(m)) * (1.0f - clamp01(k)));
        b = clamp01((1.0f - clamp01(y)) * (1.0f - clamp01(k)));
    }

    void SwatchImport::hsbToRgb(float hueDegrees, float saturation, float brightness, float& r, float& g, float& b) {
        const float hue = std::fmod(std::fmod(hueDegrees, 360.0f) + 360.0f, 360.0f) / 60.0f;
        const int sector = int(hue) % 6;
        const float fraction = hue - std::floor(hue);
        const float s = clamp01(saturation);
        const float v = clamp01(brightness);
        const float p = v * (1.0f - s);
        const float q = v * (1.0f - s * fraction);
        const float t = v * (1.0f - s * (1.0f - fraction));
        switch (sector) {
        case 0:
            r = v;
            g = t;
            b = p;
            break;
        case 1:
            r = q;
            g = v;
            b = p;
            break;
        case 2:
            r = p;
            g = v;
            b = t;
            break;
        case 3:
            r = p;
            g = q;
            b = v;
            break;
        case 4:
            r = t;
            g = p;
            b = v;
            break;
        default:
            r = v;
            g = p;
            b = q;
            break;
        }
    }

    void SwatchImport::labToRgb(float l, float a, float b, float& r, float& g, float& blue) {
        const float fy = (l + 16.0f) / 116.0f;
        const float fx = a / 500.0f + fy;
        const float fz = fy - b / 200.0f;
        const auto inverse = [](float t) {
            const float cube = t * t * t;
            return cube > 0.008856f ? cube : (t - 16.0f / 116.0f) / 7.787f;
        };
        const float x = 0.9642f * inverse(fx);
        const float y = 1.0f * inverse(fy);
        const float z = 0.8249f * inverse(fz);
        // XYZ with a D50 white to linear sRGB (the matrix already adapts the white point).
        const float lr = 3.1338561f * x - 1.6168667f * y - 0.4906146f * z;
        const float lg = -0.9787684f * x + 1.9161415f * y + 0.0334540f * z;
        const float lb = 0.0719453f * x - 0.2289914f * y + 1.4052427f * z;
        const auto encode = [](float linear) {
            const float value = clamp01(linear);
            return value <= 0.0031308f ? 12.92f * value : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
        };
        r = encode(lr);
        g = encode(lg);
        blue = encode(lb);
    }

    SwatchImportResult SwatchImport::fromAco(const std::vector<std::uint8_t>& bytes, const std::string& fallbackName) {
        SwatchImportResult result;
        try {
            Reader reader(bytes);
            const std::uint16_t version = reader.u16();
            if (version != 1 && version != 2) {
                throw std::runtime_error("this is not an .aco swatch file");
            }
            const std::uint16_t count = reader.u16();
            std::vector<Swatch> plain;
            for (std::uint16_t i = 0; i < count; ++i) {
                const std::uint16_t space = reader.u16();
                const std::uint16_t w = reader.u16();
                const std::uint16_t x = reader.u16();
                const std::uint16_t y = reader.u16();
                const std::uint16_t z = reader.u16();
                Swatch swatch;
                if (version == 1 && acoColor(space, w, x, y, z, "", swatch)) {
                    plain.push_back(swatch);
                } else if (version == 2) {
                    // A file may hold only the second form: its names come after each colour.
                    const std::uint32_t length = reader.u32();
                    const std::string name = reader.utf16(length);
                    if (acoColor(space, w, x, y, z, name, swatch)) {
                        plain.push_back(swatch);
                    }
                }
            }
            std::vector<Swatch> named;
            if (version == 1 && reader.remaining() >= 4) {
                const std::uint16_t second = reader.u16();
                const std::uint16_t secondCount = reader.u16();
                if (second == 2) {
                    for (std::uint16_t i = 0; i < secondCount; ++i) {
                        const std::uint16_t space = reader.u16();
                        const std::uint16_t w = reader.u16();
                        const std::uint16_t x = reader.u16();
                        const std::uint16_t y = reader.u16();
                        const std::uint16_t z = reader.u16();
                        const std::uint32_t length = reader.u32();
                        const std::string name = reader.utf16(length);
                        Swatch swatch;
                        if (acoColor(space, w, x, y, z, name, swatch)) {
                            named.push_back(swatch);
                        }
                    }
                }
            }
            Palette palette;
            palette.name = fallbackName;
            palette.swatches = named.empty() ? plain : named;
            if (palette.swatches.empty()) {
                throw std::runtime_error("the file has no colours this program can read");
            }
            result.palettes.push_back(palette);
        } catch (const std::exception& error) {
            result.palettes.clear();
            result.error = failure(error);
        }
        return result;
    }

    SwatchImportResult SwatchImport::fromAse(const std::vector<std::uint8_t>& bytes, const std::string& fallbackName) {
        SwatchImportResult result;
        try {
            Reader reader(bytes);
            if (reader.tag() != "ASEF") {
                throw std::runtime_error("this is not an .ase swatch file");
            }
            reader.u16();   // version
            reader.u16();
            const std::uint32_t blocks = reader.u32();
            Palette loose;
            loose.name = fallbackName;
            Palette* group = nullptr;
            std::vector<Palette> groups;
            for (std::uint32_t i = 0; i < blocks; ++i) {
                const std::uint16_t type = reader.u16();
                const std::uint32_t length = reader.u32();
                const std::size_t blockStart = reader.remaining();
                if (type == 0xC002) {   // the end of a group
                    group = nullptr;
                } else if (type == 0xC001) {
                    groups.push_back(Palette());
                    group = &groups.back();
                    const std::string name = reader.utf16(reader.u16());
                    group->name = name.empty() ? fallbackName : name;
                } else if (type == 0x0001) {
                    const std::string name = reader.utf16(reader.u16());
                    const std::string model = reader.tag();
                    float r = 0.0f;
                    float g = 0.0f;
                    float b = 0.0f;
                    bool known = true;
                    if (model == "RGB ") {
                        r = reader.f32();
                        g = reader.f32();
                        b = reader.f32();
                    } else if (model == "CMYK") {
                        const float c = reader.f32();
                        const float m = reader.f32();
                        const float y = reader.f32();
                        const float k = reader.f32();
                        cmykToRgb(c, m, y, k, r, g, b);
                    } else if (model == "LAB ") {
                        const float l = reader.f32();
                        const float a = reader.f32();
                        const float bb = reader.f32();
                        labToRgb(l * 100.0f, a, bb, r, g, b);
                    } else if (model == "Gray") {
                        r = reader.f32();
                        g = r;
                        b = r;
                    } else {
                        known = false;   // a colour model this program does not know
                    }
                    if (known) {
                        (group != nullptr ? *group : loose).swatches.push_back(swatchOf(r, g, b, name));
                    }
                }
                // Whatever the block holds beyond what was read (the colour type, or blocks of a kind not understood) is skipped by its length.
                const std::size_t used = blockStart - reader.remaining();
                if (used < length) {
                    reader.skip(length - used);
                }
            }
            if (!loose.swatches.empty()) {
                result.palettes.push_back(loose);
            }
            for (Palette& palette : groups) {
                if (!palette.swatches.empty()) {
                    result.palettes.push_back(palette);
                }
            }
            if (result.palettes.empty()) {
                throw std::runtime_error("the file has no colours this program can read");
            }
        } catch (const std::exception& error) {
            result.palettes.clear();
            result.error = failure(error);
        }
        return result;
    }

    SwatchImportResult SwatchImport::fromFile(const std::string& path) {
        const std::filesystem::path file = std::filesystem::u8path(path);
        std::string extension = file.extension().u8string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        SwatchImportResult result;
        if (extension != ".aco" && extension != ".ase") {
            result.error = "only .aco and .ase swatch files can be imported";
            return result;
        }
        std::ifstream in(file, std::ios::binary);
        if (!in) {
            result.error = "could not read " + file.filename().u8string();
            return result;
        }
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const std::string name = file.stem().u8string();
        return extension == ".aco" ? fromAco(bytes, name) : fromAse(bytes, name);
    }

}
