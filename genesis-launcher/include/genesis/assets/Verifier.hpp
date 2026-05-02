#pragma once

#include "genesis/core/Result.hpp"
#include <string>
#include <cstdint>

namespace genesis::assets {

enum class HashAlgorithm { SHA1, SHA256 };

class Verifier {
public:
    static core::Result<std::string> hash_file(const std::string& path,
                                               HashAlgorithm algo = HashAlgorithm::SHA1);

    static core::Result<void> verify_file(const std::string& path,
                                          const std::string& expected_hash,
                                          HashAlgorithm algo = HashAlgorithm::SHA1);

    static core::Result<int64_t> file_size(const std::string& path);
};

}
