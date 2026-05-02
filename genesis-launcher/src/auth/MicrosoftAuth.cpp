#include "genesis/auth/MicrosoftAuth.hpp"
#include "genesis/logging/Logger.hpp"
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <thread>
#include <chrono>
#include <sstream>
#include <vector>

namespace genesis::auth {

using json  = nlohmann::json;
using Error = core::Error;
template<typename T>
using Result = core::Result<T>;

static auto log = logging::get_logger("MicrosoftAuth");

namespace {

size_t curl_write_cb(void* ptr, size_t size, size_t nmemb, std::string* out) {
    out->append(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

std::chrono::system_clock::time_point now_plus_seconds(int secs) {
    return std::chrono::system_clock::now() + std::chrono::seconds(secs);
}

} // namespace

MicrosoftAuthFlow::MicrosoftAuthFlow(std::string client_id)
    : client_id_(std::move(client_id)) {}

Result<std::string> MicrosoftAuthFlow::post_json(
    const std::string& url,
    const std::string& body,
    const std::vector<std::string>& extra_headers)
{
    CURL* curl = curl_easy_init();
    if (!curl) return Result<std::string>::err(Error::make(Error::Code::NetworkError, "curl_easy_init failed"));

    std::string response;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    for (auto& h : extra_headers)
        headers = curl_slist_append(headers, h.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
        return Result<std::string>::err(Error::make(Error::Code::NetworkError,
                                                     "HTTP POST failed", curl_easy_strerror(res)));
    return Result<std::string>::ok(std::move(response));
}

Result<std::string> MicrosoftAuthFlow::get_json(
    const std::string& url,
    const std::string& bearer_token)
{
    CURL* curl = curl_easy_init();
    if (!curl) return Result<std::string>::err(Error::make(Error::Code::NetworkError, "curl_easy_init failed"));

    std::string response;
    struct curl_slist* headers = nullptr;
    std::string auth_header    = "Authorization: Bearer " + bearer_token;
    headers = curl_slist_append(headers, auth_header.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
        return Result<std::string>::err(Error::make(Error::Code::NetworkError,
                                                     "HTTP GET failed", curl_easy_strerror(res)));
    return Result<std::string>::ok(std::move(response));
}

Result<DeviceCodeInfo> MicrosoftAuthFlow::start_device_flow() {
    CURL* curl = curl_easy_init();
    if (!curl) return Result<DeviceCodeInfo>::err(Error::make(Error::Code::NetworkError, "curl_easy_init failed"));

    std::string response;
    std::string body = "client_id=" + client_id_
                     + "&scope=XboxLive.signin%20offline_access";

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");

    curl_easy_setopt(curl, CURLOPT_URL,
                     "https://login.microsoftonline.com/consumers/oauth2/v2.0/devicecode");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
        return Result<DeviceCodeInfo>::err(Error::make(Error::Code::NetworkError,
                                                       "Device flow request failed", curl_easy_strerror(res)));

    try {
        auto j = json::parse(response);
        if (j.contains("error"))
            return Result<DeviceCodeInfo>::err(Error::make(Error::Code::AuthError,
                "Device code error: " + j.value("error_description", j["error"].get<std::string>())));

        return Result<DeviceCodeInfo>::ok(DeviceCodeInfo{
            j["device_code"].get<std::string>(),
            j["user_code"].get<std::string>(),
            j["verification_uri"].get<std::string>(),
            j["expires_in"].get<int>(),
            j["interval"].get<int>(),
            j.value("message", "")
        });
    } catch (const json::exception& e) {
        return Result<DeviceCodeInfo>::err(Error::make(Error::Code::ParseError, "Device code parse error", e.what()));
    }
}

Result<MsaToken> MicrosoftAuthFlow::poll_for_token(const DeviceCodeInfo& info) {
    using namespace std::chrono_literals;
    int elapsed = 0;
    while (elapsed < info.expires_in_seconds) {
        std::this_thread::sleep_for(std::chrono::seconds(info.interval_seconds));
        elapsed += info.interval_seconds;

        CURL* curl = curl_easy_init();
        if (!curl) continue;
        std::string response;
        std::string body =
            "grant_type=urn:ietf:params:oauth:grant-type:device_code"
            "&client_id=" + client_id_ +
            "&device_code=" + info.device_code;

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");

        curl_easy_setopt(curl, CURLOPT_URL,
                         "https://login.microsoftonline.com/consumers/oauth2/v2.0/token");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) continue;

        try {
            auto j = json::parse(response);
            if (j.contains("error")) {
                std::string err = j.value("error", "");
                if (err == "authorization_pending") continue;
                if (err == "slow_down") {
                    std::this_thread::sleep_for(5s);
                    continue;
                }
                return Result<MsaToken>::err(Error::make(Error::Code::AuthError,
                    "Token poll error: " + j.value("error_description", err)));
            }

            MsaToken token;
            token.access_token  = j["access_token"].get<std::string>();
            token.refresh_token = j.value("refresh_token", "");
            token.id_token      = j.value("id_token", "");
            token.scope         = j.value("scope", "");
            token.expires_at    = now_plus_seconds(j.value("expires_in", 3600));
            return Result<MsaToken>::ok(std::move(token));
        } catch (const json::exception& e) {
            return Result<MsaToken>::err(Error::make(Error::Code::ParseError, "Token parse error", e.what()));
        }
    }
    return Result<MsaToken>::err(Error::make(Error::Code::Timeout, "Device code expired before user authenticated"));
}

Result<MsaToken> MicrosoftAuthFlow::refresh_token(const std::string& refresh_tok) {
    CURL* curl = curl_easy_init();
    if (!curl) return Result<MsaToken>::err(Error::make(Error::Code::NetworkError, "curl_easy_init failed"));

    std::string response;
    std::string body =
        "grant_type=refresh_token"
        "&client_id=" + client_id_ +
        "&refresh_token=" + refresh_tok +
        "&scope=XboxLive.signin%20offline_access";

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");

    curl_easy_setopt(curl, CURLOPT_URL,
                     "https://login.microsoftonline.com/consumers/oauth2/v2.0/token");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
        return Result<MsaToken>::err(Error::make(Error::Code::NetworkError, "Refresh request failed", curl_easy_strerror(res)));

    try {
        auto j = json::parse(response);
        if (j.contains("error"))
            return Result<MsaToken>::err(Error::make(Error::Code::AuthError,
                "Refresh failed: " + j.value("error_description", j["error"].get<std::string>())));

        MsaToken token;
        token.access_token  = j["access_token"].get<std::string>();
        token.refresh_token = j.value("refresh_token", refresh_tok);
        token.id_token      = j.value("id_token", "");
        token.scope         = j.value("scope", "");
        token.expires_at    = now_plus_seconds(j.value("expires_in", 3600));
        return Result<MsaToken>::ok(std::move(token));
    } catch (const json::exception& e) {
        return Result<MsaToken>::err(Error::make(Error::Code::ParseError, "Refresh parse error", e.what()));
    }
}

Result<XblToken> MicrosoftAuthFlow::authenticate_xbl(const MsaToken& msa) {
    json body;
    body["Properties"]["AuthMethod"] = "RPS";
    body["Properties"]["SiteName"]   = "user.auth.xboxlive.com";
    body["Properties"]["RpsTicket"]  = "d=" + msa.access_token;
    body["RelyingParty"]             = "http://auth.xboxlive.com";
    body["TokenType"]                = "JWT";

    auto resp = post_json("https://user.auth.xboxlive.com/user/authenticate",
                          body.dump(),
                          {"Accept: application/json"});
    if (resp.is_err()) return Result<XblToken>::err(resp.error());

    try {
        auto j = json::parse(resp.value());
        if (j.contains("XErr"))
            return Result<XblToken>::err(Error::make(Error::Code::AuthError,
                "XBL auth error " + std::to_string(j["XErr"].get<int64_t>())));

        XblToken token;
        token.token     = j["Token"].get<std::string>();
        token.user_hash = j["DisplayClaims"]["xui"][0]["uhs"].get<std::string>();
        token.expires_at = now_plus_seconds(86400);
        return Result<XblToken>::ok(std::move(token));
    } catch (const json::exception& e) {
        return Result<XblToken>::err(Error::make(Error::Code::ParseError, "XBL parse error", e.what()));
    }
}

Result<XstsToken> MicrosoftAuthFlow::authenticate_xsts(const XblToken& xbl) {
    json body;
    body["Properties"]["SandboxId"]   = "RETAIL";
    body["Properties"]["UserTokens"]   = json::array({xbl.token});
    body["RelyingParty"]               = "rp://api.minecraftservices.com/";
    body["TokenType"]                  = "JWT";

    auto resp = post_json("https://xsts.auth.xboxlive.com/xsts/authorize",
                          body.dump(),
                          {"Accept: application/json"});
    if (resp.is_err()) return Result<XstsToken>::err(resp.error());

    try {
        auto j = json::parse(resp.value());
        if (j.contains("XErr")) {
            int64_t xerr = j["XErr"].get<int64_t>();
            std::string msg = (xerr == 2148916233) ? "No Xbox account linked to this Microsoft account"
                             : (xerr == 2148916235) ? "Xbox Live is not available in your region"
                             : (xerr == 2148916238) ? "This account is a child account; add it to a family"
                             : "XSTS error: " + std::to_string(xerr);
            return Result<XstsToken>::err(Error::make(Error::Code::AuthError, msg));
        }

        XstsToken token;
        token.token     = j["Token"].get<std::string>();
        token.user_hash = j["DisplayClaims"]["xui"][0]["uhs"].get<std::string>();
        token.expires_at = now_plus_seconds(86400);
        return Result<XstsToken>::ok(std::move(token));
    } catch (const json::exception& e) {
        return Result<XstsToken>::err(Error::make(Error::Code::ParseError, "XSTS parse error", e.what()));
    }
}

Result<MinecraftToken> MicrosoftAuthFlow::authenticate_minecraft(const XstsToken& xsts) {
    json body;
    body["identityToken"] = "XBL3.0 x=" + xsts.user_hash + ";" + xsts.token;

    auto resp = post_json(
        "https://api.minecraftservices.com/authentication/login_with_xbox",
        body.dump(),
        {"Accept: application/json"});
    if (resp.is_err()) return Result<MinecraftToken>::err(resp.error());

    try {
        auto j = json::parse(resp.value());
        MinecraftToken token;
        token.access_token = j["access_token"].get<std::string>();
        token.expires_at   = now_plus_seconds(j.value("expires_in", 86400));
        return Result<MinecraftToken>::ok(std::move(token));
    } catch (const json::exception& e) {
        return Result<MinecraftToken>::err(Error::make(Error::Code::ParseError, "MC token parse error", e.what()));
    }
}

Result<MinecraftProfile> MicrosoftAuthFlow::fetch_profile(const MinecraftToken& mc_token) {
    auto resp = get_json(
        "https://api.minecraftservices.com/minecraft/profile",
        mc_token.access_token);
    if (resp.is_err()) return Result<MinecraftProfile>::err(resp.error());

    try {
        auto j = json::parse(resp.value());
        if (j.contains("error"))
            return Result<MinecraftProfile>::err(Error::make(Error::Code::AuthError,
                "Profile fetch error: " + j.value("errorMessage", j["error"].get<std::string>())));

        MinecraftProfile profile;
        profile.uuid      = j["id"].get<std::string>();
        profile.username  = j["name"].get<std::string>();
        profile.owns_game = true;
        return Result<MinecraftProfile>::ok(std::move(profile));
    } catch (const json::exception& e) {
        return Result<MinecraftProfile>::err(Error::make(Error::Code::ParseError, "Profile parse error", e.what()));
    }
}

Result<AuthCredential> MicrosoftAuthFlow::full_auth_flow(DeviceCodePromptFn prompt_fn) {
    log->info("Starting Microsoft device code authentication flow");

    auto code_res = start_device_flow();
    if (code_res.is_err()) return Result<AuthCredential>::err(code_res.error());
    auto& code_info = code_res.value();

    if (prompt_fn) prompt_fn(code_info);

    auto msa_res = poll_for_token(code_info);
    if (msa_res.is_err()) return Result<AuthCredential>::err(msa_res.error());

    return refresh_auth(msa_res.value());
}

Result<AuthCredential> MicrosoftAuthFlow::refresh_auth(const MsaToken& msa) {
    auto xbl_res = authenticate_xbl(msa);
    if (xbl_res.is_err()) return Result<AuthCredential>::err(xbl_res.error());

    auto xsts_res = authenticate_xsts(xbl_res.value());
    if (xsts_res.is_err()) return Result<AuthCredential>::err(xsts_res.error());

    auto mc_res = authenticate_minecraft(xsts_res.value());
    if (mc_res.is_err()) return Result<AuthCredential>::err(mc_res.error());

    auto profile_res = fetch_profile(mc_res.value());
    if (profile_res.is_err()) return Result<AuthCredential>::err(profile_res.error());

    AuthCredential cred;
    cred.msa       = msa;
    cred.minecraft = mc_res.value();
    cred.profile   = profile_res.value();

    log->info("Authentication complete for: " + cred.profile.username);
    return Result<AuthCredential>::ok(std::move(cred));
}

}
