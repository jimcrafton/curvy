#include "curvy/graphtree.h"

#include "curvy/theme.h"

#include <newui/color.h>
#include <newui/fontmanager.h>

#include <algorithm>
#include <cctype>
#include <set>
#include <unordered_set>

namespace curvy {

    using namespace newui;

    namespace {
        BLRgba32 rgb(std::uint32_t color, std::uint32_t alpha = 0xFF) { return BLRgba32((color & 0x00FFFFFFu) | (alpha << 24)); }

        std::string lower(std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            return text;
        }

        constexpr float kChipWidth = 26.0f, kChipHeight = 14.0f, kRightMargin = 8.0f;
    }

    void GraphTreeModel::setRows(std::vector<GraphRow> rows) {
        rows_ = std::move(rows);
        top_.clear();
        children_.assign(rows_.size(), {});
        parent_.assign(rows_.size(), rows_.size());
        position_.assign(rows_.size(), 0);
        std::vector<std::size_t> open;   // the row at each depth above the current one
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            std::size_t depth = rows_[i].depth;
            if (depth > open.size()) {
                depth = open.size();   // a row cannot be deeper than one below its parent
                rows_[i].depth = depth;
            }
            open.resize(depth);
            if (depth == 0) {
                position_[i] = top_.size();
                top_.push_back(i);
            } else {
                parent_[i] = open[depth - 1];
                position_[i] = children_[open[depth - 1]].size();
                children_[open[depth - 1]].push_back(i);
            }
            open.push_back(i);
        }
        onChanged(*this);
    }

    std::size_t GraphTreeModel::indexOf(const std::vector<std::size_t>& path, bool& valid) const {
        valid = true;
        std::size_t current = rows_.size();
        for (std::size_t step : path) {
            const std::vector<std::size_t>& list = current == rows_.size() ? top_ : children_[current];
            if (step >= list.size()) {
                valid = false;
                return rows_.size();
            }
            current = list[step];
        }
        return current;
    }

    const GraphRow* GraphTreeModel::rowAt(const std::vector<std::size_t>& path) const {
        bool valid = false;
        const std::size_t index = indexOf(path, valid);
        return valid && index < rows_.size() ? &rows_[index] : nullptr;
    }

    std::vector<std::size_t> GraphTreeModel::pathOf(std::size_t index) const {
        std::vector<std::size_t> path;
        while (index < rows_.size()) {
            path.insert(path.begin(), position_[index]);
            index = parent_[index];
        }
        return path;
    }

    std::size_t GraphTreeModel::childCount(const std::vector<std::size_t>& path) const {
        bool valid = false;
        const std::size_t index = indexOf(path, valid);
        if (!valid) {
            return 0;
        }
        return index == rows_.size() ? top_.size() : children_[index].size();
    }

    std::any GraphTreeModel::value(const std::any& key) {
        if (const auto* path = std::any_cast<std::vector<std::size_t>>(&key)) {
            if (const GraphRow* row = rowAt(*path)) {
                return row->text;
            }
        }
        return std::any();
    }

    void GraphTreeItem::paint(BLContext& ctx, const Rect& rect, const std::vector<std::size_t>& path, TreeController& controller) {
        TreeItem::paint(ctx, rect, path, controller);
        auto* graph = dynamic_cast<GraphTreeController*>(&controller);
        const GraphRow* row = graph != nullptr && graph->graphModel() != nullptr ? graph->graphModel()->rowAt(path) : nullptr;
        if (row == nullptr) {
            return;
        }
        const Rect client = clientBounds();
        const double right = client.left() + client.size().width - kRightMargin;
        const double centerY = client.top() + client.size().height * 0.5;
        if (row->chip) {
            const double x = right - kChipWidth, y = centerY - kChipHeight * 0.5;
            ctx.save();
            // A checkerboard under the colour, so a partly transparent fill reads as such.
            ctx.clip_to_rect(BLRect(x, y, kChipWidth, kChipHeight));
            ctx.fill_rect(BLRect(x, y, kChipWidth, kChipHeight), rgb(0xBBBBBB));
            for (int i = 0; i < 4; ++i) {
                for (int j = 0; j < 2; ++j) {
                    if ((i + j) % 2 == 0) {
                        ctx.fill_rect(BLRect(x + i * 7.0, y + j * 7.0, 7.0, 7.0), rgb(0x777777));
                    }
                }
            }
            ctx.fill_rect(BLRect(x, y, kChipWidth, kChipHeight), rgb(row->chipColor, std::uint32_t(std::lround(std::clamp(row->chipAlpha, 0.0, 1.0) * 255.0))));
            ctx.restore();
            ctx.save();
            ctx.set_stroke_style(rgb(Theme::current().border));
            ctx.stroke_rect(BLRect(x + 0.5, y + 0.5, kChipWidth - 1.0, kChipHeight - 1.0));
            ctx.restore();
        } else if (!row->summary.empty()) {
            Font font = FontManager::getSystemFont(SystemUIFont::Message);
            BLFont* blFont = font.blFont();
            if (blFont == nullptr || !blFont->is_valid()) {
                return;
            }
            BLGlyphBuffer glyphs;
            glyphs.set_utf8_text(row->summary.c_str(), row->summary.size());
            blFont->shape(glyphs);
            BLTextMetrics metrics;
            blFont->get_text_metrics(glyphs, metrics);
            const double width = metrics.advance.x;
            const BLFontMetrics& fm = blFont->metrics();
            ctx.save();
            ctx.set_fill_style(rgb(Theme::current().textMuted));
            ctx.fill_utf8_text(BLPoint(right - width, centerY + (fm.ascent - fm.descent) * 0.5), *blFont, row->summary.c_str(), row->summary.size());
            ctx.restore();
        }
    }

    GraphTreeController::GraphTreeController() {
        setDefaultItemHeight(22.0f);
    }

    TreeItem* GraphTreeController::createItem(const std::vector<std::size_t>&) {
        return new GraphTreeItem();
    }

    std::optional<std::string> GraphTreeController::iconFor(const std::vector<std::size_t>& path) const {
        const GraphTreeModel* model = graphModel();
        const GraphRow* row = model != nullptr ? model->rowAt(path) : nullptr;
        if (row == nullptr || row->icon.empty()) {
            return std::nullopt;
        }
        return row->icon;
    }

    std::vector<GraphRow> flowRows(const FlowSource& source, const std::function<void(GraphRow&)>& describe) {
        std::vector<GraphRow> rows;
        std::unordered_map<jitnodes::NodeId, std::size_t> at;
        for (std::size_t i = 0; i < source.nodes.size(); ++i) {
            at[source.nodes[i]] = i;
        }
        std::unordered_set<jitnodes::NodeId> used;
        for (const std::vector<jitnodes::NodeId>& list : source.inputs) {
            used.insert(list.begin(), list.end());
        }
        std::unordered_set<jitnodes::NodeId> shown;
        std::function<void(jitnodes::NodeId, std::size_t, const std::string&)> add = [&](jitnodes::NodeId id, std::size_t depth, const std::string& via) {
            GraphRow row;
            row.depth = depth;
            row.node = id;
            row.key = via + "/" + std::to_string(id);
            const bool again = shown.count(id) != 0;
            row.kind = again ? GraphRow::Kind::Link : GraphRow::Kind::Node;
            describe(row);
            if (again) {
                row.text = "-> " + row.text;
                rows.push_back(row);
                return;
            }
            shown.insert(id);
            rows.push_back(row);
            const auto found = at.find(id);
            if (found != at.end()) {
                for (jitnodes::NodeId input : source.inputs[found->second]) {
                    if (at.count(input) != 0) {
                        add(input, depth + 1, row.key);
                    }
                }
            }
        };
        for (jitnodes::NodeId id : source.nodes) {
            if (used.count(id) == 0) {
                add(id, 0, "flow");
            }
        }
        return rows;
    }

    std::vector<GraphRow> filterRows(const std::vector<GraphRow>& rows, const std::string& filter) {
        if (filter.empty()) {
            return rows;
        }
        const std::string needle = lower(filter);
        std::vector<bool> keep(rows.size(), false), match(rows.size(), false);
        std::vector<std::size_t> stack;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            stack.resize(rows[i].depth);
            stack.push_back(i);
            match[i] = lower(rows[i].text).find(needle) != std::string::npos || lower(rows[i].summary).find(needle) != std::string::npos;
            if (match[i]) {
                for (std::size_t k : stack) {
                    keep[k] = true;
                }
            }
        }
        std::vector<GraphRow> out;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (keep[i]) {
                out.push_back(rows[i]);
                out.back().dim = !match[i];
            }
        }
        return out;
    }

}
