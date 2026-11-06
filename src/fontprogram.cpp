#include "curvy/fontprogram.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <sstream>
#include <utility>
#include <vector>

namespace curvy {

    namespace {

        // ---- bytes -----------------------------------------------------------------------------------------------------------------------

        // Big-endian reads that never leave the data: whatever lies outside reads as zero.
        class Reader {
        public:
            Reader(const std::string& data) : data_(data) {}
            std::size_t size() const { return data_.size(); }
            unsigned u8(std::size_t at) const { return at < data_.size() ? (unsigned char)data_[at] : 0u; }
            unsigned u16(std::size_t at) const { return (u8(at) << 8) | u8(at + 1); }
            int s16(std::size_t at) const { return int(std::int16_t(u16(at))); }
            std::uint32_t u32(std::size_t at) const { return (std::uint32_t(u16(at)) << 16) | u16(at + 2); }

        private:
            const std::string& data_;
        };

        struct Span {
            std::size_t offset = 0, length = 0;
        };

        // ---- glyph names -----------------------------------------------------------------------------------------------------------------

        // The CFF standard strings 0 - 228 (the ISOAdobe character set), which is all a subset font's glyph names use.
        const std::vector<std::string>& standardStrings() {
            static const std::vector<std::string> names = [] {
                const char* text =
                    ".notdef space exclam quotedbl numbersign dollar percent ampersand quoteright parenleft parenright asterisk plus comma hyphen period slash "
                    "zero one two three four five six seven eight nine colon semicolon less equal greater question at "
                    "A B C D E F G H I J K L M N O P Q R S T U V W X Y Z bracketleft backslash bracketright asciicircum underscore quoteleft "
                    "a b c d e f g h i j k l m n o p q r s t u v w x y z braceleft bar braceright asciitilde "
                    "exclamdown cent sterling fraction yen florin section currency quotesingle quotedblleft guillemotleft guilsinglleft guilsinglright fi fl "
                    "endash dagger daggerdbl periodcentered paragraph bullet quotesinglbase quotedblbase quotedblright guillemotright ellipsis perthousand questiondown "
                    "grave acute circumflex tilde macron breve dotaccent dieresis ring cedilla hungarumlaut ogonek caron emdash "
                    "AE ordfeminine Lslash Oslash OE ordmasculine ae dotlessi lslash oslash oe germandbls "
                    "onesuperior logicalnot mu trademark Eth onehalf plusminus Thorn onequarter divide brokenbar degree thorn threequarters twosuperior registered minus eth multiply threesuperior copyright "
                    "Aacute Acircumflex Adieresis Agrave Aring Atilde Ccedilla Eacute Ecircumflex Edieresis Egrave Iacute Icircumflex Idieresis Igrave Ntilde "
                    "Oacute Ocircumflex Odieresis Ograve Otilde Scaron Uacute Ucircumflex Udieresis Ugrave Yacute Ydieresis Zcaron "
                    "aacute acircumflex adieresis agrave aring atilde ccedilla eacute ecircumflex edieresis egrave iacute icircumflex idieresis igrave ntilde "
                    "oacute ocircumflex odieresis ograve otilde scaron uacute ucircumflex udieresis ugrave yacute ydieresis zcaron";
                std::vector<std::string> out;
                std::istringstream in(text);
                for (std::string word; in >> word;) { out.push_back(word); }
                return out;
            }();
            return names;
        }

        // ---- sfnt ------------------------------------------------------------------------------------------------------------------------

        struct Sfnt {
            std::map<std::string, Span> tables;

            bool parse(const Reader& r) {
                const std::uint32_t tag = r.u32(0);
                if (tag != 0x00010000u && tag != 0x74727565u && tag != 0x4F54544Fu) { return false; }   // 1.0, 'true', 'OTTO'
                const unsigned count = r.u16(4);
                for (unsigned i = 0; i < count && i < 256; ++i) {
                    const std::size_t record = 12 + 16 * std::size_t(i);
                    std::string name;
                    for (int k = 0; k < 4; ++k) { name += char(r.u8(record + std::size_t(k))); }
                    Span span;
                    span.offset = r.u32(record + 8);
                    span.length = r.u32(record + 12);
                    if (span.offset > r.size()) { continue; }
                    span.length = std::min(span.length, r.size() - span.offset);
                    tables[name] = span;
                }
                return !tables.empty();
            }
            const Span* find(const char* name) const {
                auto it = tables.find(name);
                return it == tables.end() ? nullptr : &it->second;
            }
        };

        class Cmap {
        public:
            void load(const Reader& r, const Span& table) {
                subtables_.clear();
                const unsigned count = r.u16(table.offset + 2);
                for (unsigned i = 0; i < count && i < 64; ++i) {
                    const std::size_t record = table.offset + 4 + 8 * std::size_t(i);
                    Sub sub;
                    sub.platform = r.u16(record);
                    sub.encoding = r.u16(record + 2);
                    sub.offset = table.offset + r.u32(record + 4);
                    if (sub.offset < r.size()) { subtables_.push_back(sub); }
                }
            }
            bool empty() const { return subtables_.empty(); }

            unsigned unicode(const Reader& r, unsigned cp) const {
                static const unsigned preference[][2] = {{3, 10}, {0, 4}, {0, 6}, {3, 1}, {0, 3}, {0, 2}, {0, 1}, {0, 0}};
                for (const auto& want : preference) {
                    for (const Sub& sub : subtables_) {
                        if (sub.platform == want[0] && sub.encoding == want[1]) {
                            if (const unsigned g = lookup(r, sub.offset, cp)) { return g; }
                        }
                    }
                }
                return 0;
            }

