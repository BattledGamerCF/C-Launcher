#include "genesis/assets/Verifier.hpp"
#include "genesis/platform/PlatformUtils.hpp"
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace genesis::assets {

using Error = core::Error;
template<typename T> using Result = core::Result<T>;

static std::string bytes_to_hex(const unsigned char* data, size_t len) {
    std::ostringstream oss;
    for (size_t i = 0; i < len; ++i)
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)data[i];
    return oss.str();
}

Result<std::string> Verifier::hash_file(const std::string& path, HashAlgorithm algo) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
        return Result<std::string>::err(Error::make(Error::Code::IoError, "Cannot open file for hashing: " + path));

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    const EVP_MD* md = (algo == HashAlgorithm::SHA256) ? EVP_sha256() : EVP_sha1();
    EVP_DigestInit_ex(ctx, md, nullptr);

    char buf[65536];
    while (f.read(buf, sizeof(buf)) || f.gcount() > 0) {
        EVP_DigestUpdate(ctx, buf, static_cast<size_t>(f.gcount()));
    }

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int  digest_len = 0;
    EVP_DigestFinal_ex(ctx, digest, &digest_len);
    EVP_MD_CTX_free(ctx);

    return Result<std::string>::ok(bytes_to_hex(digest, digest_len));
}

Result<void> Verifier::verify_file(const std::string& path,
                                    const std::string& expected,
                                    HashAlgorithm algo)
{
    auto actual_res = hash_file(path, algo);
    if (actual_res.is_err()) return Result<void>::err(actual_res.error());

    if (actual_res.value() != expected)
        return Result<void>::err(Error::make(Error::Code::HashMismatch,
            "Hash mismatch for " + path,
            "expected=" + expected + " actual=" + actual_res.value()));

    return Result<void>::ok();
}

Result<int64_t> Verifier::file_size(const std::string& path) {
    return platform::read_file(path).map([](const std::string& s) {
        return static_cast<int64_t>(s.size());
    });
}

}
