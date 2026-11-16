#pragma once

#include "curvy/paletteextract.h"
#include "curvy/swatchpalettes.h"

#include <string>
#include <vector>

namespace curvy {

    // The choices of "New palette from image" and what they give: the image (decoded once), how many colours to look for, the order, the two filters and the
    // palette's name. Changing a choice finds the colours again, so a dialog can show them as the controls move. Without an image, or when the image has
    // nothing usable, found() is empty and error() says why.
    // @reflect ignore=true
    class PaletteImageModel {
    public:
        static constexpr int kMinCount = 4;
        static constexpr int kMaxCount = 16;

        // Reads the image and finds its colours with the current choices; the name becomes the file's name unless one was typed. False (error() set)
        // when the file is not an image this program can read; the previous image stays then.
        bool load(const std::string& path);

        const std::string& path() const { return path_; }
        std::string fileName() const;
        const std::string& error() const { return error_; }

        int count() const { return count_; }
        void setCount(int count);
        PaletteExtractOptions::Order order() const { return order_; }
        void setOrder(PaletteExtractOptions::Order order);
        bool mergeNearDuplicates() const { return merge_; }
        void setMergeNearDuplicates(bool merge);
        bool ignoreNearWhiteAndBlack() const { return skip_; }
        void setIgnoreNearWhiteAndBlack(bool skip);

        const std::string& name() const { return name_; }
        void setName(const std::string& name);

        const std::vector<Swatch>& found() const { return found_; }
        // An image is loaded and gave colours, and the name is not blank: Create palette can be pressed.
        bool canCreate() const { return !found_.empty() && !name_.empty(); }

    private:
        void recompute();

        std::string path_;
        PaletteExtract::Image image_;
        std::string error_;
        int count_ = 8;
        PaletteExtractOptions::Order order_ = PaletteExtractOptions::Order::MostUsed;
        bool merge_ = true;
        bool skip_ = true;
        std::string name_;
        bool nameTyped_ = false;
        std::vector<Swatch> found_;
    };

}