            unsigned symbolic(const Reader& r, unsigned code) const {
                for (const Sub& sub : subtables_) {
                    if (sub.platform == 3 && sub.encoding == 0) {
                        for (unsigned base : {0u, 0xF000u, 0xF100u, 0xF200u}) {
                            if (const unsigned g = lookup(r, sub.offset, base + code)) { return g; }
                        }
                    }
                }
                for (const Sub& sub : subtables_) {
                    if (sub.platform == 1 && sub.encoding == 0) {
                        if (const unsigned g = lookup(r, sub.offset, code)) { return g; }
                    }
                }
                return 0;
            }

        private:
            struct Sub {
                unsigned platform = 0, encoding = 0;
                std::size_t offset = 0;
            };

            static unsigned lookup(const Reader& r, std::size_t at, unsigned code) {
                switch (r.u16(at)) {
                    case 0: return code < 256 ? r.u8(at + 6 + code) : 0u;
                    case 4: {
                        const std::size_t segs = r.u16(at + 6) / 2;
                        const std::size_t ends = at + 14, starts = at + 16 + 2 * segs, deltas = starts + 2 * segs, ranges = deltas + 2 * segs;
                        for (std::size_t i = 0; i < segs; ++i) {
                            if (code > r.u16(ends + 2 * i)) { continue; }
                            const unsigned start = r.u16(starts + 2 * i);
                            if (code < start) { return 0; }
                            const unsigned range = r.u16(ranges + 2 * i), delta = r.u16(deltas + 2 * i);
                            unsigned glyph = range == 0 ? code : r.u16(ranges + 2 * i + range + 2 * (code - start));
                            if (range == 0 || glyph != 0) { glyph = (glyph + delta) & 0xFFFFu; }
                            return glyph;
                        }
                        return 0;
                    }
                    case 6: {
                        const unsigned first = r.u16(at + 6), count = r.u16(at + 8);
                        return code >= first && code < first + count ? r.u16(at + 10 + 2 * std::size_t(code - first)) : 0u;
                    }
                    case 12: {
                        const std::uint32_t groups = r.u32(at + 12);
                        for (std::uint32_t i = 0; i < groups && i < 100000; ++i) {
                            const std::size_t group = at + 16 + 12 * std::size_t(i);
                            const std::uint32_t start = r.u32(group), end = r.u32(group + 4);
                            if (code >= start && code <= end) { return unsigned(r.u32(group + 8) + (code - start)); }
                        }
                        return 0;
                    }
                    default: return 0;
                }
            }

            std::vector<Sub> subtables_;
        };

        // ---- TrueType outlines -----------------------------------------------------------------------------------------------------------

        class TrueTypeProgram : public FontProgram {
        public:
            TrueTypeProgram(std::string data, const Sfnt& sfnt) : data_(std::move(data)), r_(data_) {
                const Span* head = sfnt.find("head");
                const Span* maxp = sfnt.find("maxp");
                const Span* loca = sfnt.find("loca");
                const Span* glyf = sfnt.find("glyf");
                if (head == nullptr || maxp == nullptr || loca == nullptr || glyf == nullptr) { return; }
                unitsPerEm_ = std::max(16u, r_.u16(head->offset + 18));
                longLoca_ = r_.s16(head->offset + 50) != 0;
                glyphs_ = r_.u16(maxp->offset + 4);
                loca_ = loca->offset;
                glyf_ = *glyf;
                const std::size_t entries = loca->length / (longLoca_ ? 4 : 2);
                if (entries > 0) { glyphs_ = std::min<std::size_t>(glyphs_, entries - 1); }
                if (const Span* cmap = sfnt.find("cmap")) { cmap_.load(r_, *cmap); }
                valid_ = glyphs_ > 0;
            }

            bool valid() const { return valid_; }
            unsigned glyphCount() const override { return unsigned(glyphs_); }

            bool outline(unsigned glyph, BLPath& out) const override {
                BLPath units;
                if (!build(glyph, units, 0)) { return false; }
                const double s = 1.0 / double(unitsPerEm_);
                out.add_path(units, BLMatrix2D(s, 0.0, 0.0, s, 0.0, 0.0));
                return true;
            }
            unsigned glyphForUnicode(unsigned cp) const override { return cmap_.unicode(r_, cp); }
            unsigned glyphForCode(unsigned code) const override { return cmap_.symbolic(r_, code); }
            unsigned glyphForCid(unsigned cid) const override { return cid < glyphs_ ? cid : 0u; }

        private:
            struct Point {
                double x, y;
                bool on;
            };

            std::size_t offsetOf(unsigned glyph) const {
                return glyf_.offset + (longLoca_ ? r_.u32(loca_ + 4 * std::size_t(glyph)) : 2 * std::size_t(r_.u16(loca_ + 2 * std::size_t(glyph))));
            }

            bool build(unsigned glyph, BLPath& out, int depth) const {
                if (!valid_ || glyph >= glyphs_ || depth > 6) { return false; }
                const std::size_t begin = offsetOf(glyph), end = glyf_.offset + (longLoca_ ? r_.u32(loca_ + 4 * (std::size_t(glyph) + 1)) : 2 * std::size_t(r_.u16(loca_ + 2 * (std::size_t(glyph) + 1))));
                if (end <= begin || begin + 10 > glyf_.offset + glyf_.length) { return true; }   // an empty glyph (a space)
                const int contours = r_.s16(begin);
                return contours >= 0 ? simple(begin, unsigned(contours), out) : composite(begin + 10, out, depth);
            }

