#include "curvy/pdfcrypt.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace curvy {

    namespace {

        std::uint32_t rotateLeft(std::uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

        // ---- AES -------------------------------------------------------------------------------------------------------------------------

        std::uint8_t multiply(std::uint8_t a, std::uint8_t b) {
            std::uint8_t result = 0;
            while (b != 0) {
                if (b & 1) { result ^= a; }
                a = std::uint8_t((a << 1) ^ ((a & 0x80) ? 0x1B : 0));
                b = std::uint8_t(b >> 1);
            }
            return result;
        }

        struct AesTables {
            std::uint8_t sbox[256], inverse[256];
            std::uint8_t times9[256], times11[256], times13[256], times14[256];   // for the inverse mix of columns
            AesTables() {
                // The S-box from its definition (multiplicative inverse in GF(2^8), then the affine map), so no table has to be typed in.
                std::uint8_t p = 1, q = 1;
                do {
                    p = std::uint8_t(p ^ (p << 1) ^ ((p & 0x80) ? 0x1B : 0));
                    q = std::uint8_t(q ^ (q << 1));
                    q = std::uint8_t(q ^ (q << 2));
                    q = std::uint8_t(q ^ (q << 4));
                    if (q & 0x80) { q ^= 0x09; }
                    auto rot = [](std::uint8_t v, int n) { return std::uint8_t((v << n) | (v >> (8 - n))); };
                    sbox[p] = std::uint8_t(q ^ rot(q, 1) ^ rot(q, 2) ^ rot(q, 3) ^ rot(q, 4) ^ 0x63);
                } while (p != 1);
                sbox[0] = 0x63;
                for (int i = 0; i < 256; ++i) {
                    inverse[sbox[i]] = std::uint8_t(i);
                    times9[i] = multiply(std::uint8_t(i), 9);
                    times11[i] = multiply(std::uint8_t(i), 11);
                    times13[i] = multiply(std::uint8_t(i), 13);
                    times14[i] = multiply(std::uint8_t(i), 14);
                }
            }
        };

        const AesTables& tables() {
            static const AesTables t;
            return t;
        }

        class AesDecryptor {
        public:
            explicit AesDecryptor(const std::string& key) {
                const std::size_t words = key.size() / 4;
                rounds_ = int(words) + 6;
                const AesTables& t = tables();
                std::vector<std::uint8_t> w(16 * std::size_t(rounds_ + 1));
                for (std::size_t i = 0; i < key.size(); ++i) { w[i] = std::uint8_t(key[i]); }
                std::uint8_t rcon = 1;
                for (std::size_t i = words; i < 4 * std::size_t(rounds_ + 1); ++i) {
                    std::uint8_t temp[4] = {w[4 * (i - 1)], w[4 * (i - 1) + 1], w[4 * (i - 1) + 2], w[4 * (i - 1) + 3]};
                    if (i % words == 0) {
                        const std::uint8_t first = temp[0];
                        temp[0] = std::uint8_t(t.sbox[temp[1]] ^ rcon);
                        temp[1] = t.sbox[temp[2]];
                        temp[2] = t.sbox[temp[3]];
                        temp[3] = t.sbox[first];
                        rcon = multiply(rcon, 2);
                    } else if (words > 6 && i % words == 4) {
                        for (std::uint8_t& b : temp) { b = t.sbox[b]; }
                    }
                    for (int k = 0; k < 4; ++k) { w[4 * i + std::size_t(k)] = std::uint8_t(w[4 * (i - words) + std::size_t(k)] ^ temp[k]); }
                }
                keys_ = std::move(w);
            }

            void decryptBlock(const std::uint8_t* in, std::uint8_t* out) const {
                const AesTables& t = tables();
                std::uint8_t s[16];
                for (int i = 0; i < 16; ++i) { s[i] = std::uint8_t(in[i] ^ keys_[16 * std::size_t(rounds_) + std::size_t(i)]); }
                for (int round = rounds_ - 1; round >= 0; --round) {
                    std::uint8_t shifted[16];   // inverse shift rows: row r moves right by r (the state is column-major)
                    for (int c = 0; c < 4; ++c) {
                        for (int r = 0; r < 4; ++r) { shifted[((c + r) % 4) * 4 + r] = s[c * 4 + r]; }
                    }
                    for (int i = 0; i < 16; ++i) { s[i] = std::uint8_t(t.inverse[shifted[i]] ^ keys_[16 * std::size_t(round) + std::size_t(i)]); }
                    if (round > 0) {
                        for (int c = 0; c < 4; ++c) {
                            const std::uint8_t a0 = s[c * 4], a1 = s[c * 4 + 1], a2 = s[c * 4 + 2], a3 = s[c * 4 + 3];
                            s[c * 4] = std::uint8_t(t.times14[a0] ^ t.times11[a1] ^ t.times13[a2] ^ t.times9[a3]);
                            s[c * 4 + 1] = std::uint8_t(t.times9[a0] ^ t.times14[a1] ^ t.times11[a2] ^ t.times13[a3]);
                            s[c * 4 + 2] = std::uint8_t(t.times13[a0] ^ t.times9[a1] ^ t.times14[a2] ^ t.times11[a3]);
                            s[c * 4 + 3] = std::uint8_t(t.times11[a0] ^ t.times13[a1] ^ t.times9[a2] ^ t.times14[a3]);
                        }
                    }
                }
                for (int i = 0; i < 16; ++i) { out[i] = s[i]; }
            }

        private:
            std::vector<std::uint8_t> keys_;
            int rounds_ = 10;
        };

    }

    std::string md5(const std::string& message) {
        static const int shifts[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
                                       4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
        static const std::vector<std::uint32_t> constants = [] {
            std::vector<std::uint32_t> k(64);
            for (int i = 0; i < 64; ++i) { k[std::size_t(i)] = std::uint32_t(std::floor(std::fabs(std::sin(double(i + 1))) * 4294967296.0)); }
            return k;
        }();
        std::string padded = message;
        padded += char(0x80);
        while (padded.size() % 64 != 56) { padded += char(0); }
        const std::uint64_t bits = std::uint64_t(message.size()) * 8;
        for (int i = 0; i < 8; ++i) { padded += char((bits >> (8 * i)) & 255); }

        std::uint32_t a0 = 0x67452301u, b0 = 0xefcdab89u, c0 = 0x98badcfeu, d0 = 0x10325476u;
        for (std::size_t chunk = 0; chunk < padded.size(); chunk += 64) {
            std::uint32_t m[16];
            for (int i = 0; i < 16; ++i) {
                const unsigned char* p = reinterpret_cast<const unsigned char*>(padded.data()) + chunk + 4 * std::size_t(i);
                m[i] = std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
            }
            std::uint32_t a = a0, b = b0, c = c0, d = d0;
            for (int i = 0; i < 64; ++i) {
                std::uint32_t f;
                int g;
                if (i < 16) { f = (b & c) | (~b & d); g = i; }
                else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
                else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
                else { f = c ^ (b | ~d); g = (7 * i) % 16; }
                f = f + a + constants[std::size_t(i)] + m[g];
                a = d;
                d = c;
                c = b;
                b = b + rotateLeft(f, shifts[i]);
            }
            a0 += a;
            b0 += b;
            c0 += c;
            d0 += d;
        }
        std::string out;
        for (std::uint32_t v : {a0, b0, c0, d0}) {
            for (int i = 0; i < 4; ++i) { out += char((v >> (8 * i)) & 255); }
        }
        return out;
    }

    std::string rc4(const std::string& key, const std::string& data) {
        if (key.empty()) { return data; }
        std::uint8_t s[256];
        for (int i = 0; i < 256; ++i) { s[i] = std::uint8_t(i); }
        for (int i = 0, j = 0; i < 256; ++i) {
            j = (j + s[i] + std::uint8_t(key[std::size_t(i) % key.size()])) & 255;
            std::swap(s[i], s[j]);
        }
        std::string out = data;
        for (std::size_t n = 0, i = 0, j = 0; n < out.size(); ++n) {
            i = (i + 1) & 255;
            j = (j + s[i]) & 255;
            std::swap(s[i], s[j]);
            out[n] = char(std::uint8_t(out[n]) ^ s[(s[i] + s[j]) & 255]);
        }
        return out;
    }

    bool aesCbcDecrypt(const std::string& key, const std::string& data, std::string& out) {
        out.clear();
        if ((key.size() != 16 && key.size() != 24 && key.size() != 32) || data.size() < 32 || data.size() % 16 != 0) { return false; }
        const AesDecryptor aes(key);
        const std::uint8_t* in = reinterpret_cast<const std::uint8_t*>(data.data());
        out.resize(data.size() - 16);
        for (std::size_t block = 16; block < data.size(); block += 16) {
            std::uint8_t plain[16];
            aes.decryptBlock(in + block, plain);
            for (int i = 0; i < 16; ++i) { out[block - 16 + std::size_t(i)] = char(plain[i] ^ in[block - 16 + std::size_t(i)]); }
        }
        const unsigned pad = std::uint8_t(out.back());
        if (pad >= 1 && pad <= 16 && pad <= out.size()) { out.resize(out.size() - pad); }
        return true;
    }

}
