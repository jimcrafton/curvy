#include "curvy/imagedata.h"

#include <cctype>
#include <cmath>
#include <chrono>
#include <cstdio>

#include <algorithm>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#endif
#include <fstream>
#include <sstream>

namespace curvy {

    std::string imageMimeType(const std::string& b) {
        auto at = [&b](std::size_t i) { return i < b.size() ? static_cast<unsigned char>(b[i]) : 0u; };
        if (at(0) == 0x89 && at(1) == 'P' && at(2) == 'N' && at(3) == 'G') { return "image/png"; }
        if (at(0) == 0xFF && at(1) == 0xD8) { return "image/jpeg"; }
        if (at(0) == 'B' && at(1) == 'M') { return "image/bmp"; }
        if (at(0) == 'q' && at(1) == 'o' && at(2) == 'i' && at(3) == 'f') { return "image/qoi"; }
        return std::string();
    }

#ifdef _WIN32
    namespace {
        // Decodes a picture with Windows Imaging Component: PNG, JPEG, BMP, GIF, TIFF ... and the turn a camera records in a photo (EXIF
        // orientation) is applied, so a phone photo comes out the way up it was taken. Blend2D's own JPEG reader is not used: it stops with a
        // failed assertion on some photos.
        bool decodeWithWic(const std::string& bytes, BLImage& out) {
            using Microsoft::WRL::ComPtr;
            const HRESULT init = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);   // already initialised (even otherwise) is fine
            const bool uninit = SUCCEEDED(init);
            bool ok = false;
            {
                ComPtr<IWICImagingFactory> factory;
                ComPtr<IWICStream> stream;
                ComPtr<IWICBitmapDecoder> decoder;
                ComPtr<IWICBitmapFrameDecode> frame;
                ComPtr<IWICBitmapSource> source;
                if (SUCCEEDED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
                    SUCCEEDED(factory->CreateStream(&stream)) &&
                    SUCCEEDED(stream->InitializeFromMemory(reinterpret_cast<WICInProcPointer>(const_cast<char*>(bytes.data())), static_cast<DWORD>(bytes.size()))) &&
                    SUCCEEDED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) &&
                    SUCCEEDED(decoder->GetFrame(0, &frame))) {
                    source = frame;
                    // EXIF orientation 1..8 -> a rotation and / or a flip.
                    WICBitmapTransformOptions turn = WICBitmapTransformRotate0;
                    ComPtr<IWICMetadataQueryReader> reader;
                    if (SUCCEEDED(frame->GetMetadataQueryReader(&reader))) {
                        PROPVARIANT value;
                        ::PropVariantInit(&value);
                        if (SUCCEEDED(reader->GetMetadataByName(L"/app1/ifd/{ushort=274}", &value)) && value.vt == VT_UI2) {
                            switch (value.uiVal) {
                                case 2: turn = WICBitmapTransformFlipHorizontal; break;
                                case 3: turn = WICBitmapTransformRotate180; break;
                                case 4: turn = WICBitmapTransformFlipVertical; break;
                                case 5: turn = WICBitmapTransformOptions(WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal); break;
                                case 6: turn = WICBitmapTransformRotate90; break;
                                case 7: turn = WICBitmapTransformOptions(WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal); break;
                                case 8: turn = WICBitmapTransformRotate270; break;
                                default: break;
                            }
                        }
                        ::PropVariantClear(&value);
                    }
                    // Decoded into memory first: a rotation applied straight to a JPEG that is still being decoded makes the codec decode it
                    // over and over (minutes for a 12-megapixel photo).
                    ComPtr<IWICFormatConverter> converter;
                    ComPtr<IWICBitmap> bitmap;
                    if (SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
                        SUCCEEDED(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)) &&
                        SUCCEEDED(factory->CreateBitmapFromSource(converter.Get(), WICBitmapCacheOnLoad, &bitmap))) {
                        source = bitmap;
                        ComPtr<IWICBitmapFlipRotator> rotator;
                        if (turn != WICBitmapTransformRotate0 && SUCCEEDED(factory->CreateBitmapFlipRotator(&rotator)) &&
                            SUCCEEDED(rotator->Initialize(source.Get(), turn))) {
                            source = rotator;
                        }
                        UINT width = 0, height = 0;
                        if (SUCCEEDED(source->GetSize(&width, &height)) && width > 0 && height > 0 && width < 32768 && height < 32768 &&
                            out.create(int(width), int(height), BL_FORMAT_PRGB32) == BL_SUCCESS) {
                        BLImageData pixels;
                        if (out.get_data(&pixels) == BL_SUCCESS &&
                            SUCCEEDED(source->CopyPixels(nullptr, static_cast<UINT>(pixels.stride), static_cast<UINT>(pixels.stride) * height, static_cast<BYTE*>(pixels.pixel_data)))) {
                            ok = true;
                        }
                        }
                    }
                }
            }
            if (uninit) {
                ::CoUninitialize();
            }
            return ok;
        }
    }