            bool simple(std::size_t at, unsigned contours, BLPath& out) const {
                std::vector<unsigned> ends;
                for (unsigned i = 0; i < contours; ++i) { ends.push_back(r_.u16(at + 10 + 2 * std::size_t(i))); }
                if (ends.empty()) { return true; }
                const std::size_t count = std::size_t(ends.back()) + 1;
                if (count > 20000) { return false; }
                std::size_t p = at + 10 + 2 * std::size_t(contours);
                p += 2 + r_.u16(p);
                std::vector<unsigned> flags;
                while (flags.size() < count) {
                    const unsigned f = r_.u8(p++);
                    flags.push_back(f);
                    if (f & 8) {
                        for (unsigned repeat = r_.u8(p++); repeat > 0 && flags.size() < count; --repeat) { flags.push_back(f); }
                    }
                }
                std::vector<Point> points(count, Point{0.0, 0.0, false});
                int value = 0;
                for (std::size_t i = 0; i < count; ++i) {
                    const unsigned f = flags[i];
                    if (f & 2) { const int d = int(r_.u8(p++)); value += (f & 16) ? d : -d; }
                    else if (!(f & 16)) { value += r_.s16(p); p += 2; }
                    points[i].x = value;
                    points[i].on = (f & 1) != 0;
                }
                value = 0;
                for (std::size_t i = 0; i < count; ++i) {
                    const unsigned f = flags[i];
                    if (f & 4) { const int d = int(r_.u8(p++)); value += (f & 32) ? d : -d; }
                    else if (!(f & 32)) { value += r_.s16(p); p += 2; }
                    points[i].y = value;
                }
                std::size_t first = 0;
                for (unsigned end : ends) {
                    if (end >= first && end < count) { contour(points, first, end, out); }
                    first = std::size_t(end) + 1;
                }
                return true;
            }

            static void contour(const std::vector<Point>& all, std::size_t first, std::size_t last, BLPath& out) {
                std::vector<Point> ring;
                const std::size_t n = last - first + 1;
                if (n < 2) { return; }
                std::size_t start = n;
                for (std::size_t i = 0; i < n && start == n; ++i) { if (all[first + i].on) { start = i; } }
                if (start == n) {   // no on-curve point at all: start in the middle of the first and last
                    const Point a = all[first], b = all[last];
                    ring.push_back(Point{(a.x + b.x) / 2, (a.y + b.y) / 2, true});
                    for (std::size_t i = 0; i < n; ++i) { ring.push_back(all[first + i]); }
                } else {
                    for (std::size_t i = 0; i < n; ++i) { ring.push_back(all[first + (start + i) % n]); }
                }
                const std::size_t len = ring.size();
                out.move_to(ring[0].x, ring[0].y);
                std::size_t i = 1;
                while (i < len) {
                    const Point cur = ring[i];
                    if (cur.on) {
                        out.line_to(cur.x, cur.y);
                        ++i;
                        continue;
                    }
                    const Point next = ring[(i + 1) % len];
                    if (next.on) {
                        out.quad_to(cur.x, cur.y, next.x, next.y);
                        i += 2;
                    } else {
                        out.quad_to(cur.x, cur.y, (cur.x + next.x) / 2, (cur.y + next.y) / 2);
                        ++i;
                    }
                }
                out.close();
            }

            bool composite(std::size_t at, BLPath& out, int depth) const {
                for (int guard = 0; guard < 64; ++guard) {
                    const unsigned flags = r_.u16(at), glyph = r_.u16(at + 2);
                    at += 4;
                    double dx = 0.0, dy = 0.0;
                    if (flags & 1) { dx = r_.s16(at); dy = r_.s16(at + 2); at += 4; }
                    else { dx = double(std::int8_t(r_.u8(at))); dy = double(std::int8_t(r_.u8(at + 1))); at += 2; }
                    if (!(flags & 2)) { dx = dy = 0.0; }   // point matching is not supported
                    auto fixed = [&](std::size_t o) { return double(r_.s16(o)) / 16384.0; };
                    double a = 1.0, b = 0.0, c = 0.0, d = 1.0;
                    if (flags & 8) { a = d = fixed(at); at += 2; }
                    else if (flags & 0x40) { a = fixed(at); d = fixed(at + 2); at += 4; }
                    else if (flags & 0x80) { a = fixed(at); b = fixed(at + 2); c = fixed(at + 4); d = fixed(at + 6); at += 8; }
                    BLPath part;
                    if (build(glyph, part, depth + 1)) { out.add_path(part, BLMatrix2D(a, b, c, d, dx, dy)); }
                    if (!(flags & 0x20)) { break; }
                }
                return true;
            }

            std::string data_;
            Reader r_;
            Cmap cmap_;
            Span glyf_;
            std::size_t loca_ = 0, glyphs_ = 0;
            unsigned unitsPerEm_ = 1000;
            bool longLoca_ = false, valid_ = false;
        };

        // ---- CFF outlines ----------------------------------------------------------------------------------------------------------------

        class CffProgram : public FontProgram {
        public:
            // `table` is the CFF data inside `data`; `sfnt` is given when the data is an OpenType file whose cmap should be used.
            CffProgram(std::string data, Span table, const Sfnt* sfnt) : data_(std::move(data)), r_(data_), base_(table.offset), end_(table.offset + table.length) {
                if (sfnt != nullptr) {
                    if (const Span* cmap = sfnt->find("cmap")) { cmap_.load(r_, *cmap); }
                }
                valid_ = parse();
            }

