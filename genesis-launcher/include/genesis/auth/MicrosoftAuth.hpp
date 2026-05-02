#pragma once

#include "genesis/core/Result.hpp"
#include "genesis/auth/Token.hpp"
#include <string>
#include <functional>

namespace genesis::auth {

struct DeviceCodeInfo {
    std::string device_code;
    std::string user_code;
    std::string verification_uri;
    int         expires_in_seconds;
    int         interval_seconds;
    std::string message;
};

using DeviceCodePromptFn = std::function<void(const DeviceCodeInfo&)>;

class MicrosoftAuthFlow {
public:
    static constexpr const char* CLIENT_ID = "00000000402b5328";
    static constexpr const char* SCOPE     = "service::user.auth.xboxlive.com::MBI_SSL";

    explicit MicrosoftAuthFlow(std::string client_id = CLIENT_ID);

    core::Result<DeviceCodeInfo>    start_device_flow();
    core::Result<MsaToken>          poll_for_token(const DeviceCodeInfo& info);
    core::Result<MsaToken>          refresh_token(const std::string& refresh_token);
    core::Result<XblToken>          authenticate_xbl(const MsaToken& msa);
    core::Result<XstsToken>         authenticate_xsts(const XblToken& xbl);
    core::Result<MinecraftToken>    authenticate_minecraft(const XstsToken& xsts);
    core::Result<MinecraftProfile>  fetch_profile(const MinecraftToken& mc_token);

    core::Result<AuthCredential>    full_auth_flow(DeviceCodePromptFn prompt_fn);
    core::Result<AuthCredential>    refresh_auth(const MsaToken& expired_msa);

private:
    core::Result<std::string> post_json(const std::string& url,
                                        const std::string& body,
                                        const std::vector<std::string>& extra_headers = {});
    core::Result<std::string> get_json(const std::string& url,
                                       const std::string& bearer_token);

    std::string client_id_;
};

}
