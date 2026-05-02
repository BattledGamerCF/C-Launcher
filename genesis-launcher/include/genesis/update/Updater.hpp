#pragma once

#include "genesis/core/Result.hpp"
#include <string>
#include <functional>
#include <optional>

namespace genesis::update {

struct ReleaseInfo {
    std::string version;
    std::string download_url;
    std::string checksum_sha256;
    std::string changelog;
    std::string channel;
    bool        is_prerelease = false;
};

using UpdateProgressFn = std::function<void(float fraction, const std::string& step)>;

class Updater {
public:
    static constexpr const char* UPDATE_MANIFEST_URL =
        "https://genesis-launcher.io/releases/manifest.json";
    static constexpr const char* CURRENT_VERSION =
        GENESIS_VERSION_STRING;

    explicit Updater(std::string channel = "stable");

    core::Result<std::optional<ReleaseInfo>> check_for_update();

    core::Result<void> download_update(const ReleaseInfo& release,
                                       const std::string& temp_path,
                                       UpdateProgressFn   progress_fn = {});

    core::Result<void> apply_update(const std::string& temp_path,
                                    const std::string& current_exe_path);

    core::Result<void> rollback(const std::string& backup_path);

    [[nodiscard]] bool is_newer(const std::string& candidate) const;

private:
    core::Result<void> verify_downloaded(const std::string& path,
                                          const std::string& expected_sha256);
    core::Result<void> atomic_replace(const std::string& src, const std::string& dst);

    std::string channel_;
};

}