            bool valid() const { return valid_; }
            unsigned glyphCount() const override { return unsigned(charStrings_.count); }

            bool outline(unsigned glyph, BLPath& out) const override {
                if (!valid_ || glyph >= charStrings_.count) { return false; }
                const Span code = item(charStrings_, glyph);
                const std::size_t fd = fdOf(glyph);
                Machine m(*this, fd < privates_.size() ? &privates_[fd] : nullptr);
                m.run(code, 0);
                m.finish();
                const double s = scale_;
                out.add_path(m.path, BLMatrix2D(s, 0.0, 0.0, s, 0.0, 0.0));
                return true;
            }

            unsigned glyphForUnicode(unsigned cp) const override {
                if (!cmap_.empty()) {
                    if (const unsigned g = cmap_.unicode(r_, cp)) { return g; }
                }
                if (cid_) { return 0; }
                names();
                auto it = byUnicode_.find(cp);
                return it == byUnicode_.end() ? 0u : it->second;
            }

            unsigned glyphForCode(unsigned code) const override {
                if (!cid_ && encodingOffset_ > 1) {
                    auto it = encoding_.find(code);
                    if (it != encoding_.end()) { return it->second; }
                    return 0;
                }
                if (!cmap_.empty()) {
                    if (const unsigned g = cmap_.symbolic(r_, code)) { return g; }
                }
                return glyphForUnicode(code);   // the standard encoding agrees with ASCII for letters and digits
            }

            unsigned glyphForName(const std::string& name) const override {
                if (cid_) { return 0; }
                names();
                auto it = byName_.find(name);
                return it == byName_.end() ? 0u : it->second;
            }

            unsigned glyphForCid(unsigned cid) const override {
                if (!cid_) { return cid < charStrings_.count ? cid : 0u; }
                auto it = byCid_.find(cid);
                return it == byCid_.end() ? 0u : it->second;
            }

        private:
            struct Index {
                std::size_t count = 0, offsetsAt = 0, dataAt = 0;
                unsigned offSize = 0;
            };

            struct Private {
                Index subrs;
                bool hasSubrs = false;
            };

            Index readIndex(std::size_t at, std::size_t& after) const {
                Index index;
                index.count = r_.u16(at);
                if (index.count == 0) { after = at + 2; return index; }
                index.offSize = r_.u8(at + 2);
                if (index.offSize < 1 || index.offSize > 4) { index.count = 0; after = at + 2; return index; }
                index.offsetsAt = at + 3;
                index.dataAt = index.offsetsAt + (index.count + 1) * index.offSize - 1;
                after = index.dataAt + offsetAt(index, index.count);
                return index;
            }

            std::size_t offsetAt(const Index& index, std::size_t i) const {
                std::size_t v = 0;
                for (unsigned k = 0; k < index.offSize; ++k) { v = (v << 8) | r_.u8(index.offsetsAt + i * index.offSize + k); }
                return v;
            }

            Span item(const Index& index, std::size_t i) const {
                Span s;
                if (i >= index.count) { return s; }
                const std::size_t a = offsetAt(index, i), b = offsetAt(index, i + 1);
                if (b < a || index.dataAt + b > end_) { return s; }
                s.offset = index.dataAt + a;
                s.length = b - a;
                return s;
            }

            // A DICT as operator -> operands; two-byte operators (12 n) are keyed 1200 + n.
            std::map<int, std::vector<double>> readDict(Span span) const {
                std::map<int, std::vector<double>> dict;
                std::vector<double> operands;
                const std::size_t end = span.offset + span.length;
                for (std::size_t i = span.offset; i < end;) {
                    const unsigned b = r_.u8(i++);
                    if (b <= 21) {
                        int op = int(b);
                        if (b == 12) { op = 1200 + int(r_.u8(i++)); }
                        dict[op] = operands;
                        operands.clear();
                    } else if (b == 28) { operands.push_back(r_.s16(i)); i += 2;
                    } else if (b == 29) { operands.push_back(double(std::int32_t(r_.u32(i)))); i += 4;
                    } else if (b == 30) {
                        std::string text;
                        for (bool done = false; !done && i < end;) {
                            const unsigned byte = r_.u8(i++);
                            for (unsigned nibble : {byte >> 4, byte & 15u}) {
                                if (nibble <= 9) { text += char('0' + nibble); }
                                else if (nibble == 10) { text += '.'; }
                                else if (nibble == 11) { text += 'E'; }
                                else if (nibble == 12) { text += "E-"; }
                                else if (nibble == 14) { text += '-'; }
                                else if (nibble == 15) { done = true; break; }
                            }
                        }
                        operands.push_back(std::atof(text.c_str()));
                    } else if (b >= 32 && b <= 246) { operands.push_back(double(int(b) - 139));
                    } else if (b >= 247 && b <= 250) { operands.push_back(double((int(b) - 247) * 256 + int(r_.u8(i++)) + 108));
                    } else if (b >= 251 && b <= 254) { operands.push_back(double(-(int(b) - 251) * 256 - int(r_.u8(i++)) - 108));
                    }
                }
                return dict;
            }

