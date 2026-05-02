#pragma once

#include "genesis/core/Result.hpp"
#include <string>
#include <optional>
#include <memory>

namespace genesis::auth {

class ISecureStorage {
public:
    virtual ~ISecureStorage() = default;

    virtual core::Result<void>        store(const std::string& key, const std::string& value) = 0;
    virtual core::Result<std::string> load(const std::string& key)                            = 0;
    virtual core::Result<void>        remove(const std::string& key)                          = 0;
    virtual bool                      has(const std::string& key)                             = 0;
};

std::unique_ptr<ISecureStorage> create_secure_storage(const std::string& service_name);

}