#endif

    static std::shared_ptr<const ImageData> decodeImageUnguarded(const std::string& bytes, const std::string& name);
    static std::shared_ptr<const ImageData> loadImageFileUnguarded(const std::string& path);

    // Whatever goes wrong while reading a picture (an exception from the decoder, running out of memory) is "not a picture": null.
    std::shared_ptr<const ImageData> decodeImage(const std::string& bytes, const std::string& name) {
        try {
            return decodeImageUnguarded(bytes, name);
        } catch (...) {
            return nullptr;
        }
    }

    static std::shared_ptr<const ImageData> decodeImageUnguarded(const std::string& bytes, const std::string& name) {
        if (bytes.empty()) {
            return nullptr;
        }
        auto data = std::make_shared<ImageData>();
        bool decoded = false;
#ifdef _WIN32
        decoded = decodeWithWic(bytes, data->image);
#endif
        if (!decoded) {
            if (imageMimeType(bytes) == "image/jpeg") {
                return nullptr;   // see decodeWithWic: not Blend2D's JPEG reader
            }
            if (data->image.read_from_data(bytes.data(), bytes.size()) != BL_SUCCESS || data->image.is_empty()) {
                return nullptr;
            }
            if (data->image.convert(BL_FORMAT_PRGB32) != BL_SUCCESS) {
                return nullptr;
            }
        }
        data->width = data->image.width();
        data->height = data->image.height();
        if (data->width <= 0 || data->height <= 0) {
            return nullptr;
        }
        data->name = name;
        data->bytes = bytes;
        data->mime = imageMimeType(bytes);
        if (data->mime.empty()) {
            data->mime = "image/png";   // it decoded, so it is some picture; the name is only a label
        }
        return data;
    }

    std::shared_ptr<const ImageData> loadImageFile(const std::string& path) {
        try {
            return loadImageFileUnguarded(path);
        } catch (...) {
            return nullptr;
        }
    }

    static std::shared_ptr<const ImageData> loadImageFileUnguarded(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return nullptr;
        }
        // One read of the whole file: copying through a stream buffer is very slow for a photo in a debug build.
        in.seekg(0, std::ios::end);
        const std::streamoff size = in.tellg();
        if (size <= 0) {
            return nullptr;
        }
        std::string bytes(static_cast<std::size_t>(size), ' ');
        in.seekg(0, std::ios::beg);
        in.read(bytes.data(), size);
        if (!in) {
            return nullptr;
        }
        std::size_t slash = path.find_last_of("/\\");
        return decodeImage(bytes, slash == std::string::npos ? path : path.substr(slash + 1));
    }

    const BLImage& ImageData::levelFor(double scale, double& extra) const {
        extra = 1.0;
        if (!(scale > 0.0) || scale >= 0.75 || width < 64 || height < 64) {
            return image;
        }
        // The size of the picture shown is under three quarters of its own: draw from a copy halved as often as keeps it at least that large.
        int level = std::min(8, int(std::floor(std::log2(0.75 / scale)) + 1.0));
        while (level > 0 && (width >> level) < 16) {
            --level;
        }
        for (int k = int(mips.size()) + 1; k <= level; ++k) {
            const BLImage& from = k == 1 ? image : mips[std::size_t(k) - 2];
            BLImage smaller;
            const BLSizeI size(std::max(1, from.width() / 2), std::max(1, from.height() / 2));
            if (BLImage::scale(smaller, from, size, BL_IMAGE_SCALE_FILTER_BILINEAR) != BL_SUCCESS) {
                level = k - 1;
                break;
            }
            mips.push_back(std::move(smaller));
        }
        if (level <= 0 || std::size_t(level) > mips.size()) {
            return image;
        }
        extra = double(width) / double(mips[std::size_t(level) - 1].width());
        return mips[std::size_t(level) - 1];
    }

    std::string base64Encode(const std::string& bytes) {
        static const char* kDigits = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve((bytes.size() + 2) / 3 * 4);
        for (std::size_t i = 0; i < bytes.size(); i += 3) {
            unsigned n = static_cast<unsigned char>(bytes[i]) << 16;
            if (i + 1 < bytes.size()) { n |= static_cast<unsigned char>(bytes[i + 1]) << 8; }
            if (i + 2 < bytes.size()) { n |= static_cast<unsigned char>(bytes[i + 2]); }
            out += kDigits[(n >> 18) & 63];
            out += kDigits[(n >> 12) & 63];
            out += i + 1 < bytes.size() ? kDigits[(n >> 6) & 63] : '=';
            out += i + 2 < bytes.size() ? kDigits[n & 63] : '=';
        }
        return out;
    }

    bool base64Decode(const std::string& text, std::string& out) {
        out.clear();
        unsigned buffer = 0;
        int bits = 0;
        for (char ch : text) {
            int value;
            if (ch >= 'A' && ch <= 'Z') { value = ch - 'A'; }
            else if (ch >= 'a' && ch <= 'z') { value = ch - 'a' + 26; }
            else if (ch >= '0' && ch <= '9') { value = ch - '0' + 52; }
            else if (ch == '+' || ch == '-') { value = 62; }
            else if (ch == '/' || ch == '_') { value = 63; }
            else if (ch == '=') { break; }
            else if (std::isspace(static_cast<unsigned char>(ch))) { continue; }
            else { return false; }
            buffer = (buffer << 6) | unsigned(value);
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                out += char((buffer >> bits) & 0xFF);
            }
        }
        return true;
    }

    std::string imageDataUri(const ImageData& image) {
        return "data:" + image.mime + ";base64," + base64Encode(image.bytes);
    }

    bool bytesFromDataUri(const std::string& uri, std::string& bytes) {
        if (uri.compare(0, 5, "data:") != 0) {
            return false;
        }
        const std::size_t comma = uri.find(',');
        if (comma == std::string::npos) {
            return false;
        }
        const std::string header = uri.substr(5, comma - 5), payload = uri.substr(comma + 1);
        if (header.find(";base64") != std::string::npos) {
            return base64Decode(payload, bytes);
        }
        bytes.clear();   // percent-escaped
        for (std::size_t i = 0; i < payload.size(); ++i) {
            if (payload[i] == '%' && i + 2 < payload.size() && std::isxdigit(static_cast<unsigned char>(payload[i + 1])) && std::isxdigit(static_cast<unsigned char>(payload[i + 2]))) {
                bytes += char(std::stoi(payload.substr(i + 1, 2), nullptr, 16));
                i += 2;
            } else {
                bytes += payload[i];
            }
        }
        return true;
    }

}
