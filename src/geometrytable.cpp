#include "curvy/geometrytable.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <stdexcept>

namespace curvy {

    std::string GeometryTableModel::number(float value) {
        char text[32];
        std::snprintf(text, sizeof(text), "%.3f", double(value));
        return text;
    }

    GeometryTableModel::GeometryTableModel(jitnodes::GeometryHandle geometry, Kind kind, const std::string& filter) : geometry_(std::move(geometry)), kind_(kind) {
        if (geometry_ == nullptr) {
            return;
        }
        if (kind_ == Kind::Points) {
            rows_ = geometry_->pointCount();
            columns_ = {{"x", nullptr, 0}, {"y", nullptr, 1}};
            addAttributes(jitnodes::AttribClass::point);
            keepMatching(filter);
        } else if (kind_ == Kind::Primitives) {
            rows_ = geometry_->primitiveCount();
            columns_ = {{"start", nullptr, 0}, {"count", nullptr, 1}, {"closed", nullptr, 2}, {"kind", nullptr, 3}};
            addAttributes(jitnodes::AttribClass::primitive);
            keepMatching(filter);
        } else {
            details_.assign(geometry_->details().begin(), geometry_->details().end());
            rows_ = details_.size();
            columns_ = {{"name", nullptr, 0}, {"value", nullptr, 1}};
        }
    }

    void GeometryTableModel::addAttributes(jitnodes::AttribClass cls) {
        for (const auto& [name, array] : geometry_->attribs(cls)) {
            if (cls == jitnodes::AttribClass::point && (name == "x" || name == "y")) {
                continue;
            }
            columns_.push_back({name, &array, -1});
        }
    }

    void GeometryTableModel::keepMatching(const std::string& filter) {
        if (filter.empty()) {
            return;
        }
        const auto lowered = [](std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            return text;
        };
        const std::string needle = lowered(filter);
        columns_.erase(std::remove_if(columns_.begin(), columns_.end(),
                                      [&](const Column& column) { return lowered(column.title).find(needle) == std::string::npos; }),
                       columns_.end());
    }

    std::string GeometryTableModel::cellText(std::size_t row, std::size_t column) const {
        if (geometry_ == nullptr || row >= rows_ || column >= columns_.size()) {
            return "";
        }
        const Column& col = columns_[column];
        if (col.attribute != nullptr) {
            switch (col.attribute->type()) {
            case jitnodes::AttribType::float32:
                return number(col.attribute->floats()[row]);
            case jitnodes::AttribType::int32:
                return std::to_string(col.attribute->ints()[row]);
            case jitnodes::AttribType::string:
                return col.attribute->stringAt(row);
            }
            return "";
        }
        if (kind_ == Kind::Points) {
            return number(col.structural == 0 ? geometry_->x()[row] : geometry_->y()[row]);
        }
        if (kind_ == Kind::Primitives) {
            const jitnodes::Primitive& prim = geometry_->primitives()[row];
            switch (col.structural) {
            case 0: return std::to_string(prim.start);
            case 1: return std::to_string(prim.count);
            case 2: return prim.closed ? "closed" : "open";
            default: return prim.kind == jitnodes::PrimKind::cubic ? "cubic" : "polyline";
            }
        }
        return col.structural == 0 ? details_[row].first : number(details_[row].second);
    }

    std::any GeometryTableModel::value(const std::any& key) {
        const auto* cell = std::any_cast<std::pair<std::size_t, std::size_t>>(&key);
        if (cell == nullptr || cell->first >= rows_ || cell->second >= columns_.size()) {
            return std::any();
        }
        return cellText(cell->first, cell->second);
    }

    void GeometryTableModel::setValue(const std::any&, const std::any&) {
        throw std::runtime_error("the data table is read-only");
    }

    std::string GeometryTableModel::asText(std::size_t firstRow, std::size_t lastRow, std::size_t firstColumn, std::size_t lastColumn) const {
        std::string text;
        if (columns_.empty() || rows_ == 0) {
            return text;
        }
        lastColumn = (std::min)(lastColumn, columns_.size() - 1);
        lastRow = (std::min)(lastRow, rows_ - 1);
        for (std::size_t c = firstColumn; c <= lastColumn; ++c) {
            text += columns_[c].title + (c == lastColumn ? "\n" : "\t");
        }
        for (std::size_t r = firstRow; r <= lastRow; ++r) {
            for (std::size_t c = firstColumn; c <= lastColumn; ++c) {
                text += cellText(r, c) + (c == lastColumn ? "\n" : "\t");
            }
        }
        return text;
    }

}
