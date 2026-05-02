#include "genesis/update/Updater.hpp"
#include "genesis/assets/Downloader.hpp"
#include "genesis/assets/Verifier.hpp"
#include "genesis/platform/PlatformUtils.hpp"
#include "genesis/logging/Logger.hpp"
#include <nlohmann/json.hpp>
#include <sstream>
#include <vector>

namespace genesis::update {

using json  = nlohmann::json;
using Error = core::Error;
template<typename T> using Result = core::Result<T>;

static auto log = logging::get_logger("Updater");

Updater::Updater(std::string channel) : channel_(std::move(channel)) {}

static int parse_version_int(const std::string& ver) {
    int major = 0, minor = 0, patch = 0;
    sscanf(ver.c_str(), "%d.%d.%d", &major, &minor, &patch);
    return major * 1000000 + minor * 1000 + patch;
}

bool Updater::is_newer(const std::string& candidate) const {
    return parse_version_int(candidate) > parse_version_int(GENESIS_VERSION_STRING);
}

Result<std::optional<ReleaseInfo>> Updater::check_for_update() {
    log->info("Checking for updates on channel: " + channel_);
    assets::Downloader dl;
    auto resp = dl.fetch_string(UPDATE_MANIFEST_URL);
    if (resp.is_err()) {
        log->warn("Update check failed: " + resp.error().full());
        return Result<std::optional<ReleaseInfo>>::ok(std::nullopt);
    }

    try {
        auto j = json::parse(resp.value());
        auto channel_j = j.find(channel_);
        if (channel_j == j.end())
            return Result<std::optional<ReleaseInfo>>::ok(std::nullopt);

        std::string ver = channel_j->value("version", "");
        if (ver.empty() || !is_newer(ver))
            return Result<std::optional<ReleaseInfo>>::ok(std::nullopt);

        ReleaseInfo info;
        info.version         = ver;
        info.download_url    = channel_j->value("download_url", "");
        info.checksum_sha256 = channel_j->value("sha256", "");
        info.changelog       = channel_j->value("changelog", "");
        info.channel         = channel_;
        info.is_prerelease   = (channel_ != "stable");

        log->info("Update available: " + info.version);
        return Result<std::optional<ReleaseInfo>>::ok(std::move(info));
    } catch (const json::exception& e) {
        return Result<std::optional<ReleaseInfo>>::err(
            Error::make(Error::Code::ParseError, "Update manifest parse error", e.what()));
    }
}

Result<void> Updater::download_update(const ReleaseInfo& release,
                                       const std::string& temp_path,
                                       UpdateProgressFn   pfn)
{
    log->info("Downloading update " + release.version);

    assets::DownloadTask task;
    task.name               = "Genesis " + release.version;
    task.url                = release.download_url;
    task.dest_path          = temp_path;
    task.max_retries        = 3;

    assets::Downloader dl;
    auto res = dl.download_one(task, [&pfn](const std::string& name, int64_t d, int64_t t) {
        if (pfn) pfn(t > 0 ? float(d) / float(t) : 0.0f, "Downloading " + name);
    });
    if (res.is_err()) return res;

    if (!release.checksum_sha256.empty()) {
        if (pfn) pfn(0.95f, "Verifying download");
        auto verify = verify_downloaded(temp_path, release.checksum_sha256);
        if (verify.is_err()) return verify;
    }

    if (pfn) pfn(1.0f, "Download complete");
    return Result<void>::ok();
}

Result<void> Updater::verify_downloaded(const std::string& path,
                                          const std::string& expected_sha256)
{
    auto res = assets::Verifier::verify_file(path, expected_sha256,
                                              assets::HashAlgorithm::SHA256);
    if (res.is_err())
        return Result<void>::err(Error::make(Error::Code::UpdateFailed,
                                              "Downloaded update failed checksum verification",
                                              res.error().full()));
    return Result<void>::ok();
}

Result<void> Updater::apply_update(const std::string& temp_path,
                                    const std::string& current_exe_path)
{
    log->info("Applying update to: " + current_exe_path);
    std::string backup = current_exe_path + ".backup";

    auto backup_res = platform::copy_file(current_exe_path, backup, true);
    if (backup_res.is_err()) return backup_res;

    auto replace_res = atomic_replace(temp_path, current_exe_path);
    if (replace_res.is_err()) {
        log->error("Update apply failed; restoring backup");
        platform::copy_file(backup, current_exe_path, true);
        return replace_res;
    }

    platform::remove_file(backup);
    log->info("Update applied successfully");
    return Result<void>::ok();
}

Result<void> Updater::rollback(const std::string& backup_path) {
    std::string current = platform::executable_path();
    return atomic_replace(backup_path, current);
}

Result<void> Updater::atomic_replace(const std::string& src, const std::string& dst) {
#ifdef GENESIS_PLATFORM_WINDOWS
    if (!MoveFileExA(src.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return Result<void>::err(Error::make(Error::Code::UpdateFailed,
                                              "MoveFileEx failed", std::to_string(GetLastError())));
    return Result<void>::ok();
#else
    auto res = platform::move_file(src, dst);
    if (res.is_err())
        return Result<void>::err(Error::make(Error::Code::UpdateFailed,
                                              "Atomic replace failed", res.error().full()));
    return Result<void>::ok();
#endif
}

}
