#include "curvy/paletteimagemodel.h"

#include <algorithm>
#include <filesystem>

namespace curvy {

    bool PaletteImageModel::load(const std::string& path) {
        PaletteExtract::Image image;
        std::string problem;
        if (!PaletteExtract::load(path, image, &problem)) {
            error_ = problem;
            return false;
        }
        path_ = path;
        image_ = std::move(image);
        if (!nameTyped_) {
            name_ = std::filesystem::u8path(path).stem().u8string();
        }
        recompute();
        return true;
    }

    std::string PaletteImageModel::fileName() const {
        return path_.empty() ? std::string() : std::filesystem::u8path(path_).filename().u8string();
    }

    void PaletteImageModel::setCount(int count) {
        count_ = std::clamp(count, kMinCount, kMaxCount);
        recompute();
    }

    void PaletteImageModel::setOrder(PaletteExtractOptions::Order order) {
        order_ = order;
        recompute();
    }

    void PaletteImageModel::setMergeNearDuplicates(bool merge) {
        merge_ = merge;
        recompute();
    }

    void PaletteImageModel::setIgnoreNearWhiteAndBlack(bool skip) {
        skip_ = skip;
        recompute();
    }

    void PaletteImageModel::setName(const std::string& name) {
        name_ = name;
        nameTyped_ = true;
    }

    void PaletteImageModel::recompute() {
        found_.clear();
        if (image_.width <= 0) {
            return;
        }
        PaletteExtractOptions options;
        options.count = count_;
        options.order = order_;
        options.mergeNearDuplicates = merge_;
        options.ignoreNearWhiteAndBlack = skip_;
        found_ = PaletteExtract::fromImage(image_, options);
        error_ = found_.empty() ? "the image has no colours to take" : std::string();
    }

}
