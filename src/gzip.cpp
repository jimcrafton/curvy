#include "curvy/gzip.h"

#include <zlib.h>

#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace curvy {

    namespace {
        // zlib is built with Z_SOLO, which leaves the allocators to the caller.
        void useMalloc(z_stream& stream) {
            std::memset(&stream, 0, sizeof(stream));
            stream.zalloc = [](voidpf, uInt items, uInt size) -> voidpf { return std::calloc(items, size); };
            stream.zfree = [](voidpf, voidpf address) { std::free(address); };
        }

        constexpr int kGzipWindow = 15 + 16;        // deflateInit2: 16 added to the window bits writes a gzip header
        constexpr int kAutoDetectWindow = 15 + 32;  // inflateInit2: 32 added accepts a gzip or a zlib header
    }

    bool isGzip(const std::string& data) {
        return data.size() >= 2 && static_cast<unsigned char>(data[0]) == 0x1F && static_cast<unsigned char>(data[1]) == 0x8B;
    }

    std::string gzipCompress(const std::string& data, int level) {
        z_stream stream;
        useMalloc(stream);
        if (deflateInit2(&stream, level, Z_DEFLATED, kGzipWindow, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
            throw std::runtime_error("gzipCompress: deflateInit2 failed");
        }
        std::string out;
        out.resize(deflateBound(&stream, static_cast<uLong>(data.size())));
        stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
        stream.avail_in = static_cast<uInt>(data.size());
        stream.next_out = reinterpret_cast<Bytef*>(&out[0]);
        stream.avail_out = static_cast<uInt>(out.size());
        int result = deflate(&stream, Z_FINISH);
        std::size_t produced = out.size() - stream.avail_out;
        deflateEnd(&stream);
        if (result != Z_STREAM_END) {
            throw std::runtime_error("gzipCompress: deflate did not finish");
        }
        out.resize(produced);
        return out;
    }

    std::string zlibCompress(const std::string& data, int level) {
        z_stream stream;
        useMalloc(stream);
        if (deflateInit2(&stream, level, Z_DEFLATED, 15, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
            throw std::runtime_error("zlibCompress: deflateInit2 failed");
        }
        std::string out;
        out.resize(deflateBound(&stream, static_cast<uLong>(data.size())));
        stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
        stream.avail_in = static_cast<uInt>(data.size());
        stream.next_out = reinterpret_cast<Bytef*>(&out[0]);
        stream.avail_out = static_cast<uInt>(out.size());
        int result = deflate(&stream, Z_FINISH);
        std::size_t produced = out.size() - stream.avail_out;
        deflateEnd(&stream);
        if (result != Z_STREAM_END) {
            throw std::runtime_error("zlibCompress: deflate did not finish");
        }
        out.resize(produced);
        return out;
    }

    bool gzipDecompress(const std::string& data, std::string& out) {
        out.clear();
        z_stream stream;
        useMalloc(stream);
        if (inflateInit2(&stream, kAutoDetectWindow) != Z_OK) {
            return false;
        }
        stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
        stream.avail_in = static_cast<uInt>(data.size());
        char chunk[64 * 1024];
        int result = Z_OK;
        while (result == Z_OK) {
            stream.next_out = reinterpret_cast<Bytef*>(chunk);
            stream.avail_out = sizeof(chunk);
            result = inflate(&stream, Z_NO_FLUSH);
            out.append(chunk, sizeof(chunk) - stream.avail_out);
        }
        inflateEnd(&stream);
        if (result != Z_STREAM_END) {   // corrupt, or the stream ended early
            out.clear();
            return false;
        }
        return true;
    }

}
