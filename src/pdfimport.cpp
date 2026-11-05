#include "curvy/pdfimport.h"

#include "curvy/gzip.h"
#include "curvy/imagedata.h"
#include "curvy/sketchdocument.h"
#include "curvy/sketchexport.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <map>
#include <set>

// PDF / Illustrator import (see pdfimport.h). Three layers: a lexer / object parser, the file (objects found by scanning, streams decoded),
// and an interpreter that runs a page's content stream and turns what it paints into document objects.

namespace curvy {

    namespace {

        // ---- objects ---------------------------------------------------------------------------------------------------------------------

        struct PObj {
            enum Type { Null, Bool, Num, Str, Name, Arr, Dict, Ref, Op };
            Type type = Null;
            double n = 0.0;                    // Num; the object number of a Ref; Bool as 0 / 1
            std::string s;                     // Str, Name, Op
            std::vector<PObj> a;               // Arr
            std::map<std::string, PObj> d;     // Dict

            bool isNum() const { return type == Num; }
            bool isName(const char* name) const { return type == Name && s == name; }
            const PObj* find(const std::string& key) const {
                if (type != Dict) { return nullptr; }
                auto it = d.find(key);
                return it == d.end() ? nullptr : &it->second;
            }
        };

        bool isSpace(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f' || c == '\0'; }
        bool isDelim(char c) { return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' || c == '{' || c == '}' || c == '/' || c == '%'; }

        class Lexer {
        public:
            Lexer(const std::string& text, std::size_t pos = 0) : src_(text), pos_(pos) {}
            std::size_t pos() const { return pos_; }
            void seek(std::size_t pos) { pos_ = pos; }
            bool atEnd() { skipSpace(); return pos_ >= src_.size(); }

            void skipSpace() {
                while (pos_ < src_.size()) {
                    if (isSpace(src_[pos_])) {
                        ++pos_;
                    } else if (src_[pos_] == '%') {
                        while (pos_ < src_.size() && src_[pos_] != '\n' && src_[pos_] != '\r') { ++pos_; }
                    } else {
                        break;
                    }
                }
            }

            // One object. With `refs`, "12 0 R" is one Ref. Keywords other than true / false / null come back as Op.
            PObj parse(bool refs, int depth = 0) {
                PObj o;
                skipSpace();
                if (pos_ >= src_.size() || depth > 64) {
                    return o;
                }
                const char c = src_[pos_];
                if (c == '/') {
                    ++pos_;
                    o.type = PObj::Name;
                    while (pos_ < src_.size() && !isSpace(src_[pos_]) && !isDelim(src_[pos_])) {
                        if (src_[pos_] == '#' && pos_ + 2 < src_.size() && std::isxdigit((unsigned char)src_[pos_ + 1]) && std::isxdigit((unsigned char)src_[pos_ + 2])) {
                            o.s += char(std::strtol(src_.substr(pos_ + 1, 2).c_str(), nullptr, 16));
                            pos_ += 3;
                        } else {
                            o.s += src_[pos_++];
                        }
                    }
                    return o;
                }
                if (c == '(') {
                    return parseString();
                }
                if (c == '<') {
                    if (pos_ + 1 < src_.size() && src_[pos_ + 1] == '<') {
                        pos_ += 2;
                        o.type = PObj::Dict;
                        while (true) {
                            skipSpace();
                            if (pos_ >= src_.size()) { break; }
                            if (src_[pos_] == '>') {
                                pos_ += (pos_ + 1 < src_.size() && src_[pos_ + 1] == '>') ? 2 : 1;
                                break;
                            }
                            PObj key = parse(false, depth + 1);
                            if (key.type != PObj::Name) {
                                if (key.type == PObj::Null && pos_ >= src_.size()) { break; }
                                continue;
                            }
                            o.d[key.s] = parse(refs, depth + 1);
                        }
                        return o;
                    }
                    ++pos_;
                    o.type = PObj::Str;
                    int high = -1;
                    while (pos_ < src_.size() && src_[pos_] != '>') {
                        const char h = src_[pos_++];
                        if (!std::isxdigit((unsigned char)h)) { continue; }
                        const int v = std::isdigit((unsigned char)h) ? h - '0' : (std::tolower(h) - 'a' + 10);
                        if (high < 0) { high = v; } else { o.s += char(high * 16 + v); high = -1; }
                    }
                    if (high >= 0) { o.s += char(high * 16); }
                    if (pos_ < src_.size()) { ++pos_; }
                    return o;
                }
                if (c == '[') {
                    ++pos_;
                    o.type = PObj::Arr;
                    while (true) {
                        skipSpace();
                        if (pos_ >= src_.size()) { break; }
                        if (src_[pos_] == ']') { ++pos_; break; }
                        PObj item = parse(refs, depth + 1);
                        if (item.type == PObj::Op && item.s.empty()) { ++pos_; continue; }
                        o.a.push_back(std::move(item));
                    }
                    return o;
                }
                if (c == ')' || c == ']' || c == '>' || c == '{' || c == '}') {
                    ++pos_;
                    o.type = PObj::Op;   // a stray delimiter; empty text
                    return o;
                }
                if ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.') {
                    const char* begin = src_.c_str() + pos_;
                    char* end = nullptr;
                    const double v = std::strtod(begin, &end);
                    std::size_t length = std::size_t(end - begin);
                    if (length == 0) { length = 1; }   // "-" alone
                    pos_ += length;
                    o.type = PObj::Num;
                    o.n = std::isfinite(v) ? v : 0.0;
                    if (refs && o.n >= 0 && o.n == std::floor(o.n)) {
                        const std::size_t save = pos_;
                        skipSpace();
                        if (pos_ < src_.size() && std::isdigit((unsigned char)src_[pos_])) {
                            while (pos_ < src_.size() && std::isdigit((unsigned char)src_[pos_])) { ++pos_; }
                            skipSpace();
                            if (pos_ < src_.size() && src_[pos_] == 'R' && (pos_ + 1 >= src_.size() || isSpace(src_[pos_ + 1]) || isDelim(src_[pos_ + 1]))) {
                                ++pos_;
                                o.type = PObj::Ref;
                                return o;
                            }
                        }
                        pos_ = save;
                    }
                    return o;
                }
                std::string word;
                while (pos_ < src_.size() && !isSpace(src_[pos_]) && !isDelim(src_[pos_])) { word += src_[pos_++]; }
                if (word.empty()) { ++pos_; o.type = PObj::Op; return o; }
                if (word == "true" || word == "false") { o.type = PObj::Bool; o.n = word == "true" ? 1 : 0; return o; }
                if (word == "null") { return o; }
                o.type = PObj::Op;
                o.s = word;
                return o;
            }

        private:
            PObj parseString() {
                PObj o;
                o.type = PObj::Str;
                ++pos_;
                int depth = 1;
                while (pos_ < src_.size()) {
                    char c = src_[pos_++];
                    if (c == '\\' && pos_ < src_.size()) {
                        c = src_[pos_++];
                        switch (c) {
                            case 'n': o.s += '\n'; break;
                            case 'r': o.s += '\r'; break;
                            case 't': o.s += '\t'; break;
                            case 'b': o.s += '\b'; break;
                            case 'f': o.s += '\f'; break;
                            case '\r': if (pos_ < src_.size() && src_[pos_] == '\n') { ++pos_; } break;
                            case '\n': break;
                            default:
                                if (c >= '0' && c <= '7') {
                                    int v = c - '0';
                                    for (int k = 0; k < 2 && pos_ < src_.size() && src_[pos_] >= '0' && src_[pos_] <= '7'; ++k) { v = v * 8 + (src_[pos_++] - '0'); }
                                    o.s += char(v);
                                } else {
                                    o.s += c;
                                }
                        }
                    } else if (c == '(') {
                        ++depth;
                        o.s += c;
                    } else if (c == ')') {
                        if (--depth == 0) { break; }
                        o.s += c;
                    } else {
                        o.s += c;
                    }
                }
                return o;
            }

            const std::string& src_;
            std::size_t pos_;
        };

        // ---- stream decoding -------------------------------------------------------------------------------------------------------------

        bool ascii85(const std::string& in, std::string& out) {
            unsigned long group[5];
            int count = 0;
            for (std::size_t i = 0; i < in.size(); ++i) {
                const char c = in[i];
                if (isSpace(c)) { continue; }
                if (c == '~') { break; }
                if (c == 'z' && count == 0) { out.append(4, '\0'); continue; }
                if (c < '!' || c > 'u') { return false; }
                group[count++] = (unsigned long)(c - '!');
                if (count == 5) {
                    unsigned long v = 0;
                    for (int k = 0; k < 5; ++k) { v = v * 85 + group[k]; }
                    for (int k = 3; k >= 0; --k) { out += char((v >> (8 * k)) & 255u); }
                    count = 0;
                }
            }
            if (count > 1) {
                for (int k = count; k < 5; ++k) { group[k] = 84; }
                unsigned long v = 0;
                for (int k = 0; k < 5; ++k) { v = v * 85 + group[k]; }
                for (int k = 3; k > 4 - count; --k) { out += char((v >> (8 * k)) & 255u); }
            }
            return true;
        }

        bool asciiHex(const std::string& in, std::string& out) {
            int high = -1;
            for (char c : in) {
                if (c == '>') { break; }
                if (!std::isxdigit((unsigned char)c)) { continue; }
                const int v = std::isdigit((unsigned char)c) ? c - '0' : (std::tolower(c) - 'a' + 10);
                if (high < 0) { high = v; } else { out += char(high * 16 + v); high = -1; }
            }
            if (high >= 0) { out += char(high * 16); }
            return true;
        }

        double numberOf(const PObj* o, double fallback) { return o != nullptr && o->type == PObj::Num ? o->n : fallback; }

        // PNG predictors (Predictor >= 10) of a Flate stream.
        bool unpredict(const PObj* parms, std::string& data) {
            const int predictor = int(numberOf(parms ? parms->find("Predictor") : nullptr, 1));
            if (predictor < 10) {
                return predictor == 1;
            }
            const int colors = std::max(1, int(numberOf(parms->find("Colors"), 1)));
            const int bits = std::max(1, int(numberOf(parms->find("BitsPerComponent"), 8)));
            const int columns = std::max(1, int(numberOf(parms->find("Columns"), 1)));
            const std::size_t rowBytes = (std::size_t(colors) * std::size_t(bits) * std::size_t(columns) + 7) / 8;
            const std::size_t bpp = std::max<std::size_t>(1, (std::size_t(colors) * std::size_t(bits)) / 8);
            const std::size_t rows = data.size() / (rowBytes + 1);
            std::string out(rows * rowBytes, '\0');
            for (std::size_t r = 0; r < rows; ++r) {
                const unsigned char type = (unsigned char)data[r * (rowBytes + 1)];
                const unsigned char* src = reinterpret_cast<const unsigned char*>(data.data()) + r * (rowBytes + 1) + 1;
                unsigned char* dst = reinterpret_cast<unsigned char*>(&out[r * rowBytes]);
                const unsigned char* up = r > 0 ? dst - rowBytes : nullptr;
                for (std::size_t i = 0; i < rowBytes; ++i) {
                    const int left = i >= bpp ? dst[i - bpp] : 0;
                    const int above = up ? up[i] : 0;
                    const int upLeft = (up && i >= bpp) ? up[i - bpp] : 0;
                    int v = src[i];
                    switch (type) {
                        case 1: v += left; break;
                        case 2: v += above; break;
                        case 3: v += (left + above) / 2; break;
                        case 4: {
                            const int p = left + above - upLeft;
                            const int pa = std::abs(p - left), pb = std::abs(p - above), pc = std::abs(p - upLeft);
                            v += (pa <= pb && pa <= pc) ? left : (pb <= pc ? above : upLeft);
                            break;
                        }
                        default: break;
                    }
                    dst[i] = (unsigned char)(v & 255);
                }
            }
            data.swap(out);
            return true;
        }

        // Runs the filters of a stream. `rest` receives the name of a filter that was not run (DCTDecode ...), whose data is then in `out`.
        bool decodeStream(const PObj& dict, const std::string& raw, std::string& out, std::string& rest) {
            std::vector<std::string> filters;
            std::vector<const PObj*> parms;
            const PObj* f = dict.find("Filter");
            if (f == nullptr) { f = dict.find("F"); }
            const PObj* p = dict.find("DecodeParms");
            if (p == nullptr) { p = dict.find("DP"); }
            if (f != nullptr && f->type == PObj::Name) {
                filters.push_back(f->s);
                parms.push_back(p != nullptr && p->type == PObj::Dict ? p : (p != nullptr && p->type == PObj::Arr && !p->a.empty() ? &p->a[0] : nullptr));
            } else if (f != nullptr && f->type == PObj::Arr) {
                for (std::size_t i = 0; i < f->a.size(); ++i) {
                    filters.push_back(f->a[i].s);
                    parms.push_back(p != nullptr && p->type == PObj::Arr && i < p->a.size() && p->a[i].type == PObj::Dict ? &p->a[i] : nullptr);
                }
            }
            out = raw;
            rest.clear();
            for (std::size_t i = 0; i < filters.size(); ++i) {
                const std::string& name = filters[i];
                std::string next;
                if (name == "FlateDecode" || name == "Fl") {
                    gzipDecompress(out, next);   // a stream cut short still gives what was inflated... no: it is cleared; fail below
                    if (next.empty() && !out.empty()) { return false; }
                    if (!unpredict(parms[i], next)) { return false; }
                } else if (name == "ASCIIHexDecode" || name == "AHx") {
                    asciiHex(out, next);
                } else if (name == "ASCII85Decode" || name == "A85") {
                    if (!ascii85(out, next)) { return false; }
                } else {
                    rest = name;
                    return true;
                }
                out.swap(next);
            }
            return true;
        }

        // ---- the file --------------------------------------------------------------------------------------------------------------------

        class PdfFile {
        public:
            bool load(const std::string& data);
            const PObj& resolve(const PObj& o) const {
                const PObj* cur = &o;
                for (int i = 0; i < 16 && cur->type == PObj::Ref; ++i) {
                    auto it = objects_.find(int(cur->n));
                    if (it == objects_.end()) { return null_; }
                    cur = &it->second;
                }
                return cur->type == PObj::Ref ? null_ : *cur;
            }
            const PObj& get(const PObj& dict, const char* key) const {
                const PObj* v = dict.find(key);
                return v == nullptr ? null_ : resolve(*v);
            }
            // The decoded data of a stream object given as a Ref (or direct stream dict with its number); false when it is not one.
            bool streamOf(const PObj& ref, std::string& out, std::string& rest) const {
                if (ref.type != PObj::Ref) { return false; }
                auto raw = streams_.find(int(ref.n));
                auto dict = objects_.find(int(ref.n));
                if (raw == streams_.end() || dict == objects_.end()) { return false; }
                return decodeStream(dict->second, raw->second, out, rest);
            }
            const PObj& root() const { return null_; }
            const std::map<int, PObj>& objects() const { return objects_; }
            const PObj null_;

        private:
            void addObjectStream(int number, const PObj& dict, const std::string& raw);
            std::map<int, PObj> objects_;
            std::map<int, std::string> streams_;
        };

        bool PdfFile::load(const std::string& data) {
            std::size_t at = 0;
            while (true) {
                at = data.find("obj", at);
                if (at == std::string::npos) { break; }
                // "<number> <generation> obj" - not "endobj"
                std::size_t p = at;
                const std::size_t keyword = at;
                at += 3;
                if (p >= 1 && !isSpace(data[p - 1]) ) { continue; }
                while (p > 0 && isSpace(data[p - 1])) { --p; }
                std::size_t genEnd = p;
                while (p > 0 && std::isdigit((unsigned char)data[p - 1])) { --p; }
                if (p == genEnd) { continue; }
                while (p > 0 && isSpace(data[p - 1])) { --p; }
                std::size_t numEnd = p;
                while (p > 0 && std::isdigit((unsigned char)data[p - 1])) { --p; }
                if (p == numEnd || numEnd - p > 9) { continue; }
                if (p > 0 && !isSpace(data[p - 1]) && data[p - 1] != '>' && data[p - 1] != ']') { continue; }
                const int number = std::atoi(data.substr(p, numEnd - p).c_str());
                if (number <= 0) { continue; }

                Lexer lexer(data, keyword + 3);
                PObj obj = lexer.parse(true);
                if (obj.type == PObj::Op) { continue; }
                lexer.skipSpace();
                std::size_t next = std::min(lexer.pos(), data.size());
                if (obj.type == PObj::Dict && data.compare(next, 6, "stream") == 0) {
                    std::size_t start = next + 6;
                    if (start < data.size() && data[start] == '\r') { ++start; }
                    if (start < data.size() && data[start] == '\n') { ++start; }
                    std::size_t end = std::string::npos;
                    const PObj* length = obj.find("Length");
                    if (length != nullptr && length->type == PObj::Num && length->n >= 0 && start + std::size_t(length->n) <= data.size()) {
                        std::size_t candidate = start + std::size_t(length->n);
                        std::size_t k = candidate;
                        while (k < data.size() && isSpace(data[k])) { ++k; }
                        if (data.compare(k, 9, "endstream") == 0) { end = candidate; }
                    }
                    if (end == std::string::npos) {
                        end = data.find("endstream", start);
                        if (end == std::string::npos) { end = data.size(); }
                        while (end > start && (data[end - 1] == '\n' || data[end - 1] == '\r')) { --end; }
                    }
                    streams_[number] = data.substr(start, end - start);
                    at = end;
                } else {
                    at = next;
                }
                objects_[number] = std::move(obj);
            }
            // Objects packed into object streams (PDF 1.5).
            std::vector<int> packs;
            for (const auto& entry : objects_) {
                const PObj* type = entry.second.find("Type");
                if (type != nullptr && type->isName("ObjStm") && streams_.count(entry.first)) { packs.push_back(entry.first); }
            }
            for (int number : packs) {
                addObjectStream(number, objects_[number], streams_[number]);
            }
            return !objects_.empty();
        }

        void PdfFile::addObjectStream(int, const PObj& dict, const std::string& raw) {
            std::string data, rest;
            if (!decodeStream(dict, raw, data, rest) || !rest.empty()) { return; }
            const int count = int(numberOf(dict.find("N"), 0)), first = int(numberOf(dict.find("First"), 0));
            Lexer header(data);
            std::vector<std::pair<int, std::size_t>> entries;
            for (int i = 0; i < count && i < 100000; ++i) {
                PObj a = header.parse(false), b = header.parse(false);
                if (!a.isNum() || !b.isNum()) { break; }
                entries.push_back({int(a.n), std::size_t(b.n)});
            }
            for (const auto& entry : entries) {
                if (objects_.count(entry.first) || first < 0 || std::size_t(first) + entry.second >= data.size()) { continue; }
                Lexer lexer(data, std::size_t(first) + entry.second);
                objects_[entry.first] = lexer.parse(true);
            }
        }

        // ---- the interpreter -------------------------------------------------------------------------------------------------------------

        struct Space {
            enum Kind { Gray, RGB, CMYK, Pattern, Other } kind = Gray;
            int n = 1;
        };

        struct Paint {
            Space space;
            SketchColor color = SketchColor::rgb(0, 0, 0);
            std::string pattern;   // the name in /Pattern of the resources, when space is Pattern
        };

        BLPath closedSubpaths(const BLPath& source) {
            BLPath closed;
            BLPathView view = source.view();
            bool opened = false;
            for (std::size_t i = 0; i < view.size; ++i) {
                const BLPoint v = view.vertex_data[i];
                switch (view.command_data[i]) {
                    case BL_PATH_CMD_MOVE: if (opened) { closed.close(); } closed.move_to(v); opened = true; break;
                    case BL_PATH_CMD_ON: closed.line_to(v); break;
                    case BL_PATH_CMD_QUAD: if (i + 1 < view.size) { closed.quad_to(v, view.vertex_data[i + 1]); ++i; } break;
                    case BL_PATH_CMD_CUBIC: if (i + 2 < view.size) { closed.cubic_to(v, view.vertex_data[i + 1], view.vertex_data[i + 2]); i += 2; } break;
                    case BL_PATH_CMD_CLOSE: closed.close(); opened = false; break;
                    default: break;
                }
            }
            if (opened) { closed.close(); }
            return closed;
        }

        // One level of clipping: a group of the document that gets the clip path as its topmost member once the level ends.
        struct ClipLevel {
            std::size_t group = 0;
            BLPath path;
            int members = 0;
        };

        // A soft mask from an ExtGState, rendered once over the page: one coverage byte per pixel, `scale` pixels per document unit.
        struct SoftMask {
            BLBox page;
            double scale = 1.0;
            int width = 0, height = 0;
            std::vector<std::uint8_t> alpha;
        };

        struct State {
            std::shared_ptr<const SoftMask> softMask;
            std::vector<std::shared_ptr<ClipLevel>> clips;   // outermost first
            BLMatrix2D ctm = BLMatrix2D::make_identity();
            Paint fill, stroke;
            double lineWidth = 1.0;
            int cap = 0, join = 0;
            double miter = 10.0;
            std::vector<double> dash;
            double fillAlpha = 1.0, strokeAlpha = 1.0;
        };

        class Interpreter {
        public:
            Interpreter(const PdfFile& file, SketchDocument& document, const BLMatrix2D& base, const BLBox& pageBox) : file_(file), document_(document), base_(base), pageBox_(pageBox) { state_.ctm = base; resources_ = &file.null_; }
            void run(const std::string& content, const PObj& resources);
            void finish() { flushClips(0); }

        private:
            void addObject(SketchObject* object);
            void flushClips(std::size_t keep);
            void applyClip(const BLPath& path);
            void shadingFill(const std::string& name, const PObj& resources);
            void setSoftMask(const PObj& dict);
            SketchObject* masked(SketchObject* object);
            void drawForm(const PObj& entry, const PObj& xobject, const PObj& resources);
            void execute(const std::string& op, std::vector<PObj>& args, const PObj& resources);
            void paint(bool fill, bool stroke, bool close, bool evenOdd = false);
            void rect(double x, double y, double w, double h);
            void point(double x, double y, BLPoint& out) const { out = state_.ctm.map_point(x, y); }
            Space spaceOf(const PObj& cs, const PObj& resources, int depth = 0) const;
            void setColorOperands(Paint& paint, const std::vector<PObj>& args) const;
            static SketchColor initialColor(const Space& space);
            SketchPaint paintFor(const Paint& paint, double alpha, const PObj& resources, const BLPath& path) const;
            bool gradientFor(const PObj& pattern, const BLPath& path, SketchGradient& gradient, bool& radial) const;
            bool shadingGradient(const PObj& shading, const BLMatrix2D& m, const BLPath& path, SketchGradient& gradient, bool& radial) const;
            bool functionColor(const PObj& function, double t, const Space& space, SketchColor& out, int depth = 0) const;
            void doXObject(const std::string& name, const PObj& resources);
            void doImage(const PObj& image, const PObj& ref);
            void applyGState(const PObj& dict);
            double scale() const { return std::sqrt(std::fabs(state_.ctm.m00 * state_.ctm.m11 - state_.ctm.m01 * state_.ctm.m10)); }

            const PdfFile& file_;
            SketchDocument& document_;
            BLMatrix2D base_;
            BLBox pageBox_;
            bool pendingClip_ = false;
            State state_;
            std::vector<State> stack_;
            BLPath path_;
            bool open_ = false;
            int depth_ = 0;
            std::size_t operations_ = 0;
            const PObj* resources_ = nullptr;   // the resources of the content being run
        };

        SketchColor Interpreter::initialColor(const Space& space) {
            return space.kind == Space::CMYK ? SketchColor::cmyk(0, 0, 0, 1) : SketchColor::rgb(0, 0, 0);
        }

        Space Interpreter::spaceOf(const PObj& cs, const PObj& resources, int depth) const {
            Space s;
            const PObj& o = file_.resolve(cs);
            if (depth > 6) { return s; }
            if (o.type == PObj::Name) {
                if (o.s == "DeviceGray" || o.s == "G" || o.s == "CalGray") { return s; }
                if (o.s == "DeviceRGB" || o.s == "RGB" || o.s == "CalRGB") { s.kind = Space::RGB; s.n = 3; return s; }
                if (o.s == "DeviceCMYK" || o.s == "CMYK") { s.kind = Space::CMYK; s.n = 4; return s; }
                if (o.s == "Pattern") { s.kind = Space::Pattern; s.n = 0; return s; }
                const PObj& named = file_.get(file_.get(resources, "ColorSpace"), o.s.c_str());
                if (named.type != PObj::Null) { return spaceOf(named, resources, depth + 1); }
                return s;
            }
            if (o.type == PObj::Arr && !o.a.empty()) {
                const std::string& kind = o.a[0].s;
                if (kind == "ICCBased" && o.a.size() > 1) {
                    const PObj& stream = file_.resolve(o.a[1]);
                    const int n = int(numberOf(stream.find("N"), 3));
                    s.kind = n == 1 ? Space::Gray : (n == 4 ? Space::CMYK : Space::RGB);
                    s.n = n == 1 ? 1 : (n == 4 ? 4 : 3);
                    return s;
                }
                if (kind == "CalRGB") { s.kind = Space::RGB; s.n = 3; return s; }
                if (kind == "CalGray") { return s; }
                if (kind == "Pattern") { s.kind = Space::Pattern; s.n = 0; return s; }
                if (kind == "Separation") { s.kind = Space::Other; s.n = 1; return s; }
                if (kind == "DeviceN" && o.a.size() > 1) { s.kind = Space::Other; s.n = int(file_.resolve(o.a[1]).a.size()); return s; }
                if (kind == "Lab") { s.kind = Space::Other; s.n = 3; return s; }
                if (kind == "Indexed") { s.kind = Space::Other; s.n = 1; return s; }
                return spaceOf(o.a[0], resources, depth + 1);
            }
            return s;
        }

        void Interpreter::setColorOperands(Paint& paint, const std::vector<PObj>& args) const {
            std::vector<double> v;
            std::string name;
            for (const PObj& a : args) {
                if (a.isNum()) { v.push_back(a.n); } else if (a.type == PObj::Name) { name = a.s; }
            }
            auto unit = [](double x) { return float(std::min(1.0, std::max(0.0, x))); };
            if (paint.space.kind == Space::Pattern) {
                paint.pattern = name;
                return;
            }
            if (paint.space.kind == Space::Other && paint.space.n == 1 && !v.empty()) {
                paint.color = SketchColor::gray(unit(1.0 - v[0]));   // a tint of a spot ink: shown as a gray
            } else if (v.size() == 1) {
                paint.color = SketchColor::gray(unit(v[0]));
            } else if (v.size() == 3) {
                paint.color = SketchColor::rgb(unit(v[0]), unit(v[1]), unit(v[2]));
            } else if (v.size() == 4) {
                paint.color = SketchColor::cmyk(unit(v[0]), unit(v[1]), unit(v[2]), unit(v[3]));
            }
        }

        // The value of a PDF function (types 2 and 3, or an array of type 2 functions, one per component) as a color.
        bool Interpreter::functionColor(const PObj& function, double t, const Space& space, SketchColor& out, int depth) const {
            std::vector<double> c;
            std::function<bool(const PObj&, double, std::vector<double>&, int)> eval = [&](const PObj& fn, double x, std::vector<double>& result, int level) {
                const PObj& f = file_.resolve(fn);
                if (level > 8) { return false; }
                if (f.type == PObj::Arr) {   // one function per component
                    for (const PObj& each : f.a) {
                        std::vector<double> one;
                        if (!eval(each, x, one, level + 1) || one.empty()) { return false; }
                        result.push_back(one[0]);
                    }
                    return !result.empty();
                }
                if (f.type != PObj::Dict) { return false; }
                const int type = int(numberOf(f.find("FunctionType"), -1));
                const PObj& domain = file_.get(f, "Domain");
                const double d0 = domain.a.size() >= 2 ? numberOf(&domain.a[0], 0.0) : 0.0, d1 = domain.a.size() >= 2 ? numberOf(&domain.a[1], 1.0) : 1.0;
                x = std::min(std::max(x, std::min(d0, d1)), std::max(d0, d1));
                if (type == 2) {
                    const PObj& c0 = file_.get(f, "C0");
                    const PObj& c1 = file_.get(f, "C1");
                    const double n = numberOf(f.find("N"), 1.0);
                    const std::size_t count = std::max<std::size_t>(c0.type == PObj::Arr ? c0.a.size() : 1, c1.type == PObj::Arr ? c1.a.size() : 1);
                    const double k = n == 1.0 ? x : std::pow(std::max(0.0, x), n);
                    for (std::size_t i = 0; i < count; ++i) {
                        const double a = c0.type == PObj::Arr && i < c0.a.size() ? numberOf(&c0.a[i], 0.0) : 0.0;
                        const double b = c1.type == PObj::Arr && i < c1.a.size() ? numberOf(&c1.a[i], 1.0) : 1.0;
                        result.push_back(a + k * (b - a));
                    }
                    return true;
                }
                if (type == 3) {
                    const PObj& functions = file_.get(f, "Functions");
                    const PObj& bounds = file_.get(f, "Bounds");
                    const PObj& encode = file_.get(f, "Encode");
                    if (functions.type != PObj::Arr || functions.a.empty()) { return false; }
                    std::size_t piece = 0;
                    while (piece < bounds.a.size() && piece + 1 < functions.a.size() && x >= numberOf(&bounds.a[piece], 1.0)) { ++piece; }
                    const double lo = piece == 0 ? d0 : numberOf(&bounds.a[piece - 1], d0);
                    const double hi = piece < bounds.a.size() ? numberOf(&bounds.a[piece], d1) : d1;
                    const double e0 = 2 * piece + 1 < encode.a.size() + 0 && encode.a.size() > 2 * piece ? numberOf(&encode.a[2 * piece], 0.0) : 0.0;
                    const double e1 = encode.a.size() > 2 * piece + 1 ? numberOf(&encode.a[2 * piece + 1], 1.0) : 1.0;
                    const double mapped = hi - lo <= 1e-12 ? e0 : e0 + (x - lo) / (hi - lo) * (e1 - e0);
                    return eval(functions.a[piece], mapped, result, level + 1);
                }
                return false;
            };
            (void)depth;
            if (!eval(function, t, c, 0) || c.empty()) { return false; }
            auto unit = [](double x) { return float(std::min(1.0, std::max(0.0, x))); };
            if (space.kind == Space::CMYK && c.size() >= 4) {
                out = SketchColor::cmyk(unit(c[0]), unit(c[1]), unit(c[2]), unit(c[3]));
            } else if (c.size() >= 3) {
                out = SketchColor::rgb(unit(c[0]), unit(c[1]), unit(c[2]));
            } else if (space.kind == Space::Other) {
                out = SketchColor::gray(unit(1.0 - c[0]));
            } else {
                out = SketchColor::gray(unit(c[0]));
            }
            return true;
        }

        // A shading pattern (PatternType 2) of an axial or radial shading as a gradient over `path`.
        bool Interpreter::gradientFor(const PObj& pattern, const BLPath& path, SketchGradient& gradient, bool& radial) const {
            BLMatrix2D m = BLMatrix2D::make_identity();
            const PObj& matrix = file_.get(pattern, "Matrix");
            if (matrix.type == PObj::Arr && matrix.a.size() >= 6) {
                m = BLMatrix2D(numberOf(&matrix.a[0], 1), numberOf(&matrix.a[1], 0), numberOf(&matrix.a[2], 0), numberOf(&matrix.a[3], 1), numberOf(&matrix.a[4], 0), numberOf(&matrix.a[5], 0));
            }
            m.post_transform(base_);   // pattern space is the page's default space
            return shadingGradient(file_.get(pattern, "Shading"), m, path, gradient, radial);
        }

        bool Interpreter::shadingGradient(const PObj& shading, const BLMatrix2D& m, const BLPath& path, SketchGradient& gradient, bool& radial) const {
            const int type = int(numberOf(shading.find("ShadingType"), 0));
            if (type != 2 && type != 3) { return false; }
            const PObj& coords = file_.get(shading, "Coords");
            const std::size_t need = type == 2 ? 4 : 6;
            if (coords.type != PObj::Arr || coords.a.size() < need) { return false; }
            std::vector<double> c;
            for (const PObj& v : coords.a) { c.push_back(numberOf(&file_.resolve(v), 0.0)); }
            const Space space = spaceOf(shading.find("ColorSpace") != nullptr ? *shading.find("ColorSpace") : PObj(), PObj());
            const PObj* function = shading.find("Function");
            if (function == nullptr) { return false; }
            std::vector<double> offsets;
            for (int i = 0; i <= 16; ++i) { offsets.push_back(i / 16.0); }
            const PObj& top = file_.resolve(*function);
            if (top.type == PObj::Dict && int(numberOf(top.find("FunctionType"), 0)) == 3) {
                for (const PObj& b : file_.get(top, "Bounds").a) {
                    const double v = numberOf(&b, -1);
                    if (v > 0.0 && v < 1.0) { offsets.push_back(std::max(0.0, v - 1e-4)); offsets.push_back(v); }
                }
            }
            std::sort(offsets.begin(), offsets.end());
            gradient = SketchGradient();
            for (double t : offsets) {
                SketchColor color = SketchColor::rgb(0, 0, 0);
                if (!functionColor(*function, t, space, color)) { return false; }
                GradientStop stop;
                stop.offset = t;
                stop.color = color;
                gradient.stops.push_back(stop);
            }
            BLBox box(0, 0, 0, 0);
            path.get_bounding_box(&box);
            radial = type == 3;
            BLPoint start, end;
            if (radial) {
                const bool inner = c[2] <= 1e-9;
                start = m.map_point(inner ? c[0] : c[3], inner ? c[1] : c[4]);
                end = m.map_point((inner ? c[0] : c[3]) + c[5], inner ? c[1] : c[4]);
            } else {
                start = m.map_point(c[0], c[1]);
                end = m.map_point(c[2], c[3]);
            }
            gradient.setAxis(box, start, end);
            return true;
        }

        SketchPaint Interpreter::paintFor(const Paint& paint, double alpha, const PObj& resources, const BLPath& path) const {
            if (paint.space.kind == Space::Pattern) {
                const PObj& pattern = file_.get(file_.get(resources, "Pattern"), paint.pattern.c_str());
                SketchGradient gradient;
                bool radial = false;
                if (pattern.type == PObj::Dict && int(numberOf(pattern.find("PatternType"), 0)) == 2 && gradientFor(pattern, path, gradient, radial)) {
                    return radial ? SketchPaint::radial(gradient) : SketchPaint::linear(gradient);
                }
                return SketchPaint::none();
            }
            SketchColor color = paint.color;
            color.setAlpha(float(alpha));
            return SketchPaint::solid(color);
        }

        void Interpreter::rect(double x, double y, double w, double h) {
            BLPoint p;
            point(x, y, p); path_.move_to(p);
            point(x + w, y, p); path_.line_to(p);
            point(x + w, y + h, p); path_.line_to(p);
            point(x, y + h, p); path_.line_to(p);
            path_.close();
            open_ = false;
        }

        void Interpreter::paint(bool fill, bool stroke, bool close, bool evenOdd) {
            const BLPath clipSource = pendingClip_ ? path_ : BLPath();
            if (path_.size() > 0 && (fill || stroke)) {
                BLPath outline = path_;
                if (close) { outline.close(); }
                if (fill) {   // fills close every open subpath
                    const BLPath closed = closedSubpaths(outline);
                    if (stroke && !close) {
                        outline = closed;   // B on an open path: the stroke closes it as well (a small difference)
                    } else if (!stroke) {
                        outline = closed;
                    }
                }
                std::unique_ptr<SketchObject> object(SketchObject::make(SketchObjectType::Path, outline));
                object->setFill(fill ? paintFor(state_.fill, state_.fillAlpha, *resources_, outline) : SketchPaint::none());
                object->setFillEvenOdd(fill && evenOdd);
                if (stroke) {
                    object->setStroke(paintFor(state_.stroke, state_.strokeAlpha, *resources_, outline));
                    const double s = scale();
                    object->setStrokeWidth(std::max(0.05, state_.lineWidth * s));
                    StrokeOptions options;
                    options.cap = state_.cap == 1 ? StrokeCap::Round : (state_.cap == 2 ? StrokeCap::Square : StrokeCap::Butt);
                    options.join = state_.join == 1 ? StrokeJoin::Round : (state_.join == 2 ? StrokeJoin::Bevel : StrokeJoin::Miter);
                    options.miterLimit = std::max(1.0, state_.miter);
                    double period = 0.0;
                    for (double d : state_.dash) { period += d; }
                    if (period > 1e-9) {
                        for (double d : state_.dash) { options.dashes.push_back(d * s); }
                        if (options.dashes.size() % 2 == 1) { options.dashes.insert(options.dashes.end(), options.dashes.begin(), options.dashes.end()); }
                    }
                    object->setStrokeOptions(options);
                } else {
                    object->setStroke(SketchPaint::none());
                }
                addObject(object.release());
            }
            path_.reset();
            open_ = false;
            if (pendingClip_) {
                pendingClip_ = false;
                if (clipSource.size() > 0) { applyClip(closedSubpaths(clipSource)); }
            }
        }

        void Interpreter::setSoftMask(const PObj& dict) {
            state_.softMask.reset();
            const PObj* g = dict.find("G");
            if (g == nullptr || file_.resolve(*g).type != PObj::Dict || depth_ >= 8) { return; }
            const double w = pageBox_.x1 - pageBox_.x0, h = pageBox_.y1 - pageBox_.y0;
            const double scale = std::min(2.0, 2400.0 / std::max(w, h));
            if (!(w > 0.0) || !(h > 0.0) || w * scale * h * scale > 16.0 * 1024 * 1024) { return; }
            SketchDocument scratch;
            scratch.setPageSize(float(w), float(h));
            scratch.setArtboards({Artboard{"mask", pageBox_.x0, pageBox_.y0, w, h}}, 0);
            scratch.setPageBackground(std::nullopt);
            Interpreter inner(file_, scratch, base_, pageBox_);
            inner.state_.ctm = state_.ctm;
            inner.depth_ = depth_ + 1;
            inner.drawForm(*g, file_.resolve(*g), *resources_);
            inner.finish();
            BLImage image;
            if (!renderPageImage(scratch, scale, image)) { return; }
            BLImageData data;
            if (image.get_data(&data) != BL_SUCCESS || data.format != BL_FORMAT_PRGB32) { return; }
            double backdrop = 0.0;   // luminosity of the backdrop color BC (black by default)
            const PObj& bc = file_.get(dict, "BC");
            if (bc.type == PObj::Arr && (bc.a.size() == 1 || bc.a.size() == 3)) {
                const double r = numberOf(&bc.a[0], 0), g2 = bc.a.size() == 3 ? numberOf(&bc.a[1], 0) : r, b = bc.a.size() == 3 ? numberOf(&bc.a[2], 0) : r;
                backdrop = 0.3 * r + 0.59 * g2 + 0.11 * b;
            }
            const bool alphaOnly = file_.get(dict, "S").isName("Alpha");
            auto mask = std::make_shared<SoftMask>();
            mask->page = pageBox_;
            mask->scale = scale;
            mask->width = int(data.size.w);
            mask->height = int(data.size.h);
            mask->alpha.resize(std::size_t(mask->width) * std::size_t(mask->height));
            for (int y = 0; y < mask->height; ++y) {
                const std::uint32_t* row = reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(data.pixel_data) + std::ptrdiff_t(y) * data.stride);
                for (int x = 0; x < mask->width; ++x) {
                    const std::uint32_t p = row[x];
                    const double a = double(p >> 24) / 255.0;
                    double v = a;
                    if (!alphaOnly) {   // premultiplied, so the color already is the color over black
                        v = (0.3 * double((p >> 16) & 255u) + 0.59 * double((p >> 8) & 255u) + 0.11 * double(p & 255u)) / 255.0 + (1.0 - a) * backdrop;
                    }
                    mask->alpha[std::size_t(y) * std::size_t(mask->width) + std::size_t(x)] = std::uint8_t(std::lround(std::min(1.0, std::max(0.0, v)) * 255.0));
                }
            }
            state_.softMask = mask;
        }

