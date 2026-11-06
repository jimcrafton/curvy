#pragma once

#include <string>

namespace curvy {

    // The ciphers of the PDF standard security handler (what opens an encrypted PDF whose user password is empty).

    // The 16 bytes of the MD5 digest.
    std::string md5(const std::string& data);

    // RC4 is its own inverse.
    std::string rc4(const std::string& key, const std::string& data);

    // AES-CBC decryption (key of 16, 24 or 32 bytes). `data` is the 16 byte IV followed by the cipher text, as PDF stores it; the PKCS#5
    // padding is removed. False when the key size is wrong or the data is not a whole number of blocks.
    bool aesCbcDecrypt(const std::string& key, const std::string& data, std::string& out);

}