            Private readPrivate(const std::map<int, std::vector<double>>& dict) const {
                Private p;
                auto it = dict.find(18);
                if (it == dict.end() || it->second.size() < 2) { return p; }
                const std::size_t size = std::size_t(it->second[0]), offset = base_ + std::size_t(it->second[1]);
                const auto priv = readDict(Span{offset, size});
                auto subrs = priv.find(19);
                if (subrs != priv.end() && !subrs->second.empty()) {
                    std::size_t after = 0;
                    p.subrs = readIndex(offset + std::size_t(subrs->second[0]), after);
                    p.hasSubrs = true;
                }
                return p;
            }

            bool parse() {
                if (r_.u8(base_) != 1 || end_ > r_.size()) { return false; }
                std::size_t at = base_ + r_.u8(base_ + 2), after = 0;
                readIndex(at, after);   // the font names
                const Index tops = readIndex(after, after);
                strings_ = readIndex(after, after);
                gsubrs_ = readIndex(after, after);
                if (tops.count == 0) { return false; }
                const auto top = readDict(item(tops, 0));
                auto cs = top.find(17);
                if (cs == top.end() || cs->second.empty()) { return false; }
                charStrings_ = readIndex(base_ + std::size_t(cs->second[0]), after);
                if (charStrings_.count == 0) { return false; }
                auto matrix = top.find(1207);
                if (matrix != top.end() && !matrix->second.empty() && matrix->second[0] > 0.0) { scale_ = matrix->second[0]; }
                cid_ = top.count(1230) > 0;
                auto charset = top.find(15);
                readCharset(charset == top.end() || charset->second.empty() ? 0 : std::size_t(charset->second[0]));
                if (cid_) {
                    auto array = top.find(1236);
                    if (array != top.end() && !array->second.empty()) {
                        const Index fds = readIndex(base_ + std::size_t(array->second[0]), after);
                        for (std::size_t i = 0; i < fds.count; ++i) { privates_.push_back(readPrivate(readDict(item(fds, i)))); }
                    }
                    auto select = top.find(1237);
                    if (select != top.end() && !select->second.empty()) { readFdSelect(base_ + std::size_t(select->second[0])); }
                } else {
                    privates_.push_back(readPrivate(top));
                    auto encoding = top.find(16);
                    encodingOffset_ = encoding == top.end() || encoding->second.empty() ? 0 : std::size_t(encoding->second[0]);
                    if (encodingOffset_ > 1) { readEncoding(base_ + encodingOffset_); }
                }
                return true;
            }

            void readCharset(std::size_t offset) {
                charset_.assign(charStrings_.count, 0);
                if (offset <= 2) {
                    for (std::size_t g = 0; g < charset_.size(); ++g) { charset_[g] = unsigned(g); }   // the predefined sets, taken as ISOAdobe
                } else {
                    std::size_t p = base_ + offset;
                    const unsigned format = r_.u8(p++);
                    std::size_t g = 1;
                    if (format == 0) {
                        for (; g < charset_.size(); ++g, p += 2) { charset_[g] = r_.u16(p); }
                    } else {
                        while (g < charset_.size() && p < end_) {
                            const unsigned first = r_.u16(p);
                            const unsigned left = format == 1 ? r_.u8(p + 2) : r_.u16(p + 2);
                            p += format == 1 ? 3 : 4;
                            for (unsigned k = 0; k <= left && g < charset_.size(); ++k, ++g) { charset_[g] = first + k; }
                        }
                    }
                }
                if (cid_) {
                    for (std::size_t g = 0; g < charset_.size(); ++g) { byCid_.emplace(charset_[g], unsigned(g)); }
                }
            }

            void readFdSelect(std::size_t at) {
                fdSelect_.assign(charStrings_.count, 0);
                const unsigned format = r_.u8(at);
                if (format == 0) {
                    for (std::size_t g = 0; g < fdSelect_.size(); ++g) { fdSelect_[g] = (unsigned char)r_.u8(at + 1 + g); }
                } else if (format == 3) {
                    const unsigned ranges = r_.u16(at + 1);
                    for (unsigned i = 0; i < ranges; ++i) {
                        const std::size_t rec = at + 3 + 3 * std::size_t(i);
                        const unsigned first = r_.u16(rec), fd = r_.u8(rec + 2), next = r_.u16(rec + 3);
                        for (unsigned g = first; g < next && g < fdSelect_.size(); ++g) { fdSelect_[g] = (unsigned char)fd; }
                    }
                }
            }

            void readEncoding(std::size_t at) {
                const unsigned format = r_.u8(at) & 0x7F;
                if (format == 0) {
                    const unsigned n = r_.u8(at + 1);
                    for (unsigned i = 0; i < n; ++i) { encoding_[r_.u8(at + 2 + i)] = i + 1; }
                } else if (format == 1) {
                    const unsigned n = r_.u8(at + 1);
                    unsigned glyph = 1;
                    for (unsigned i = 0; i < n; ++i) {
                        const unsigned first = r_.u8(at + 2 + 2 * i), left = r_.u8(at + 3 + 2 * i);
                        for (unsigned k = 0; k <= left; ++k) { encoding_[first + k] = glyph++; }
                    }
                }
            }

            std::size_t fdOf(unsigned glyph) const { return glyph < fdSelect_.size() ? fdSelect_[glyph] : 0u; }

            std::string nameOfSid(unsigned sid) const {
                const auto& standard = standardStrings();
                if (sid < standard.size()) { return standard[sid]; }
                if (sid < 391) { return std::string(); }
                const Span s = item(strings_, sid - 391);
                return s.length == 0 ? std::string() : data_.substr(s.offset, s.length);
            }