        // The object as it shows through the soft mask: drawn on its own, multiplied by the mask, and kept as a picture (the document has no
        // opacity masks). Objects the mask does not reach are kept as they are.
        SketchObject* Interpreter::masked(SketchObject* object) {
            const SoftMask& mask = *state_.softMask;
            const BLBox bounds = object->paintBounds();
            const int x0 = std::max(0, int(std::floor((bounds.x0 - mask.page.x0) * mask.scale))), y0 = std::max(0, int(std::floor((bounds.y0 - mask.page.y0) * mask.scale)));
            const int x1 = std::min(mask.width, int(std::ceil((bounds.x1 - mask.page.x0) * mask.scale))), y1 = std::min(mask.height, int(std::ceil((bounds.y1 - mask.page.y0) * mask.scale)));
            if (x1 <= x0 || y1 <= y0 || std::int64_t(x1 - x0) * (y1 - y0) > 16 * 1024 * 1024) { return object; }
            const double rx = mask.page.x0 + x0 / mask.scale, ry = mask.page.y0 + y0 / mask.scale;
            const double rw = (x1 - x0) / mask.scale, rh = (y1 - y0) / mask.scale;
            SketchDocument scratch;
            scratch.setPageSize(float(rw), float(rh));
            scratch.setArtboards({Artboard{"object", rx, ry, rw, rh}}, 0);
            scratch.setPageBackground(std::nullopt);
            scratch.add(object);
            BLImage image;
            BLImageData data;
            if (!renderPageImage(scratch, mask.scale, image) || image.get_data(&data) != BL_SUCCESS || data.format != BL_FORMAT_PRGB32) {
                return SketchObject::makePath(BLPath());
            }
            const int w = std::min(int(data.size.w), x1 - x0), h = std::min(int(data.size.h), y1 - y0);
            for (int y = 0; y < h; ++y) {
                std::uint32_t* row = reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(data.pixel_data) + std::ptrdiff_t(y) * data.stride);
                const std::uint8_t* coverage = mask.alpha.data() + std::size_t(y0 + y) * std::size_t(mask.width) + std::size_t(x0);
                for (int x = 0; x < w; ++x) {
                    const std::uint32_t p = row[x], m = coverage[x];
                    row[x] = (((p >> 24) * m / 255u) << 24) | ((((p >> 16) & 255u) * m / 255u) << 16) | ((((p >> 8) & 255u) * m / 255u) << 8) | ((p & 255u) * m / 255u);
                }
            }
            BLImageCodec codec;
            codec.find_by_name("PNG");
            BLArray<uint8_t> buffer;
            if (image.write_to_data(buffer, codec) != BL_SUCCESS) { return SketchObject::makePath(BLPath()); }
            std::shared_ptr<const ImageData> picture = decodeImage(std::string(reinterpret_cast<const char*>(buffer.data()), buffer.size()), "PDF soft mask");
            if (!picture) { return SketchObject::makePath(BLPath()); }
            return SketchObject::makeImageObject(picture, BLMatrix2D(1.0 / mask.scale, 0.0, 0.0, 1.0 / mask.scale, rx, ry));
        }

