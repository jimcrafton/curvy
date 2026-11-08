#include "curvy/graphbrush.h"

#include "curvy/pathflatten.h"

#include <jitnodes/geo/geometry.h>
#include <jitnodes/graph/network.h>
#include <jitnodes/graph/node.h>
#include <jitnodes/graph/serialize.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <exception>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace curvy {

    namespace {
        std::vector<GraphBrushInfo>& library() {
            static std::vector<GraphBrushInfo> brushes;
            return brushes;
        }

        bool endsWith(const std::string& text, const std::string& tail) {
            return text.size() >= tail.size() && text.compare(text.size() - tail.size(), tail.size(), tail) == 0;
        }
    }

    bool isHostBrushParam(const std::string& name) {
        for (const char* host : {kBrushReferenceWidthParam, kBrushGuideLengthParam, kBrushGuideStartParam, kBrushStrokeLengthParam,
                 kBrushGuideMinXParam, kBrushGuideMinYParam, kBrushGuideMaxXParam, kBrushGuideMaxYParam, kBrushSettleTimeParam}) {
            if (name == host) {
                return true;
            }
        }
        return false;
    }

    namespace {
        // "dryStart" -> "Dry start"
        std::string settingLabel(const std::string& name) {
            std::string label;
            for (char c : name) {
                if (std::isupper(static_cast<unsigned char>(c)) && !label.empty()) {
                    label += ' ';
                    label += char(std::tolower(static_cast<unsigned char>(c)));
                } else {
                    label += label.empty() ? char(std::toupper(static_cast<unsigned char>(c))) : c;
                }
            }
            return label;
        }

        // What a registered asset type is as a brush; nothing for a plain asset (no problem text) or one that cannot be a brush (with the reason).
        std::optional<GraphBrushInfo> brushInfoOf(const std::string& type, const std::string& path, std::string* problem) {
            const std::unique_ptr<jitnodes::Node> probe = jitnodes::NodeRegistry::instance().create(type);
            if (!probe || !probe->hasFloat(kBrushReferenceWidthParam) || !probe->hasFloat(kBrushGuideLengthParam)) {
                return std::nullopt;
            }
            GraphBrushInfo info;
            info.type = type;
            info.path = path;
            info.referenceWidth = double(probe->baseFloat(kBrushReferenceWidthParam));
            info.hasTintable = probe->hasFloat(kBrushTintableParam);
            info.tintableDefault = info.hasTintable ? double(probe->baseFloat(kBrushTintableParam)) : 0.0;
            info.simulation = probe->hasFloat(kBrushSettleTimeParam);
            info.settleTime = info.simulation ? double(probe->baseFloat(kBrushSettleTimeParam)) : 0.0;
            for (const std::string& name : probe->spareNames()) {
                if (!isHostBrushParam(name)) {
                    info.settings.push_back({name, settingLabel(name), double(probe->baseFloat(name))});
                }
            }
            if (!(info.referenceWidth > 0.0)) {
                if (problem != nullptr) {
                    *problem = "referenceWidth must be above 0";
                }
                return std::nullopt;
            }
            return info;
        }

        // Brushes that came with a document's graph and are not in the brush folder. A map, so a pointer from findGraphBrush stays valid when
        // another is added while the cook threads work.
        std::mutex& embeddedMutex() {
            static std::mutex m;
            return m;
        }

        std::map<std::string, GraphBrushInfo>& embeddedBrushes() {
            static std::map<std::string, GraphBrushInfo> brushes;
            return brushes;
        }
    }

    void registerEmbeddedBrushes() {
        for (const std::string& type : jitnodes::NodeRegistry::instance().assetNames()) {
            if (findGraphBrush(type) != nullptr) {
                continue;
            }
            if (std::optional<GraphBrushInfo> info = brushInfoOf(type, std::string(), nullptr)) {
                std::lock_guard<std::mutex> guard(embeddedMutex());
                embeddedBrushes().emplace(type, std::move(*info));
            }
        }
    }

    std::vector<GraphBrushInfo> loadBrushAssets(const std::string& folder, std::vector<std::string>* problems) {
        namespace fs = std::filesystem;
        std::vector<GraphBrushInfo> found;
        std::error_code error;
        std::vector<fs::path> files;
        for (fs::directory_iterator it(fs::path(folder), error), end; !error && it != end; it.increment(error)) {
            if (it->is_regular_file(error) && endsWith(it->path().filename().string(), ".asset.json")) {
                files.push_back(it->path());
            }
        }
        std::sort(files.begin(), files.end());
        for (const fs::path& file : files) {
            // The type is the file name up to the first dot, which is also how jitnodes names it.
            const std::string type = file.filename().string().substr(0, file.filename().string().find('.'));
            try {
                jitnodes::registerAssetFromFile(file.string());
                std::string problem;
                if (std::optional<GraphBrushInfo> info = brushInfoOf(type, file.string(), &problem)) {
                    found.push_back(std::move(*info));
                } else if (!problem.empty() && problems) {
                    problems->push_back(file.filename().string() + ": " + problem);
                }
            } catch (const std::exception& e) {
                if (problems) {
                    problems->push_back(file.filename().string() + ": " + e.what());
                }
            }
        }
        library() = found;
        return found;
    }

    const std::vector<GraphBrushInfo>& graphBrushes() {
        return library();
    }

    const GraphBrushInfo* findGraphBrush(const std::string& type) {
        for (const GraphBrushInfo& info : library()) {
            if (info.type == type) {
                return &info;
            }
        }
        std::lock_guard<std::mutex> guard(embeddedMutex());
        const auto found = embeddedBrushes().find(type);
        return found != embeddedBrushes().end() ? &found->second : nullptr;
    }

    namespace {
        constexpr std::size_t kMaxCached = 128;
        constexpr std::size_t kMaxShapes = 400000;   // dots plus polygons per stroke: a runaway brush must not take the editor down
        constexpr double kChunkUnits = 24.0;         // graph units of guide per chunk; a longer stroke is cooked in several
        constexpr std::size_t kMaxJobs = 256;

        std::uint64_t mix(std::uint64_t h, const void* data, std::size_t bytes) {
            const unsigned char* p = static_cast<const unsigned char*>(data);
            for (std::size_t i = 0; i < bytes; ++i) {
                h = (h ^ p[i]) * 1099511628211ull;
            }
            return h;
        }

        // The cache and the queue of the background cook share this lock. A null entry is a stroke the brush made nothing of.
        std::mutex& lock() {
            static std::mutex m;
            return m;
        }

        std::unordered_map<std::uint64_t, std::shared_ptr<const GraphBrushResult>>& cache() {
            static std::unordered_map<std::uint64_t, std::shared_ptr<const GraphBrushResult>> entries;
            return entries;
        }

        std::uint32_t colorOf(float r, float g, float b, float a) {
            auto byte = [](float v) { return std::uint32_t(std::lround(std::min(1.0f, std::max(0.0f, v)) * 255.0f)); };
            return (byte(a) << 24) | (byte(r) << 16) | (byte(g) << 8) | byte(b);
        }

        // The pen pressure at fraction `t` of the stroke (both sides alike, as the nib and the copies of the other brushes read it).
        double pressureAt(const std::vector<WidthPoint>& widths, double t) {
            return widths.empty() ? 1.0 : 0.5 * (widthScaleAt(widths, t, true) + widthScaleAt(widths, t, false));
        }

        // The tailTaper setting of a stroke: its own value, else the asset's default; 0 when the asset has none.
        double tailTaperOf(const std::string& type, const GraphBrushSettings& settings) {
            for (const auto& [name, value] : settings) {
                if (name == kBrushTailTaperParam) {
                    return std::clamp(value, 0.0, 1.0);
                }
            }
            const GraphBrushInfo* info = findGraphBrush(type);
            if (info != nullptr) {
                for (const GraphBrushSetting& s : info->settings) {
                    if (s.name == kBrushTailTaperParam) {
                        return std::clamp(s.defaultValue, 0.0, 1.0);
                    }
                }
            }
            return 0.0;
        }

        // 1 until the last `taper` of the stroke, then a smooth fall to a tenth at the end.
        double tailScale(double t, double taper) {
            if (!(taper > 0.0) || t <= 1.0 - taper) {
                return 1.0;
            }
            const double u = std::min(1.0, (t - (1.0 - taper)) / taper);
            return 1.0 - 0.9 * u * u * (3.0 - 2.0 * u);
        }

        // Points first..last of a subpath: a chunk of a long stroke. Neighbours share their end point.
        struct ChunkSpec {
            std::size_t piece = 0;
            std::size_t first = 0;
            std::size_t last = 0;
        };

        // What a worker thread shows about the chunk it is cooking: how far the cook has got (0..1) and whether it was told to stop.
        struct RunningChunk {
            std::atomic<float> fraction{0.0f};
            std::atomic<bool> cancel{false};
            std::uint64_t key = 0;   // the stroke it belongs to
        };

        // Called by a cook thread whenever its cook reports; about every 150 ms it drops the strokes nobody asks for any more and tells the UI.
        void graphBrushTick();

        // The chunk of a subpath through the asset; the result is added to `into` relative to `global`, where the whole stroke starts.
        // The asset is told how far along the stroke the chunk begins (`guideStart`, when it has one) so what depends on the distance travelled
        // (running dry, the colour drift) carries on across chunks.
        void cookOne(const std::string& type, const MeasuredPath& m, const ChunkSpec& chunk, const BLPoint& global, double k,
            const std::vector<WidthPoint>& widths, const GraphBrushSettings& settings, GraphBrushResult& into, RunningChunk* run = nullptr) {
            const std::size_t n = chunk.last - chunk.first + 1;
            const double startLength = m.at[chunk.first];
            const double length = m.at[chunk.last] - startLength;
            jitnodes::Geometry guide;
            guide.resizePoints(n);
            float* gx = guide.x();
            float* gy = guide.y();
            float* gw = guide.pointAttrib("w").data();
            const BLPoint origin = m.points[chunk.first];   // the guide starts at the origin, so a brush looks the same wherever it is drawn
            const double taper = widths.empty() ? tailTaperOf(type, settings) : 0.0;
            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t at = chunk.first + i;
                gx[i] = float((m.points[at].x - origin.x) / k);
                gy[i] = float((m.points[at].y - origin.y) / k);
                const double t = m.at[at] / m.total;
                gw[i] = float(pressureAt(widths, t) * tailScale(t, taper));
            }
            jitnodes::Primitive line;
            line.start = 0;
            line.count = std::uint32_t(n);
            guide.addPrimitive(line);

            jitnodes::Network net(1);   // one thread: the chunks run in parallel with each other, not inside
            const jitnodes::NodeId source = net.addNode("geometry");
            net.node(source).setGeometryParam("geometry", std::make_shared<const jitnodes::Geometry>(std::move(guide)));
            const jitnodes::NodeId brush = net.addNode(type);
            for (const auto& [name, value] : settings) {
                if (!isHostBrushParam(name) && net.node(brush).hasFloat(name)) {
                    net.node(brush).setFloat(name, float(value));
                }
            }
            net.node(brush).setFloat(kBrushGuideLengthParam, float(length / k));
            if (net.node(brush).hasFloat(kBrushGuideMinXParam)) {
                float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
                for (std::size_t i = 0; i < n; ++i) {
                    x0 = std::min(x0, gx[i]);
                    y0 = std::min(y0, gy[i]);
                    x1 = std::max(x1, gx[i]);
                    y1 = std::max(y1, gy[i]);
                }
                net.node(brush).setFloat(kBrushGuideMinXParam, x0);
                net.node(brush).setFloat(kBrushGuideMinYParam, y0);
                net.node(brush).setFloat(kBrushGuideMaxXParam, x1);
                net.node(brush).setFloat(kBrushGuideMaxYParam, y1);
            }
            if (net.node(brush).hasFloat(kBrushStrokeLengthParam)) {
                net.node(brush).setFloat(kBrushStrokeLengthParam, float(m.total / k));
            }
            if (net.node(brush).hasFloat(kBrushGuideStartParam)) {
                net.node(brush).setFloat(kBrushGuideStartParam, float(startLength / k));
            }
            net.connect(source, brush);
            net.setDisplayNode(brush);
            if (const GraphBrushInfo* info = findGraphBrush(type); info != nullptr && info->simulation) {
                net.setTime(info->settleTime);
            }
            if (run != nullptr) {
                if (run->cancel) {
                    throw jitnodes::CookCancelled();
                }
                // Called on this thread at every node start and finish and at every simulation step; the cancel takes effect at the next one.
                net.setProgressCallback([run, &net](const jitnodes::Network::CookProgress& p) {
                    const double inNode = p.fraction < 1.0 ? p.fraction : 0.0;
                    run->fraction = float(std::min(1.0, (double(p.nodesDone) + inNode) / double(std::max<std::size_t>(1, p.nodesTotal))));
                    if (run->cancel) {
                        net.cancel();
                    }
                    graphBrushTick();
                });
            }
            const jitnodes::GeometryHandle out = net.cookDisplay();
            if (!out || out->pointCount() == 0 || !out->hasPointAttrib("x") || !out->hasPointAttrib("y")) {
                return;
            }
            const float* x = out->x();
            const float* y = out->y();
            auto channel = [&](const char* name) { const auto* a = out->findPointAttrib(name); return a ? a->data() : nullptr; };
            const float *cr = channel("Cd.x"), *cg = channel("Cd.y"), *cb = channel("Cd.z"), *alpha = channel("alpha"), *radius = channel("radius");
            const float* softness = channel("softness");
            auto colorAt = [&](std::size_t i) {
                return colorOf(cr ? cr[i] : 0.0f, cg ? cg[i] : 0.0f, cb ? cb[i] : 0.0f, alpha ? alpha[i] : 1.0f);
            };
            if (radius != nullptr) {
                for (std::size_t i = 0; i < out->pointCount() && into.dots.size() + into.polygons.size() < kMaxShapes; ++i) {
                    if (alpha != nullptr && alpha[i] <= 0.004f) {
                        continue;   // a dot the brush leaves out (dry bristles)
                    }
                    GraphDot dot;
                    dot.x = origin.x - global.x + double(x[i]) * k;
                    dot.y = origin.y - global.y + double(y[i]) * k;
                    dot.radius = double(radius[i]) * k;
                    dot.color = colorAt(i);
                    dot.softness = softness ? std::clamp(softness[i], 0.0f, 1.0f) : 0.0f;
                    into.dots.push_back(dot);
                }
                return;
            }
            for (const jitnodes::Primitive& prim : out->primitives()) {
                if (prim.kind != jitnodes::PrimKind::polyline || prim.count < 3 || into.dots.size() + into.polygons.size() >= kMaxShapes) {
                    continue;
                }
                GraphPolygon polygon;
                for (std::uint32_t j = 0; j < prim.count; ++j) {
                    const BLPoint p(origin.x - global.x + double(x[prim.start + j]) * k, origin.y - global.y + double(y[prim.start + j]) * k);
                    if (j == 0) {
                        polygon.path.move_to(p);
                    } else {
                        polygon.path.line_to(p);
                    }
                }
                if (prim.closed) {
                    polygon.path.close();
                }
                polygon.color = colorAt(prim.start);   // a shape's colour is that of its first point
                into.polygons.push_back(std::move(polygon));
            }
        }
    }

    namespace {
        // Everything a cook needs, worked out from the path once.
        struct Plan {
            std::string type;
            std::vector<WidthPoint> widths;
            GraphBrushSettings settings;
            std::vector<MeasuredPath> pieces;
            std::vector<ChunkSpec> chunks;
            BLPoint global = BLPoint(0, 0);
            double k = 1.0;
            std::uint64_t key = 0;
        };

        std::uint64_t planKey(const Plan& plan, double strokeWidth);

        // Each subpath is split into chunks of about kChunkUnits of guide, at points of the flattened path.
        std::vector<ChunkSpec> chunksOf(const std::vector<MeasuredPath>& pieces, double k, bool whole) {
            std::vector<ChunkSpec> chunks;
            for (std::size_t p = 0; p < pieces.size(); ++p) {
                const MeasuredPath& m = pieces[p];
                if (m.points.size() < 2 || !(m.total > 0.0)) {
                    continue;
                }
                if (whole) {   // a simulation runs over the whole stroke
                    chunks.push_back({p, 0, m.points.size() - 1});
                    continue;
                }
                const std::size_t count = std::max<std::size_t>(1, std::size_t(std::ceil(m.total / (kChunkUnits * k))));
                std::size_t first = 0;
                for (std::size_t c = 1; c <= count; ++c) {
                    std::size_t last = m.points.size() - 1;
                    if (c < count) {
                        const double target = m.total * double(c) / double(count);
                        last = std::size_t(std::lower_bound(m.at.begin(), m.at.end(), target) - m.at.begin());
                        last = std::min(last, m.points.size() - 1);
                    }
                    if (last > first) {
                        chunks.push_back({p, first, last});
                        first = last;
                    }
                }
            }
            return chunks;
        }

        bool makePlan(const std::string& type, const BLPath& path, double strokeWidth, const std::vector<WidthPoint>& widths,
            const GraphBrushSettings& settings, Plan& plan) {
            const GraphBrushInfo* info = findGraphBrush(type);
            if (info == nullptr || !(strokeWidth > 0.0) || !std::isfinite(strokeWidth)) {
                return false;
            }
            plan.type = type;
            plan.widths = widths;
            for (const auto& [name, value] : settings) {   // only the asset's own settings count, so a stray name does not split the cache
                const auto known = std::find_if(info->settings.begin(), info->settings.end(), [&](const GraphBrushSetting& s) { return s.name == name; });
                if (known != info->settings.end()) {
                    plan.settings.emplace_back(name, value);
                }
            }
            plan.pieces = measurePath(path);
            plan.k = strokeWidth / info->referenceWidth;
            plan.chunks = chunksOf(plan.pieces, plan.k, info->simulation);
            for (const MeasuredPath& m : plan.pieces) {
                if (m.points.size() >= 2 && m.total > 0.0) {
                    plan.global = m.points.front();
                    break;
                }
            }
            plan.key = planKey(plan, strokeWidth);
            return true;
        }

        // Null when the cook was cancelled: the caller must not keep that as the chunk's paint.
        std::shared_ptr<const GraphBrushResult> cookChunk(const Plan& plan, std::size_t index, RunningChunk* run = nullptr) {
            auto fragment = std::make_shared<GraphBrushResult>();
            try {
                const ChunkSpec& chunk = plan.chunks[index];
                cookOne(plan.type, plan.pieces[chunk.piece], chunk, plan.global, plan.k, plan.widths, plan.settings, *fragment, run);
            } catch (const jitnodes::CookCancelled&) {
                return nullptr;
            } catch (const std::exception&) {
                // a brush that fails to cook draws nothing for that chunk
            }
            return fragment;
        }

        // The fragments in order as one result, with the box of everything for the cached image; null when nothing came out.
        std::shared_ptr<const GraphBrushResult> joinChunks(const std::vector<std::shared_ptr<const GraphBrushResult>>& fragments) {
            auto result = std::make_shared<GraphBrushResult>();
            for (const auto& fragment : fragments) {
                if (!fragment) {
                    continue;
                }
                for (const GraphDot& dot : fragment->dots) {
                    if (result->dots.size() + result->polygons.size() < kMaxShapes) {
                        result->dots.push_back(dot);
                    }
                }
                for (const GraphPolygon& polygon : fragment->polygons) {
                    if (result->dots.size() + result->polygons.size() < kMaxShapes) {
                        result->polygons.push_back(polygon);
                    }
                }
            }
            if (result->dots.empty() && result->polygons.empty()) {
                return nullptr;
            }
            double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
            for (const GraphDot& dot : result->dots) {
                x0 = std::min(x0, dot.x - dot.radius);
                y0 = std::min(y0, dot.y - dot.radius);
                x1 = std::max(x1, dot.x + dot.radius);
                y1 = std::max(y1, dot.y + dot.radius);
            }
            for (const GraphPolygon& polygon : result->polygons) {
                BLBox box(0, 0, 0, 0);
                if (polygon.path.get_bounding_box(&box) == BL_SUCCESS) {
                    x0 = std::min(x0, box.x0);
                    y0 = std::min(y0, box.y0);
                    x1 = std::max(x1, box.x1);
                    y1 = std::max(y1, box.y1);
                }
            }
            result->bounds = BLBox(x0, y0, x1, y1);
            return result;
        }

        // Caller holds lock().
        void storeLocked(std::uint64_t key, std::shared_ptr<const GraphBrushResult> result) {
            if (cache().size() >= kMaxCached) {
                cache().clear();
            }
            cache()[key] = std::move(result);
        }

        // The background cook: a few threads taking chunks, newest request first (a scrubbed weight asks for many strokes and only the last matters).
        struct Task {
            std::shared_ptr<Plan> plan;
            std::shared_ptr<std::vector<std::shared_ptr<const GraphBrushResult>>> fragments;
            std::shared_ptr<std::atomic<std::size_t>> remaining;
            std::size_t index = 0;
        };

        struct Pool {
            std::condition_variable wake;
            std::vector<Task> queue;
            // The strokes being cooked, with when a paint last asked for each. A stroke nobody asks for within kLease is stale (deleted, moved off
            // screen, or drawn with another weight since): its queued chunks are dropped and its running ones stopped.
            std::unordered_map<std::uint64_t, std::chrono::steady_clock::time_point> pending;
            static constexpr std::chrono::milliseconds kLease{3000};
            std::atomic<std::int64_t> lastTickMs{0};
            std::size_t chunksTotal = 0, chunksDone = 0;   // since pending was last empty
            std::vector<std::shared_ptr<RunningChunk>> running;
            std::unordered_set<std::uint64_t> cancelled;   // strokes the user stopped: a hairline until they are edited (a new key)
            std::vector<std::thread> threads;
            std::function<void()> ready;
            bool stop = false;

            ~Pool() {
                {
                    std::lock_guard<std::mutex> guard(lock());
                    stop = true;
                    queue.clear();
                    for (const auto& chunk : running) {
                        chunk->cancel = true;   // a long simulation must not hold up the exit
                    }
                }
                wake.notify_all();
                for (std::thread& t : threads) {
                    t.join();
                }
            }

            void start() {   // caller holds lock()
                if (!threads.empty()) {
                    return;
                }
                const unsigned hardware = std::thread::hardware_concurrency();
                const unsigned count = std::max(1u, std::min(6u, hardware > 2 ? hardware - 2 : 1u));
                for (unsigned i = 0; i < count; ++i) {
                    threads.emplace_back([this] { run(); });
                }
            }

            // Caller holds lock().
            void expireStaleLocked() {
                const auto now = std::chrono::steady_clock::now();
                for (auto it = pending.begin(); it != pending.end();) {
                    if (now - it->second <= kLease) {
                        ++it;
                        continue;
                    }
                    const std::uint64_t key = it->first;
                    it = pending.erase(it);
                    const std::size_t before = queue.size();
                    queue.erase(std::remove_if(queue.begin(), queue.end(), [key](const Task& t) { return t.plan->key == key; }), queue.end());
                    std::size_t gone = before - queue.size();
                    for (const auto& chunk : running) {
                        if (chunk->key == key && !chunk->cancel) {
                            chunk->cancel = true;
                            ++gone;
                        }
                    }
                    chunksTotal = chunksTotal > gone ? chunksTotal - gone : 0;
                }
            }

            void tick() {
                const std::int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                std::int64_t last = lastTickMs.load();
                if (ms - last < 150 || !lastTickMs.compare_exchange_strong(last, ms)) {
                    return;
                }
                {
                    std::lock_guard<std::mutex> guard(lock());
                    expireStaleLocked();
                }
                notifyReady();
            }

            void notifyReady() {
                std::function<void()> handler;
                {
                    std::lock_guard<std::mutex> guard(lock());
                    handler = ready;
                }
                if (handler) {
                    handler();
                }
            }

            void run() {
                for (;;) {
                    Task task;
                    {
                        std::unique_lock<std::mutex> guard(lock());
                        wake.wait(guard, [this] { return stop || !queue.empty(); });
                        if (stop) {
                            return;
                        }
                        task = std::move(queue.back());
                        queue.pop_back();
                    }
                    auto chunk = std::make_shared<RunningChunk>();
                    chunk->key = task.plan->key;
                    {
                        std::lock_guard<std::mutex> guard(lock());
                        running.push_back(chunk);
                    }
                    auto fragment = cookChunk(*task.plan, task.index, chunk.get());
                    {
                        std::lock_guard<std::mutex> guard(lock());
                        running.erase(std::find(running.begin(), running.end(), chunk));
                    }
                    if (!fragment || chunk->cancel) {
                        continue;   // stopped: cancelGraphBrushCooks already forgot the stroke
                    }
                    (*task.fragments)[task.index] = std::move(fragment);
                    const bool last = task.remaining->fetch_sub(1) == 1;
                    std::shared_ptr<const GraphBrushResult> result;
                    if (last) {
                        result = joinChunks(*task.fragments);
                    }
                    {
                        std::lock_guard<std::mutex> guard(lock());
                        ++chunksDone;
                        if (last) {
                            storeLocked(task.plan->key, std::move(result));
                            pending.erase(task.plan->key);
                        }
                    }
                    notifyReady();
                }
            }
        };

        Pool& pool() {
            static Pool instance;
            return instance;
        }

        void graphBrushTick() {
            pool().tick();
        }
    }

    namespace {
        thread_local int blockingDepth = 0;
    }

    GraphBrushBlockingScope::GraphBrushBlockingScope() {
        ++blockingDepth;
    }

    GraphBrushBlockingScope::~GraphBrushBlockingScope() {
        --blockingDepth;
    }

    void setGraphBrushReadyHandler(std::function<void()> handler) {
        std::lock_guard<std::mutex> guard(lock());
        pool().ready = std::move(handler);
    }

    GraphBrushProgress graphBrushProgress() {
        std::lock_guard<std::mutex> guard(lock());
        const Pool& p = pool();
        GraphBrushProgress progress;
        progress.strokes = p.pending.size();
        progress.total = p.chunksTotal;
        progress.done = std::min(p.chunksDone, p.chunksTotal);
        double sum = double(progress.done);
        for (const auto& chunk : p.running) {
            if (!chunk->cancel) {
                sum += chunk->fraction.load();
            }
        }
        progress.fraction = progress.total > 0 ? std::min(1.0, sum / double(progress.total)) : 0.0;
        return progress;
    }

    void cancelGraphBrushCooks() {
        {
            std::lock_guard<std::mutex> guard(lock());
            Pool& p = pool();
            p.queue.clear();
            for (const auto& entry : p.pending) {
                p.cancelled.insert(entry.first);
            }
            p.pending.clear();
            p.chunksTotal = p.chunksDone = 0;
            for (const auto& chunk : p.running) {
                chunk->cancel = true;
            }
            if (p.cancelled.size() > 1024) {
                p.cancelled.clear();
            }
        }
        pool().notifyReady();
    }

    GraphBrushCook cookGraphBrushLater(const std::string& type, const BLPath& path, double strokeWidth, const std::vector<WidthPoint>& widths,
        bool* pending, const GraphBrushSettings& settings) {
        if (pending) {
            *pending = false;
        }
        auto plan = std::make_shared<Plan>();
        if (!makePlan(type, path, strokeWidth, widths, settings, *plan)) {
            return {};
        }
        bool dropped = false, queued = false;
        {
            std::lock_guard<std::mutex> guard(lock());
            auto found = cache().find(plan->key);
            if (found != cache().end()) {
                return found->second ? GraphBrushCook{found->second, plan->global} : GraphBrushCook{};
            }
            Pool& p = pool();
            if (p.ready == nullptr || blockingDepth > 0) {
                dropped = true;   // nobody to tell when it is done: the caller has to wait for it
            } else if (p.cancelled.count(plan->key) != 0) {
                return {};   // stopped by the user: a hairline until the stroke is edited, and nothing is coming
            } else if (p.pending.count(plan->key) == 0) {
                if (p.pending.size() >= kMaxJobs) {
                    // far more strokes than can be cooked soon: forget the queue, the ones still on screen ask again when the repaint comes
                    p.queue.clear();
                    p.pending.clear();
                    dropped = true;
                }
                p.start();
                if (p.pending.empty()) {
                    p.chunksTotal = p.chunksDone = 0;
                }
                p.chunksTotal += plan->chunks.size();
                p.pending[plan->key] = std::chrono::steady_clock::now();
                auto fragments = std::make_shared<std::vector<std::shared_ptr<const GraphBrushResult>>>(plan->chunks.size());
                auto remaining = std::make_shared<std::atomic<std::size_t>>(plan->chunks.size());
                if (plan->chunks.empty()) {
                    storeLocked(plan->key, nullptr);
                    p.pending.erase(plan->key);
                    return {};
                }
                for (std::size_t i = 0; i < plan->chunks.size(); ++i) {
                    p.queue.push_back(Task{plan, fragments, remaining, i});
                }
                p.wake.notify_all();
                queued = true;
            } else {
                p.pending[plan->key] = std::chrono::steady_clock::now();   // asked again: still wanted
            }
            if (pending) {
                *pending = !dropped;
            }
        }
        if (queued) {
            pool().notifyReady();   // a status display shows the new work
        }
        if (dropped) {
            return cookGraphBrush(type, path, strokeWidth, widths, settings);
        }
        return {};
    }

    GraphBrushCook cookGraphBrush(const std::string& type, const BLPath& path, double strokeWidth, const std::vector<WidthPoint>& widths,
        const GraphBrushSettings& settings) {
        Plan plan;
        if (!makePlan(type, path, strokeWidth, widths, settings, plan)) {
            return {};
        }
        {
            std::lock_guard<std::mutex> guard(lock());
            auto found = cache().find(plan.key);
            if (found != cache().end()) {
                return found->second ? GraphBrushCook{found->second, plan.global} : GraphBrushCook{};
            }
        }
        std::vector<std::shared_ptr<const GraphBrushResult>> fragments(plan.chunks.size());
        for (std::size_t i = 0; i < plan.chunks.size(); ++i) {
            fragments[i] = cookChunk(plan, i);
        }
        auto result = joinChunks(fragments);
        {
            std::lock_guard<std::mutex> guard(lock());
            storeLocked(plan.key, result);
        }
        return result ? GraphBrushCook{result, plan.global} : GraphBrushCook{};
    }

    namespace {
        std::uint64_t planKey(const Plan& plan, double strokeWidth) {
            const std::string& type = plan.type;
            const std::vector<WidthPoint>& widths = plan.widths;
            const std::vector<MeasuredPath>& pieces = plan.pieces;
            const BLPoint global = plan.global;
        std::uint64_t key = 14695981039346656037ull;
        key = mix(key, type.data(), type.size());
        const std::size_t revision = jitnodes::NodeRegistry::instance().assetRevision(type);
        key = mix(key, &revision, sizeof revision);
        key = mix(key, &strokeWidth, sizeof strokeWidth);
        for (const auto& [name, value] : plan.settings) {
            key = mix(key, name.data(), name.size());
            key = mix(key, &value, sizeof value);
        }
        for (const WidthPoint& w : widths) {
            key = mix(key, &w.t, sizeof w.t);
            key = mix(key, &w.left, sizeof w.left);
            key = mix(key, &w.right, sizeof w.right);
        }
        // The shape of the path, not its place: points relative to the start, in 1/256 units, so a moved stroke finds its cook again.
        for (const MeasuredPath& m : pieces) {
            for (const BLPoint& p : m.points) {
                const std::int64_t rel[2] = {std::int64_t(std::llround((p.x - global.x) * 256.0)), std::int64_t(std::llround((p.y - global.y) * 256.0))};
                key = mix(key, rel, sizeof rel);
            }
        }
            return key;
        }
    }

    void paintGraphBrush(BLContext& ctx, const GraphBrushCook& cook) {
        if (!cook) {
            return;
        }
        const GraphBrushResult& result = *cook.shapes;
        const BLBox& bounds = result.bounds;
        if (!(bounds.x1 > bounds.x0) || !(bounds.y1 > bounds.y0)) {
            return;
        }
        // Pixels per unit the context draws at; the image is made at the next step of 1.25 above it, so a smooth zoom does not redraw it every
        // frame and it is never stretched up. A very large stroke gets a coarser image rather than a huge one.
        const BLMatrix2D& f = ctx.final_transform();
        const double device = std::sqrt(std::fabs(f.m00 * f.m11 - f.m01 * f.m10));
        double scale = std::pow(1.25, std::ceil(std::log(std::max(device, 1e-3)) / std::log(1.25)));
        const double pad = 2.0;   // pixels of margin so the edge dots are not cut
        const double width = bounds.x1 - bounds.x0, height = bounds.y1 - bounds.y0;
        const double limit = std::min(4096.0, std::sqrt(16.0e6));
        scale = std::min(scale, std::min((limit - 2.0 * pad) / width, (limit - 2.0 * pad) / height));
        GraphBrushResult::Raster& raster = result.raster;
        if (raster.scale <= 0.0 || std::fabs(raster.scale - scale) > 1e-9 * scale) {
            const int w = int(std::ceil(width * scale + 2.0 * pad)), h = int(std::ceil(height * scale + 2.0 * pad));
            BLImage image;
            if (image.create(w, h, BL_FORMAT_PRGB32) != BL_SUCCESS) {
                return;
            }
            BLContext c(image);
            c.clear_all();
            c.scale(scale);
            c.translate(-bounds.x0 + pad / scale, -bounds.y0 + pad / scale);
            for (const GraphPolygon& polygon : result.polygons) {
                c.set_fill_style(BLRgba32(polygon.color));
                c.fill_path(polygon.path);
            }
            // The stops follow jitnodes' splat profile (smooth falloff over the outer `softness` of the radius).
            static const float kFall[] = {1.0f, 0.84375f, 0.5f, 0.15625f, 0.0f};
            for (const GraphDot& dot : result.dots) {
                if (dot.softness < 0.01f) {
                    c.set_fill_style(BLRgba32(dot.color));
                    c.fill_circle(BLCircle(dot.x, dot.y, dot.radius));
                    continue;
                }
                BLGradient gradient(BLRadialGradientValues(dot.x, dot.y, dot.x, dot.y, dot.radius));
                const std::uint32_t alpha = dot.color >> 24;
                const double inner = 1.0 - double(dot.softness);
                gradient.add_stop(0.0, BLRgba32((dot.color & 0x00FFFFFFu) | (alpha << 24)));
                for (int s = 0; s < 5; ++s) {
                    const std::uint32_t a = std::uint32_t(std::lround(double(alpha) * kFall[s]));
                    gradient.add_stop(inner + double(dot.softness) * (s / 4.0), BLRgba32((dot.color & 0x00FFFFFFu) | (a << 24)));
                }
                c.set_fill_style(gradient);
                c.fill_circle(BLCircle(dot.x, dot.y, dot.radius));
            }
            c.end();
            raster.image = std::move(image);
            raster.box = BLBox(bounds.x0 - pad / scale, bounds.y0 - pad / scale, bounds.x0 - pad / scale + w / scale, bounds.y0 - pad / scale + h / scale);
            raster.scale = scale;
        }
        ctx.save();
        ctx.translate(cook.origin.x, cook.origin.y);
        ctx.set_pattern_quality(BL_PATTERN_QUALITY_BILINEAR);
        ctx.blit_image(BLRect(raster.box.x0, raster.box.y0, raster.box.x1 - raster.box.x0, raster.box.y1 - raster.box.y0), raster.image);
        ctx.restore();
    }

    std::vector<BrushPart> graphBrushPartsTinted(const GraphBrushCook& cook, float tr, float tg, float tb, float ta);

    bool graphBrushTinted(const std::string& type, const GraphBrushSettings& settings) {
        const GraphBrushInfo* info = findGraphBrush(type);
        if (info == nullptr || !info->hasTintable) {
            return false;
        }
        double value = info->tintableDefault;
        for (const auto& [name, changed] : settings) {
            if (name == kBrushTintableParam) {
                value = changed;
            }
        }
        return value >= 0.5;
    }

    void paintGraphBrushTinted(BLContext& ctx, const GraphBrushCook& cook, const std::function<bool(BLContext&)>& applyPaint) {
        if (!cook) {
            return;
        }
        paintGraphBrush(ctx, cook);
        const GraphBrushResult::Raster& raster = cook.shapes->raster;
        if (raster.scale <= 0.0) {
            return;
        }
        // Multiply fills the whole box (also where nothing was drawn), so the shapes' own alpha is put back over it afterwards.
        ctx.set_comp_op(BL_COMP_OP_MULTIPLY);
        if (applyPaint(ctx)) {
            ctx.fill_rect(BLRect(raster.box.x0 + cook.origin.x, raster.box.y0 + cook.origin.y, raster.box.x1 - raster.box.x0, raster.box.y1 - raster.box.y0));
        }
        ctx.set_comp_op(BL_COMP_OP_DST_IN);
        paintGraphBrush(ctx, cook);
        ctx.set_comp_op(BL_COMP_OP_SRC_OVER);
    }

    std::vector<BrushPart> graphBrushParts(const GraphBrushCook& cook, const StrokeOptions& options, const SketchPaint& paint) {
        const bool solid = !paint.isNone() && !paint.isGradient() && !paint.isPattern() && !paint.isPoints();
        if (!solid || !options.brush || !graphBrushTinted(options.brush->graphType, options.graphSettings)) {
            return graphBrushParts(cook);
        }
        float tr = 1.0f, tg = 1.0f, tb = 1.0f;
        paint.color().toRgb(tr, tg, tb);
        return graphBrushPartsTinted(cook, tr, tg, tb, paint.color().alpha());
    }

    std::vector<BrushPart> graphBrushParts(const GraphBrushCook& cook) {
        return graphBrushPartsTinted(cook, 1.0f, 1.0f, 1.0f, 1.0f);
    }

    std::vector<BrushPart> graphBrushPartsTinted(const GraphBrushCook& cook, float tr, float tg, float tb, float ta) {
        std::vector<BrushPart> parts;
        if (!cook) {
            return parts;
        }
        const GraphBrushResult& result = *cook.shapes;
        const BLMatrix2D place = BLMatrix2D::make_translation(cook.origin.x, cook.origin.y);
        auto fillOf = [&](std::uint32_t argb) {
            return SketchPaint::solid(SketchColor::rgb(float((argb >> 16) & 255u) / 255.0f * tr, float((argb >> 8) & 255u) / 255.0f * tg,
                float(argb & 255u) / 255.0f * tb, float(argb >> 24) / 255.0f * ta));
        };
        parts.reserve(result.polygons.size() + result.dots.size());
        for (const GraphPolygon& polygon : result.polygons) {
            BrushPart part;
            part.path = polygon.path;
            part.path.transform(place);
            part.closed = true;
            part.fill = fillOf(polygon.color);
            part.stroke = SketchPaint::none();
            parts.push_back(std::move(part));
        }
        for (const GraphDot& dot : result.dots) {
            BrushPart part;
            part.path.add_circle(BLCircle(dot.x + cook.origin.x, dot.y + cook.origin.y, dot.radius));
            part.closed = true;
            part.fill = fillOf(dot.color);
            part.stroke = SketchPaint::none();
            parts.push_back(std::move(part));
        }
        return parts;
    }

}