            void names() const {
                if (namesRead_) { return; }
                namesRead_ = true;
                for (std::size_t g = 1; g < charset_.size(); ++g) {
                    const std::string name = nameOfSid(charset_[g]);
                    if (name.empty()) { continue; }
                    byName_.emplace(name, unsigned(g));
                    const unsigned u = glyphNameToUnicode(name);
                    if (u != 0) { byUnicode_.emplace(u, unsigned(g)); }
                }
            }

            // The Type 2 charstring interpreter.
            struct Machine {
                Machine(const CffProgram& font, const Private* priv) : f(font), local(priv) {}

                const CffProgram& f;
                const Private* local;
                BLPath path;
                std::vector<double> st;
                double x = 0.0, y = 0.0;
                int hints = 0;
                bool widthSeen = false, open = false, done = false;

                void finish() { if (open) { path.close(); open = false; } }

                static int bias(std::size_t count) { return count < 1240 ? 107 : (count < 33900 ? 1131 : 32768); }

                void moveTo(double dx, double dy) {
                    if (open) { path.close(); }
                    x += dx;
                    y += dy;
                    path.move_to(x, y);
                    open = true;
                }
                void lineTo(double dx, double dy) {
                    if (!open) { path.move_to(x, y); open = true; }
                    x += dx;
                    y += dy;
                    path.line_to(x, y);
                }
                void curveTo(double a, double b, double c, double d, double e, double g) {
                    if (!open) { path.move_to(x, y); open = true; }
                    const double x1 = x + a, y1 = y + b, x2 = x1 + c, y2 = y1 + d;
                    x = x2 + e;
                    y = y2 + g;
                    path.cubic_to(x1, y1, x2, y2, x, y);
                }

                // The first operator that clears the stack may carry the glyph width as an extra leading operand.
                void takeWidth(bool extra) {
                    if (!widthSeen && extra && !st.empty()) { st.erase(st.begin()); }
                    widthSeen = true;
                }

                void run(Span code, int depth) {
                    if (depth > 10) { return; }
                    const std::size_t end = code.offset + code.length;
                    std::size_t i = code.offset;
                    while (i < end && !done) {
                        const unsigned b = f.r_.u8(i++);
                        if (b >= 32 || b == 28) {
                            double v;
                            if (b == 28) { v = f.r_.s16(i); i += 2; }
                            else if (b <= 246) { v = double(int(b) - 139); }
                            else if (b <= 250) { v = double((int(b) - 247) * 256 + int(f.r_.u8(i++)) + 108); }
                            else if (b <= 254) { v = double(-(int(b) - 251) * 256 - int(f.r_.u8(i++)) - 108); }
                            else { v = double(std::int32_t(f.r_.u32(i))) / 65536.0; i += 4; }
                            if (st.size() < 96) { st.push_back(v); }
                            continue;
                        }
                        const std::size_t n = st.size();
                        switch (b) {
                            case 1: case 3: case 18: case 23:
                                takeWidth(n % 2 == 1);
                                hints += int(st.size() / 2);
                                st.clear();
                                break;
                            case 19: case 20:
                                takeWidth(n % 2 == 1);
                                hints += int(st.size() / 2);
                                st.clear();
                                i += std::size_t(hints + 7) / 8;
                                break;
                            case 21: takeWidth(n > 2); if (st.size() >= 2) { moveTo(st[0], st[1]); } st.clear(); break;
                            case 22: takeWidth(n > 1); if (!st.empty()) { moveTo(st[0], 0.0); } st.clear(); break;
                            case 4: takeWidth(n > 1); if (!st.empty()) { moveTo(0.0, st[0]); } st.clear(); break;
                            case 5: for (std::size_t k = 0; k + 1 < n; k += 2) { lineTo(st[k], st[k + 1]); } st.clear(); break;
                            case 6: case 7: {
                                bool horizontal = b == 6;
                                for (std::size_t k = 0; k < n; ++k, horizontal = !horizontal) { horizontal ? lineTo(st[k], 0.0) : lineTo(0.0, st[k]); }
                                st.clear();
                                break;
                            }
                            case 8: for (std::size_t k = 0; k + 5 < n; k += 6) { curveTo(st[k], st[k + 1], st[k + 2], st[k + 3], st[k + 4], st[k + 5]); } st.clear(); break;
                            case 24: {
                                std::size_t k = 0;
                                for (; k + 5 < n && n - k >= 8; k += 6) { curveTo(st[k], st[k + 1], st[k + 2], st[k + 3], st[k + 4], st[k + 5]); }
                                if (k + 1 < n) { lineTo(st[k], st[k + 1]); }
                                st.clear();
                                break;
                            }
                            case 25: {
                                std::size_t k = 0;
                                for (; k + 1 < n && n - k >= 8; k += 2) { lineTo(st[k], st[k + 1]); }
                                if (k + 5 < n) { curveTo(st[k], st[k + 1], st[k + 2], st[k + 3], st[k + 4], st[k + 5]); }
                                st.clear();
                                break;
                            }
                            case 26: {
                                std::size_t k = 0;
                                double dx1 = 0.0;
                                if (n % 2 == 1) { dx1 = st[k++]; }
                                for (; k + 3 < n; k += 4) { curveTo(dx1, st[k], st[k + 1], st[k + 2], 0.0, st[k + 3]); dx1 = 0.0; }
                                st.clear();
                                break;
                            }
                            case 27: {
                                std::size_t k = 0;
                                double dy1 = 0.0;
                                if (n % 2 == 1) { dy1 = st[k++]; }
                                for (; k + 3 < n; k += 4) { curveTo(st[k], dy1, st[k + 1], st[k + 2], st[k + 3], 0.0); dy1 = 0.0; }
                                st.clear();
                                break;
                            }
                            case 30: case 31: {
                                bool horizontal = b == 31;
                                for (std::size_t k = 0; k + 3 < n; k += 4, horizontal = !horizontal) {
                                    const double last = n - k == 5 ? st[k + 4] : 0.0;
                                    if (horizontal) { curveTo(st[k], 0.0, st[k + 1], st[k + 2], last, st[k + 3]); }
                                    else { curveTo(0.0, st[k], st[k + 1], st[k + 2], st[k + 3], last); }
                                }
                                st.clear();
                                break;
                            }
                            case 10: case 29: {
                                if (st.empty()) { break; }
                                const bool global = b == 29;
                                const Index& subrs = global ? f.gsubrs_ : (local != nullptr && local->hasSubrs ? local->subrs : f.gsubrs_);
                                const double number = st.back() + double(bias(subrs.count));
                                st.pop_back();
                                if (number >= 0 && std::size_t(number) < subrs.count) { run(f.item(subrs, std::size_t(number)), depth + 1); }
                                break;
                            }
                            case 11: return;
                            case 14: takeWidth(n == 1 || n == 5); st.clear(); finish(); done = true; break;
                            case 12: {
                                const unsigned b1 = f.r_.u8(i++);
                                if (b1 == 35 && n >= 12) {
                                    curveTo(st[0], st[1], st[2], st[3], st[4], st[5]);
                                    curveTo(st[6], st[7], st[8], st[9], st[10], st[11]);
                                } else if (b1 == 34 && n >= 7) {
                                    curveTo(st[0], 0.0, st[1], st[2], st[3], 0.0);
                                    curveTo(st[4], 0.0, st[5], -st[2], st[6], 0.0);
                                } else if (b1 == 36 && n >= 9) {
                                    curveTo(st[0], st[1], st[2], st[3], st[4], 0.0);
                                    curveTo(st[5], 0.0, st[6], st[7], st[8], -(st[1] + st[3] + st[7]));
                                } else if (b1 == 37 && n >= 11) {
                                    const double dx = st[0] + st[2] + st[4] + st[6] + st[8], dy = st[1] + st[3] + st[5] + st[7] + st[9];
                                    curveTo(st[0], st[1], st[2], st[3], st[4], st[5]);
                                    if (std::fabs(dx) > std::fabs(dy)) { curveTo(st[6], st[7], st[8], st[9], st[10], -dy); }
                                    else { curveTo(st[6], st[7], st[8], st[9], -dx, st[10]); }
                                }
                                st.clear();
                                break;
                            }
                            default: st.clear(); break;
                        }
                    }
                }
            };

