#include "genesis/version/VersionManager.hpp"
#include "genesis/assets/Downloader.hpp"
#include "genesis/platform/PlatformUtils.hpp"
#include "genesis/logging/Logger.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <chrono>

namespace genesis::version {

using json  = nlohmann::json;
using Error = core::Error;
template<typename T> using Result = core::Result<T>;

static auto log = logging::get_logger("VersionManager");

static constexpr int CACHE_TTL_MINUTES = 60;

VersionManager::VersionManager(std::string cache_dir)
    : cache_dir_(std::move(cache_dir)) {}

Result<VersionList> VersionManager::fetch_version_list(bool force_refresh) {
    using namespace std::chrono;
    auto now = system_clock::now();
    bool cache_valid = cached_list_.has_value()
        && duration_cast<minutes>(now - list_fetched_at_).count() < CACHE_TTL_MINUTES;

    if (!force_refresh && cache_valid)
        return Result<VersionList>::ok(*cached_list_);

    if (!force_refresh) {
        auto cached = load_cached_manifest();
        if (cached.is_ok()) {
            cached_list_      = cached.value();
            list_fetched_at_  = now;
            return cached;
        }
    }

    log->info("Fetching version manifest from Mojang");
    assets::Downloader dl;
    auto resp = dl.fetch_string(MANIFEST_URL);
    if (resp.is_err()) return Result<VersionList>::err(resp.error());

    auto parsed = parse_version_list(resp.value());
    if (parsed.is_err()) return Result<VersionList>::err(parsed.error());

    save_manifest(resp.value());
    cached_list_     = parsed.value();
    list_fetched_at_ = now;
    return parsed;
}

Result<VersionMeta> VersionManager::fetch_version_meta(const std::string& version_id) {
    auto cached_path = version_json_path(version_id);
    if (platform::is_file(cached_path)) {
        auto text = platform::read_file(cached_path);
        if (text.is_ok()) {
            auto parsed = parse_version_meta(text.value());
            if (parsed.is_ok()) return parsed;
            log->warn("Cached version meta invalid for " + version_id + ", re-fetching");
        }
    }

    auto list_res = fetch_version_list();
    if (list_res.is_err()) return Result<VersionMeta>::err(list_res.error());

    auto entry = list_res.value().find(version_id);
    if (!entry)
        return Result<VersionMeta>::err(Error::make(Error::Code::VersionNotFound,
                                                     "Version not found: " + version_id));

    log->info("Fetching version meta for " + version_id);
    assets::Downloader dl;
    auto resp = dl.fetch_string(entry->url);
    if (resp.is_err()) return Result<VersionMeta>::err(resp.error());

    auto parsed = parse_version_meta(resp.value());
    if (parsed.is_err()) return Result<VersionMeta>::err(parsed.error());

    auto dir = platform::path_parent(cached_path);
    platform::create_directories(dir);
    platform::write_file(cached_path, resp.value());

    return parsed;
}

Result<void> VersionManager::ensure_version_downloaded(
    const std::string& version_id,
    const std::string& game_dir,
    VersionProgressFn  progress_fn)
{
    if (progress_fn) progress_fn("Fetching version info", 0.0f);
    auto meta_res = fetch_version_meta(version_id);
    if (meta_res.is_err()) return Result<void>::err(meta_res.error());
    if (progress_fn) progress_fn("Version info ready", 1.0f);
    return Result<void>::ok();
}

bool VersionManager::is_version_cached(const std::string& version_id) const {
    return platform::is_file(version_json_path(version_id));
}

std::string VersionManager::version_json_path(const std::string& version_id) const {
    return platform::path_join({cache_dir_, "versions", version_id, version_id + ".json"});
}

Result<VersionList> VersionManager::load_cached_manifest() {
    auto path = platform::path_join(cache_dir_, "version_manifest_v2.json");
    auto text = platform::read_file(path);
    if (text.is_err()) return Result<VersionList>::err(text.error());
    return parse_version_list(text.value());
}

Result<void> VersionManager::save_manifest(const std::string& json_text) {
    auto path = platform::path_join(cache_dir_, "version_manifest_v2.json");
    platform::create_directories(cache_dir_);
    return platform::write_file(path, json_text);
}

Result<std::string> VersionManager::download_to_string(const std::string& url) {
    assets::Downloader dl;
    return dl.fetch_string(url);
}

Result<std::vector<RuntimeProfile>> VersionManager::list_profiles() const {
    return Result<std::vector<RuntimeProfile>>::ok({});
}

Result<RuntimeProfile> VersionManager::get_profile(const std::string& profile_id) const {
    return Result<RuntimeProfile>::err(Error::make(Error::Code::VersionNotFound,
                                                    "Profile not found: " + profile_id));
}

Result<void> VersionManager::save_profile(const RuntimeProfile&) {
    return Result<void>::ok();
}

Result<void> VersionManager::delete_profile(const std::string&) {
    return Result<void>::ok();
}

}
