#include "genesis/auth/AuthManager.hpp"
#include "genesis/logging/Logger.hpp"
#include <chrono>
#include <sstream>

namespace genesis::auth {

using Error = core::Error;
template<typename T> using Result = core::Result<T>;

static auto log = logging::get_logger("AuthManager");

AuthManager::AuthManager(std::string service_name)
    : storage_(create_secure_storage(std::move(service_name)))
    , auth_flow_(std::make_unique<MicrosoftAuthFlow>())
{}

Result<AuthCredential> AuthManager::login(DeviceCodePromptFn prompt_fn) {
    log->info("Beginning login flow");
    auto res = auth_flow_->full_auth_flow(std::move(prompt_fn));
    if (res.is_err()) return res;

    auto persist_res = persist_credential(res.value());
    if (persist_res.is_err())
        log->warn("Could not persist credential: " + persist_res.error().full());

    cached_credential_ = res.value();
    return res;
}

Result<AuthCredential> AuthManager::refresh() {
    if (!cached_credential_.has_value()) {
        auto loaded = load_persisted_credential();
        if (loaded.is_err()) return loaded;
        cached_credential_ = loaded.value();
    }

    auto& cred = *cached_credential_;

    if (cred.msa.refresh_token.empty())
        return Result<AuthCredential>::err(Error::make(Error::Code::AuthError, "No refresh token available"));

    log->info("Refreshing MSA token");
    auto msa_res = auth_flow_->refresh_token(cred.msa.refresh_token);
    if (msa_res.is_err()) return Result<AuthCredential>::err(msa_res.error());

    auto full_res = auth_flow_->refresh_auth(msa_res.value());
    if (full_res.is_err()) return Result<AuthCredential>::err(full_res.error());

    persist_credential(full_res.value());
    cached_credential_ = full_res.value();
    return full_res;
}

Result<void> AuthManager::logout() {
    cached_credential_.reset();
    storage_->remove(STORAGE_KEY_MSA_REFRESH);
    storage_->remove(STORAGE_KEY_PROFILE_UUID);
    storage_->remove(STORAGE_KEY_PROFILE_NAME);
    storage_->remove(STORAGE_KEY_MC_TOKEN);
    storage_->remove(STORAGE_KEY_MC_EXPIRY);
    log->info("User logged out");
    return Result<void>::ok();
}

bool AuthManager::is_logged_in() const {
    return cached_credential_.has_value() && cached_credential_->fully_valid();
}

std::optional<AuthCredential> AuthManager::credential() const {
    return cached_credential_;
}

std::optional<std::string> AuthManager::username() const {
    if (cached_credential_) return cached_credential_->profile.username;
    return std::nullopt;
}

Result<AuthCredential> AuthManager::ensure_valid_credential() {
    if (cached_credential_.has_value()) {
        auto& cred = *cached_credential_;
        if (cred.fully_valid() && !cred.msa.expires_soon())
            return Result<AuthCredential>::ok(cred);
        if (!cred.msa.refresh_token.empty())
            return refresh();
    }

    auto loaded = load_persisted_credential();
    if (loaded.is_ok()) {
        cached_credential_ = loaded.value();
        if (cached_credential_->fully_valid() && !cached_credential_->msa.expires_soon())
            return loaded;
        if (!cached_credential_->msa.refresh_token.empty())
            return refresh();
    }

    return Result<AuthCredential>::err(Error::make(Error::Code::AuthError,
                                                    "Not authenticated; please log in"));
}

Result<void> AuthManager::persist_credential(const AuthCredential& cred) {
    auto r1 = storage_->store(STORAGE_KEY_MSA_REFRESH, cred.msa.refresh_token);
    auto r2 = storage_->store(STORAGE_KEY_PROFILE_UUID, cred.profile.uuid);
    auto r3 = storage_->store(STORAGE_KEY_PROFILE_NAME, cred.profile.username);
    auto r4 = storage_->store(STORAGE_KEY_MC_TOKEN, cred.minecraft.access_token);

    int64_t expiry = std::chrono::duration_cast<std::chrono::seconds>(
        cred.minecraft.expires_at.time_since_epoch()).count();
    auto r5 = storage_->store(STORAGE_KEY_MC_EXPIRY, std::to_string(expiry));

    if (r1.is_err() || r2.is_err() || r3.is_err() || r4.is_err() || r5.is_err())
        return Result<void>::err(Error::make(Error::Code::IoError, "Failed to persist some credentials"));

    return Result<void>::ok();
}

Result<AuthCredential> AuthManager::load_persisted_credential() {
    if (!storage_->has(STORAGE_KEY_MSA_REFRESH))
        return Result<AuthCredential>::err(Error::make(Error::Code::AuthError, "No persisted credentials"));

    auto refresh_res = storage_->load(STORAGE_KEY_MSA_REFRESH);
    auto uuid_res    = storage_->load(STORAGE_KEY_PROFILE_UUID);
    auto name_res    = storage_->load(STORAGE_KEY_PROFILE_NAME);
    auto mc_tok_res  = storage_->load(STORAGE_KEY_MC_TOKEN);
    auto expiry_res  = storage_->load(STORAGE_KEY_MC_EXPIRY);

    if (refresh_res.is_err() || uuid_res.is_err() || name_res.is_err())
        return Result<AuthCredential>::err(Error::make(Error::Code::AuthError, "Incomplete stored credentials"));

    AuthCredential cred;
    cred.msa.refresh_token = refresh_res.value();
    cred.profile.uuid      = uuid_res.value();
    cred.profile.username  = name_res.value();
    cred.profile.owns_game = true;

    if (mc_tok_res.is_ok() && expiry_res.is_ok()) {
        cred.minecraft.access_token = mc_tok_res.value();
        int64_t secs = std::stoll(expiry_res.value());
        cred.minecraft.expires_at = std::chrono::system_clock::time_point{std::chrono::seconds{secs}};
    }

    log->info("Loaded persisted credentials for: " + cred.profile.username);
    return Result<AuthCredential>::ok(std::move(cred));
}

}