            std::string data_;
            Reader r_;
            std::size_t base_ = 0, end_ = 0;
            Cmap cmap_;
            Index strings_, gsubrs_, charStrings_;
            std::vector<Private> privates_;
            std::vector<unsigned> charset_;
            std::vector<unsigned char> fdSelect_;
            std::map<unsigned, unsigned> byCid_, encoding_;
            std::size_t encodingOffset_ = 0;
            double scale_ = 0.001;
            bool cid_ = false, valid_ = false;
            mutable bool namesRead_ = false;
            mutable std::map<unsigned, unsigned> byUnicode_;
            mutable std::map<std::string, unsigned> byName_;
        };

    }

    std::shared_ptr<FontProgram> FontProgram::load(const std::string& data) {
        if (data.size() < 8) { return nullptr; }
        try {
            const Reader r(data);
            Sfnt sfnt;
            if (sfnt.parse(r)) {
                if (sfnt.find("glyf") != nullptr) {
                    auto font = std::make_shared<TrueTypeProgram>(data, sfnt);
                    return font->valid() ? font : nullptr;
                }
                if (const Span* cff = sfnt.find("CFF ")) {
                    auto font = std::make_shared<CffProgram>(data, *cff, &sfnt);
                    return font->valid() ? font : nullptr;
                }
                return nullptr;
            }
            if (r.u8(0) == 1 && r.u8(2) >= 4) {   // bare CFF: major version 1, header size
                auto font = std::make_shared<CffProgram>(data, Span{0, data.size()}, nullptr);
                return font->valid() ? font : nullptr;
            }
        } catch (...) {
        }
        return nullptr;
    }

