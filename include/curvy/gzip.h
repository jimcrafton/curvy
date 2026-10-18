#pragma once

#include <string>

namespace curvy {

    // gzip (RFC 1952) compression of whole byte strings, through the vendored zlib. Used for .svgz files: SVG text is very
    // repetitive, so it shrinks several times over.

    // `data` as a zlib stream (RFC 1950: what a PDF FlateDecode stream holds). gzipDecompress reads it back too.
    std::string zlibCompress(const std::string& data, int level = 6);

    // True when `data` starts with the gzip magic bytes (1F 8B) - how a file is told apart from plain SVG regardless of its name.
    bool isGzip(const std::string& data);

    // `data` as a gzip stream. `level` is zlib's 1 (fastest) .. 9 (smallest); 6 is its usual default.
    std::string gzipCompress(const std::string& data, int level = 6);

    // Inflates a gzip stream into `out`. False (and `out` cleared) when `data` is not valid gzip or is cut short.
    bool gzipDecompress(const std::string& data, std::string& out);

}