        void Interpreter::addObject(SketchObject* object) {
            if (state_.softMask) { object = masked(object); }
            std::vector<std::size_t> groups;
            for (const auto& level : state_.clips) {
                groups.push_back(level->group);
                ++level->members;
            }
            object->setGroups(groups);
            document_.add(object);
        }

        // Ends the clip levels deeper than `keep`: each clip path joins its group as the topmost member, which is what makes it the mask.
        void Interpreter::flushClips(std::size_t keep) {
            while (state_.clips.size() > keep) {
                const std::shared_ptr<ClipLevel> level = state_.clips.back();
                if (level->members > 0) {
                    std::vector<std::size_t> groups;
                    for (const auto& l : state_.clips) { groups.push_back(l->group); }
                    SketchObject* mask = SketchObject::makePath(level->path);
                    mask->setFill(SketchPaint::solid(SketchColor::gray(0.0f)));
                    mask->setStroke(SketchPaint::none());
                    mask->setName("Clipping Path");
                    mask->setGroups(groups);
                    document_.add(mask);
                    GroupProps props;
                    props.clip = true;
                    document_.setGroupProps(level->group, props);
                }
                state_.clips.pop_back();
            }
        }

        void Interpreter::applyClip(const BLPath& path) {
            BLBox box(0, 0, 0, 0);
            if (path.get_bounding_box(&box) != BL_SUCCESS || !(box.x1 - box.x0 > 1e-6) || !(box.y1 - box.y0 > 1e-6)) { return; }
            // Illustrator wraps a page in a clip to its own bounds; that clip changes nothing.
            const double eps = 0.01;
            bool rectangle = path.size() <= 6;
            BLPathView view = path.view();
            for (std::size_t i = 0; rectangle && i < view.size; ++i) {
                if (view.command_data[i] == BL_PATH_CMD_CLOSE) { continue; }
                const BLPoint v = view.vertex_data[i];
                const bool onX = std::fabs(v.x - box.x0) < eps || std::fabs(v.x - box.x1) < eps, onY = std::fabs(v.y - box.y0) < eps || std::fabs(v.y - box.y1) < eps;
                rectangle = view.command_data[i] <= BL_PATH_CMD_ON && onX && onY;
            }
            if (rectangle && box.x0 <= pageBox_.x0 + eps && box.y0 <= pageBox_.y0 + eps && box.x1 >= pageBox_.x1 - eps && box.y1 >= pageBox_.y1 - eps) { return; }
            auto level = std::make_shared<ClipLevel>();
            level->group = document_.newGroupId();
            level->path = path;
            state_.clips.push_back(level);
        }

