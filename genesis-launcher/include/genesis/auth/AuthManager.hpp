#pragma once

#include "genesis/core/Result.hpp"
#include "genesis/auth/Token.hpp"
#include "genesis/auth/MicrosoftAuth.hpp"
#include "genesis/auth/SecureStorage.hpp"
#include <memory>
#include <optional>
#include <functional>

namespace genesis::auth {

class AuthManager {
public:
    explicit AuthManager(std::string service_name = "Genesis");

    core::Result<AuthCredential> login(DeviceCodePromptFn prompt_fn);
    core::Result<AuthCredential> refresh();
    core::Result<void>           logout();

    [[nodiscard]] bool                             is_logged_in()    const;
    [[nodiscard]] std::optional<AuthCredential>    credential()      const;
    [[nodiscard]] std::optional<std::string>       username()        const;

    core::Result<AuthCredential> ensure_valid_credential();

private:
    core::Result<void>           persist_credential(const AuthCredential& cred);
    core::Result<AuthCredential> load_persisted_credential();

    static constexpr const char* STORAGE_KEY_MSA_REFRESH = "msa_refresh_token";
    static constexpr const char* STORAGE_KEY_PROFILE_UUID = "profile_uuid";
    static constexpr const char* STORAGE_KEY_PROFILE_NAME = "profile_name";
    static constexpr const char* STORAGE_KEY_MC_TOKEN     = "mc_access_token";
    static constexpr const char* STORAGE_KEY_MC_EXPIRY    = "mc_token_expiry";

    std::unique_ptr<ISecureStorage>  storage_;
    std::unique_ptr<MicrosoftAuthFlow> auth_flow_;
    std::optional<AuthCredential>    cached_credential_;
};

}