    unsigned glyphNameToUnicode(const std::string& name) {
        static const std::map<std::string, unsigned> names = {
            {"space", 32}, {"exclam", 33}, {"quotedbl", 34}, {"numbersign", 35}, {"dollar", 36}, {"percent", 37}, {"ampersand", 38},
            {"quotesingle", 39}, {"parenleft", 40}, {"parenright", 41}, {"asterisk", 42}, {"plus", 43}, {"comma", 44}, {"hyphen", 45},
            {"period", 46}, {"slash", 47}, {"zero", 48}, {"one", 49}, {"two", 50}, {"three", 51}, {"four", 52}, {"five", 53}, {"six", 54},
            {"seven", 55}, {"eight", 56}, {"nine", 57}, {"colon", 58}, {"semicolon", 59}, {"less", 60}, {"equal", 61}, {"greater", 62},
            {"question", 63}, {"at", 64}, {"bracketleft", 91}, {"backslash", 92}, {"bracketright", 93}, {"asciicircum", 94},
            {"underscore", 95}, {"grave", 96}, {"braceleft", 123}, {"bar", 124}, {"braceright", 125}, {"asciitilde", 126},
            {"bullet", 0x2022}, {"endash", 0x2013}, {"emdash", 0x2014}, {"quoteleft", 0x2018}, {"quoteright", 0x2019},
            {"quotedblleft", 0x201C}, {"quotedblright", 0x201D}, {"quotesinglbase", 0x201A}, {"quotedblbase", 0x201E},
            {"ellipsis", 0x2026}, {"dagger", 0x2020}, {"daggerdbl", 0x2021}, {"fi", 0xFB01}, {"fl", 0xFB02}, {"Euro", 0x20AC},
            {"trademark", 0x2122}, {"copyright", 0xA9}, {"registered", 0xAE}, {"degree", 0xB0}, {"plusminus", 0xB1},
            {"section", 0xA7}, {"periodcentered", 0xB7}, {"multiply", 0xD7}, {"divide", 0xF7}, {"guillemotleft", 0xAB},
            {"guillemotright", 0xBB}, {"exclamdown", 0xA1}, {"questiondown", 0xBF}, {"sterling", 0xA3}, {"yen", 0xA5}, {"cent", 0xA2},
            {"currency", 0xA4}, {"germandbls", 0xDF}, {"nbspace", 0xA0}, {"minus", 0x2212}, {"fraction", 0x2044}, {"florin", 0x192},
            {"guilsinglleft", 0x2039}, {"guilsinglright", 0x203A}, {"perthousand", 0x2030}, {"acute", 0xB4}, {"circumflex", 0x2C6},
            {"tilde", 0x2DC}, {"macron", 0xAF}, {"breve", 0x2D8}, {"dotaccent", 0x2D9}, {"dieresis", 0xA8}, {"ring", 0x2DA},
            {"cedilla", 0xB8}, {"hungarumlaut", 0x2DD}, {"ogonek", 0x2DB}, {"caron", 0x2C7}, {"ordfeminine", 0xAA}, {"ordmasculine", 0xBA},
            {"Lslash", 0x141}, {"lslash", 0x142}, {"OE", 0x152}, {"oe", 0x153}, {"dotlessi", 0x131}, {"onesuperior", 0xB9},
            {"twosuperior", 0xB2}, {"threesuperior", 0xB3}, {"logicalnot", 0xAC}, {"mu", 0xB5}, {"Eth", 0xD0}, {"eth", 0xF0},
            {"Thorn", 0xDE}, {"thorn", 0xFE}, {"onehalf", 0xBD}, {"onequarter", 0xBC}, {"threequarters", 0xBE}, {"brokenbar", 0xA6},
            {"Scaron", 0x160}, {"scaron", 0x161}, {"Zcaron", 0x17D}, {"zcaron", 0x17E}, {"Yacute", 0xDD}, {"yacute", 0xFD},
            {"Ydieresis", 0x178}, {"Agrave", 0xC0}, {"Aacute", 0xC1}, {"Acircumflex", 0xC2}, {"Atilde", 0xC3}, {"Adieresis", 0xC4},
            {"Aring", 0xC5}, {"AE", 0xC6}, {"Ccedilla", 0xC7}, {"Egrave", 0xC8}, {"Eacute", 0xC9}, {"Ecircumflex", 0xCA},
            {"Edieresis", 0xCB}, {"Igrave", 0xCC}, {"Iacute", 0xCD}, {"Icircumflex", 0xCE}, {"Idieresis", 0xCF}, {"Ntilde", 0xD1},
            {"Ograve", 0xD2}, {"Oacute", 0xD3}, {"Ocircumflex", 0xD4}, {"Otilde", 0xD5}, {"Odieresis", 0xD6}, {"Oslash", 0xD8},
            {"Ugrave", 0xD9}, {"Uacute", 0xDA}, {"Ucircumflex", 0xDB}, {"Udieresis", 0xDC}, {"agrave", 0xE0}, {"aacute", 0xE1},
            {"acircumflex", 0xE2}, {"atilde", 0xE3}, {"adieresis", 0xE4}, {"aring", 0xE5}, {"ae", 0xE6}, {"ccedilla", 0xE7},
            {"egrave", 0xE8}, {"eacute", 0xE9}, {"ecircumflex", 0xEA}, {"edieresis", 0xEB}, {"igrave", 0xEC}, {"iacute", 0xED},
            {"icircumflex", 0xEE}, {"idieresis", 0xEF}, {"ntilde", 0xF1}, {"ograve", 0xF2}, {"oacute", 0xF3}, {"ocircumflex", 0xF4},
            {"otilde", 0xF5}, {"odieresis", 0xF6}, {"oslash", 0xF8}, {"ugrave", 0xF9}, {"uacute", 0xFA}, {"ucircumflex", 0xFB},
            {"udieresis", 0xFC}, {"ydieresis", 0xFF}};
        if (name.size() == 1) { return (unsigned char)name[0]; }
        if (name.size() >= 7 && name.compare(0, 3, "uni") == 0) { return unsigned(std::strtoul(name.substr(3, 4).c_str(), nullptr, 16)); }
        if (name.size() >= 5 && name[0] == 'u' && std::isxdigit((unsigned char)name[1])) { return unsigned(std::strtoul(name.c_str() + 1, nullptr, 16)); }
        auto it = names.find(name);
        return it == names.end() ? 0u : it->second;
    }

}