        void Interpreter::shadingFill(const std::string& name, const PObj& resources) {
            const PObj& shading = file_.get(file_.get(resources, "Shading"), name.c_str());
            if (shading.type != PObj::Dict) { return; }
            BLPath area;
            if (!state_.clips.empty()) {
                area = state_.clips.back()->path;
            } else {
                area.add_box(pageBox_);
            }
            SketchGradient gradient;
            bool radial = false;
            if (!shadingGradient(shading, state_.ctm, area, gradient, radial)) { return; }
            SketchObject* object = SketchObject::make(SketchObjectType::Path, area);
            object->setFill(radial ? SketchPaint::radial(gradient) : SketchPaint::linear(gradient));
            object->setStroke(SketchPaint::none());
            addObject(object);
        }

        void Interpreter::applyGState(const PObj& dict) {
            if (dict.type != PObj::Dict) { return; }
            if (const PObj* v = dict.find("LW")) { state_.lineWidth = numberOf(&file_.resolve(*v), state_.lineWidth); }
            if (const PObj* v = dict.find("LC")) { state_.cap = int(numberOf(&file_.resolve(*v), 0)); }
            if (const PObj* v = dict.find("LJ")) { state_.join = int(numberOf(&file_.resolve(*v), 0)); }
            if (const PObj* v = dict.find("ML")) { state_.miter = numberOf(&file_.resolve(*v), 10); }
            if (const PObj* v = dict.find("ca")) { state_.fillAlpha = std::min(1.0, std::max(0.0, numberOf(&file_.resolve(*v), 1.0))); }
            if (const PObj* v = dict.find("CA")) { state_.strokeAlpha = std::min(1.0, std::max(0.0, numberOf(&file_.resolve(*v), 1.0))); }
            if (const PObj* v = dict.find("SMask")) {
                const PObj& sm = file_.resolve(*v);
                if (sm.type == PObj::Dict) { setSoftMask(sm); } else { state_.softMask.reset(); }
            }
            if (const PObj* v = dict.find("D")) {
                const PObj& d = file_.resolve(*v);
                if (d.type == PObj::Arr && !d.a.empty()) {
                    state_.dash.clear();
                    for (const PObj& x : file_.resolve(d.a[0]).a) { state_.dash.push_back(numberOf(&x, 0.0)); }
                }
            }
        }

        void Interpreter::doImage(const PObj& image, const PObj& ref) {
            const int width = int(numberOf(image.find("Width"), numberOf(image.find("W"), 0))), height = int(numberOf(image.find("Height"), numberOf(image.find("H"), 0)));
            if (width <= 0 || height <= 0 || std::int64_t(width) * height > 64 * 1024 * 1024) { return; }
            std::string pixels, rest;
            if (!file_.streamOf(ref, pixels, rest)) { return; }
            std::shared_ptr<const ImageData> data;
            if (rest == "DCTDecode" || rest == "DCT") {
                data = decodeImage(pixels, "PDF image");
            } else if (rest.empty()) {
                const Space space = spaceOf(image.find("ColorSpace") != nullptr ? *image.find("ColorSpace") : PObj(), *resources_);
                const int bits = int(numberOf(file_.get(image, "BitsPerComponent").type == PObj::Num ? &file_.get(image, "BitsPerComponent") : nullptr, 8));
                const bool mask = file_.get(image, "ImageMask").n != 0.0 && file_.get(image, "ImageMask").type == PObj::Bool;
                const int comps = mask ? 1 : (space.kind == Space::Other || space.kind == Space::Pattern ? 0 : space.n);
                if (comps == 0 || (bits != 8 && !(mask && bits == 1))) { return; }
                BLImage out(width, height, BL_FORMAT_PRGB32);
                BLImageData target;
                if (out.get_data(&target) != BL_SUCCESS) { return; }
                // An optional 8-bit gray soft mask of the same size.
                std::string alpha, alphaRest;
                if (const PObj* sm = image.find("SMask")) {
                    const PObj& smDict = file_.resolve(*sm);
                    if (int(numberOf(smDict.find("Width"), 0)) == width && int(numberOf(smDict.find("Height"), 0)) == height && int(numberOf(smDict.find("BitsPerComponent"), 8)) == 8) {
                        if (!file_.streamOf(*sm, alpha, alphaRest) || !alphaRest.empty() || alpha.size() < std::size_t(width) * std::size_t(height)) { alpha.clear(); }
                    }
                }
                const std::size_t rowBytes = mask ? (std::size_t(width) + 7) / 8 : std::size_t(width) * std::size_t(comps);
                if (pixels.size() < rowBytes * std::size_t(height)) { return; }
                const PObj& decode = file_.get(image, "Decode");
                const bool inverted = mask && decode.type == PObj::Arr && !decode.a.empty() && numberOf(&decode.a[0], 0) == 1.0;
                const BLRgba32 ink = state_.fill.color.toRgba32();
                for (int y = 0; y < height; ++y) {
                    std::uint32_t* row = reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(target.pixel_data) + std::ptrdiff_t(y) * target.stride);
                    const unsigned char* src = reinterpret_cast<const unsigned char*>(pixels.data()) + std::size_t(y) * rowBytes;
                    for (int x = 0; x < width; ++x) {
                        unsigned r, g, b, a = alpha.empty() ? 255u : (unsigned char)alpha[std::size_t(y) * std::size_t(width) + std::size_t(x)];
                        if (mask) {
                            const bool bit = (src[x / 8] >> (7 - (x % 8))) & 1;
                            const bool painted = inverted ? bit : !bit;
                            r = ink.r(); g = ink.g(); b = ink.b();
                            a = painted ? a : 0u;
                        } else if (comps == 1) {
                            r = g = b = src[x];
                        } else if (comps == 3) {
                            r = src[x * 3]; g = src[x * 3 + 1]; b = src[x * 3 + 2];
                        } else {
                            const unsigned k = 255u - src[x * 4 + 3];
                            r = (255u - src[x * 4]) * k / 255u; g = (255u - src[x * 4 + 1]) * k / 255u; b = (255u - src[x * 4 + 2]) * k / 255u;
                        }
                        row[x] = ((a) << 24) | ((r * a / 255u) << 16) | ((g * a / 255u) << 8) | (b * a / 255u);
                    }
                }
                BLImageCodec codec;
                codec.find_by_name("PNG");
                BLArray<uint8_t> buffer;
                if (out.write_to_data(buffer, codec) != BL_SUCCESS) { return; }
                data = decodeImage(std::string(reinterpret_cast<const char*>(buffer.data()), buffer.size()), "PDF image");
            }
            if (!data) { return; }
            // The image's unit square has its top at y = 1; the picture's pixels are y-down.
            BLMatrix2D placement(1.0 / double(data->width), 0.0, 0.0, -1.0 / double(data->height), 0.0, 1.0);
            placement.post_transform(state_.ctm);
            addObject(SketchObject::makeImageObject(data, placement));
        }

        void Interpreter::doXObject(const std::string& name, const PObj& resources) {
            const PObj* entry = file_.get(resources, "XObject").find(name);
            if (entry == nullptr) { return; }
            const PObj& xobject = file_.resolve(*entry);
            if (xobject.type != PObj::Dict) { return; }
            const PObj& subtype = file_.get(xobject, "Subtype");
            if (subtype.isName("Image")) {
                doImage(xobject, *entry);
                return;
            }
            if (subtype.isName("Form")) { drawForm(*entry, xobject, resources); }
        }

        void Interpreter::drawForm(const PObj& entry, const PObj& xobject, const PObj& resources) {
            if (depth_ >= 12) { return; }
            std::string content, rest;
            if (!file_.streamOf(entry, content, rest) || !rest.empty()) { return; }
            State saved = state_;
            const PObj& matrix = file_.get(xobject, "Matrix");
            if (matrix.type == PObj::Arr && matrix.a.size() >= 6) {
                BLMatrix2D m(numberOf(&matrix.a[0], 1), numberOf(&matrix.a[1], 0), numberOf(&matrix.a[2], 0), numberOf(&matrix.a[3], 1), numberOf(&matrix.a[4], 0), numberOf(&matrix.a[5], 0));
                m.post_transform(state_.ctm);
                state_.ctm = m;
            }
            const PObj& own = file_.get(xobject, "Resources");
            ++depth_;
            const PObj* savedResources = resources_;
            const std::size_t stackSize = stack_.size();
            run(content, own.type == PObj::Dict ? own : resources);
            stack_.resize(std::min(stack_.size(), stackSize));
            resources_ = savedResources;
            --depth_;
            flushClips(saved.clips.size());
            state_ = saved;
        }

        void Interpreter::execute(const std::string& op, std::vector<PObj>& args, const PObj& resources) {
            auto num = [&](std::size_t i) { return i < args.size() ? numberOf(&args[i], 0.0) : 0.0; };
            const std::size_t n = args.size();
            if (op == "q") {
                if (stack_.size() < 256) { stack_.push_back(state_); }
            } else if (op == "Q") {
                if (!stack_.empty()) {
                    flushClips(stack_.back().clips.size());
                    state_ = stack_.back();
                    stack_.pop_back();
                }
            } else if (op == "cm" && n >= 6) {
                BLMatrix2D m(num(n - 6), num(n - 5), num(n - 4), num(n - 3), num(n - 2), num(n - 1));
                m.post_transform(state_.ctm);
                state_.ctm = m;
            } else if (op == "w") { state_.lineWidth = num(n - 1 < n ? n - 1 : 0);
            } else if (op == "J") { state_.cap = int(num(n - 1 < n ? n - 1 : 0));
            } else if (op == "j") { state_.join = int(num(n - 1 < n ? n - 1 : 0));
            } else if (op == "M") { state_.miter = num(n - 1 < n ? n - 1 : 0);
            } else if (op == "d" && n >= 1) {
                state_.dash.clear();
                if (args[0].type == PObj::Arr) { for (const PObj& x : args[0].a) { state_.dash.push_back(numberOf(&x, 0.0)); } }
            } else if (op == "gs" && n >= 1) {
                applyGState(file_.get(file_.get(resources, "ExtGState"), args[0].s.c_str()));
            } else if (op == "m" && n >= 2) {
                BLPoint p; point(num(n - 2), num(n - 1), p);
                path_.move_to(p);
                open_ = true;
            } else if (op == "l" && n >= 2) {
                BLPoint p; point(num(n - 2), num(n - 1), p);
                if (path_.size() == 0) { path_.move_to(p); } else { path_.line_to(p); }
            } else if (op == "c" && n >= 6) {
                BLPoint a, b, c; point(num(n - 6), num(n - 5), a); point(num(n - 4), num(n - 3), b); point(num(n - 2), num(n - 1), c);
                if (path_.size() == 0) { path_.move_to(a); }
                path_.cubic_to(a, b, c);
            } else if ((op == "v" || op == "y") && n >= 4) {
                BLPoint cur(0, 0), a, b;
                if (path_.size() > 0) { path_.get_last_vertex(&cur); }
                point(num(n - 4), num(n - 3), a); point(num(n - 2), num(n - 1), b);
                if (path_.size() == 0) { path_.move_to(a); cur = a; }
                if (op == "v") { path_.cubic_to(cur, a, b); } else { path_.cubic_to(a, b, b); }
            } else if (op == "h") {
                if (path_.size() > 0) { path_.close(); }
            } else if (op == "re" && n >= 4) {
                rect(num(n - 4), num(n - 3), num(n - 2), num(n - 1));
            } else if (op == "S") { paint(false, true, false);
            } else if (op == "s") { paint(false, true, true);
            } else if (op == "f" || op == "F") { paint(true, false, false, false);
            } else if (op == "f*") { paint(true, false, false, true);
            } else if (op == "B") { paint(true, true, false, false);
            } else if (op == "B*") { paint(true, true, false, true);
            } else if (op == "b") { paint(true, true, true, false);
            } else if (op == "b*") { paint(true, true, true, true);
            } else if (op == "n") { paint(false, false, false);
            } else if (op == "W" || op == "W*") { pendingClip_ = true;
            } else if (op == "sh" && n >= 1) { shadingFill(args[0].s, resources);
            } else if (op == "g" || op == "rg" || op == "k") {
                state_.fill.space.kind = op == "g" ? Space::Gray : (op == "rg" ? Space::RGB : Space::CMYK);
                state_.fill.space.n = op == "g" ? 1 : (op == "rg" ? 3 : 4);
                setColorOperands(state_.fill, args);
            } else if (op == "G" || op == "RG" || op == "K") {
                state_.stroke.space.kind = op == "G" ? Space::Gray : (op == "RG" ? Space::RGB : Space::CMYK);
                state_.stroke.space.n = op == "G" ? 1 : (op == "RG" ? 3 : 4);
                setColorOperands(state_.stroke, args);
            } else if (op == "cs" && n >= 1) {
                state_.fill.space = spaceOf(args[0], resources);
                state_.fill.color = initialColor(state_.fill.space);
            } else if (op == "CS" && n >= 1) {
                state_.stroke.space = spaceOf(args[0], resources);
                state_.stroke.color = initialColor(state_.stroke.space);
            } else if (op == "sc" || op == "scn") { setColorOperands(state_.fill, args);
            } else if (op == "SC" || op == "SCN") { setColorOperands(state_.stroke, args);
            } else if (op == "Do" && n >= 1) {
                doXObject(args[0].s, resources);
            }
            // Everything else (text, marked content, ...) is skipped.
        }

        void Interpreter::run(const std::string& content, const PObj& resources) {
            resources_ = &resources;
            Lexer lexer(content);
            std::vector<PObj> args;
            while (!lexer.atEnd()) {
                PObj o = lexer.parse(false);
                if (o.type != PObj::Op) {
                    if (args.size() < 64) { args.push_back(std::move(o)); }
                    continue;
                }
                if (o.s.empty()) { continue; }
                if (++operations_ > 20000000) { return; }
                if (o.s == "BI") {   // an inline image: skip to its EI
                    std::size_t at = content.find("ID", lexer.pos());
                    at = at == std::string::npos ? std::string::npos : at + 2;
                    while (at != std::string::npos) {
                        at = content.find("EI", at);
                        if (at == std::string::npos) { break; }
                        const bool before = at == 0 || isSpace(content[at - 1]), after = at + 2 >= content.size() || isSpace(content[at + 2]);
                        at += 2;
                        if (before && after) { break; }
                    }
                    if (at == std::string::npos) { return; }
                    lexer.seek(at);
                    args.clear();
                    continue;
                }
                execute(o.s, args, resources);
                args.clear();
            }
        }

        // ---- pages -----------------------------------------------------------------------------------------------------------------------

        struct Page {
            double x0 = 0, y0 = 0, x1 = 612, y1 = 792;
            int rotate = 0;
            PObj resources;
            std::vector<PObj> contents;   // Refs
        };

        void collectPages(const PdfFile& file, const PObj& node, Page inherited, std::vector<Page>& pages, std::set<const PObj*>& seen, int depth) {
            const PObj& n = file.resolve(node);
            if (n.type != PObj::Dict || depth > 40 || !seen.insert(&n).second) { return; }
            if (const PObj* r = n.find("Resources")) { inherited.resources = file.resolve(*r); }
            const PObj& box = file.get(n, "CropBox").type == PObj::Arr ? file.get(n, "CropBox") : file.get(n, "MediaBox");
            if (box.type == PObj::Arr && box.a.size() >= 4) {
                const double a = numberOf(&file.resolve(box.a[0]), 0), b = numberOf(&file.resolve(box.a[1]), 0), c = numberOf(&file.resolve(box.a[2]), 612), d = numberOf(&file.resolve(box.a[3]), 792);
                inherited.x0 = std::min(a, c); inherited.x1 = std::max(a, c); inherited.y0 = std::min(b, d); inherited.y1 = std::max(b, d);
            }
            if (const PObj* r = n.find("Rotate")) { inherited.rotate = ((int(numberOf(&file.resolve(*r), 0)) % 360) + 360) % 360; }
            const PObj& kids = file.get(n, "Kids");
            if (kids.type == PObj::Arr) {
                for (const PObj& kid : kids.a) { collectPages(file, kid, inherited, pages, seen, depth + 1); }
                return;
            }
            Page page = inherited;
            const PObj* contents = n.find("Contents");
            if (contents != nullptr) {
                const PObj& c = file.resolve(*contents);
                if (contents->type == PObj::Ref && c.type != PObj::Arr) {
                    page.contents.push_back(*contents);
                } else if (c.type == PObj::Arr) {
                    page.contents = c.a;
                }
            }
            pages.push_back(std::move(page));
        }

        bool locatePages(const PdfFile& file, std::vector<Page>& pages) {
            std::set<const PObj*> seen;
            const PObj* catalog = nullptr;
            for (const auto& entry : file.objects()) {
                const PObj* type = entry.second.find("Type");
                if (type != nullptr && type->isName("Catalog")) { catalog = &entry.second; }
            }
            if (catalog != nullptr) {
                const PObj* root = catalog->find("Pages");
                if (root != nullptr) { collectPages(file, *root, Page(), pages, seen, 0); }
            }
            if (pages.empty()) {   // no usable catalog: the page objects themselves, in order
                for (const auto& entry : file.objects()) {
                    const PObj* type = entry.second.find("Type");
                    if (type != nullptr && type->isName("Page")) {
                        PObj ref;
                        ref.type = PObj::Ref;
                        ref.n = entry.first;
                        collectPages(file, ref, Page(), pages, seen, 0);
                    }
                }
            }
            return !pages.empty();
        }

    }

    bool isPdfDocument(const std::string& data) {
        return data.compare(0, 5, "%PDF-") == 0 || data.substr(0, 1024).find("%PDF-") != std::string::npos;
    }

    bool documentFromPdf(const std::string& data, SketchDocument& document) {
        try {
            if (!isPdfDocument(data) || data.find("/Encrypt") != std::string::npos) {
                return false;
            }
            PdfFile file;
            std::vector<Page> pages;
            if (!file.load(data) || !locatePages(file, pages)) {
                return false;
            }
            if (pages.size() > 500) { pages.resize(500); }

            SketchDocument::Batch batch(document);
            document.clearObjects();
            std::vector<Artboard> boards;
            double offset = 0.0;
            for (std::size_t i = 0; i < pages.size(); ++i) {
                const Page& page = pages[i];
                const double w = page.x1 - page.x0, h = page.y1 - page.y0;
                const bool turned = page.rotate == 90 || page.rotate == 270;
                Artboard board;
                board.name = "Page " + std::to_string(i + 1);
                board.x = offset;
                board.y = 0.0;
                board.width = std::max(1.0, turned ? h : w);
                board.height = std::max(1.0, turned ? w : h);
                boards.push_back(board);
                offset += board.width + 40.0;
            }
            document.setPageSize(float(boards[0].width), float(boards[0].height));
            document.setArtboards(boards, 0);
            document.setPageBackground(SketchColor::rgb(1.0f, 1.0f, 1.0f));
            document.setColorMode(ColorSpace::RGB);

            for (std::size_t i = 0; i < pages.size(); ++i) {
                const Page& page = pages[i];
                const double w = page.x1 - page.x0, h = page.y1 - page.y0;
                BLMatrix2D base;
                switch (page.rotate) {
                    case 90: base = BLMatrix2D(0, 1, 1, 0, -page.y0, -page.x0); break;
                    case 180: base = BLMatrix2D(-1, 0, 0, 1, w + page.x0, -page.y0); break;
                    case 270: base = BLMatrix2D(0, -1, -1, 0, h + page.y0, w + page.x0); break;
                    default: base = BLMatrix2D(1, 0, 0, -1, -page.x0, h + page.y0); break;
                }
                base.post_translate(boards[i].x, 0.0);
                std::string content;
                for (const PObj& ref : page.contents) {
                    std::string part, rest;
                    if (file.streamOf(ref, part, rest) && rest.empty()) {
                        content += part;
                        content += '\n';
                    }
                }
                Interpreter interpreter(file, document, base, boards[i].box());
                interpreter.run(content, page.resources);
                interpreter.finish();
            }
            document.setActiveArtboard(0);
            return true;
        } catch (...) {
            return false;
        }
    }

}
