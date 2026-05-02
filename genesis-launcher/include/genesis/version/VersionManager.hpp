#pragma once

#include "genesis/core/Result.hpp"
#include "genesis/version/VersionManifest.hpp"
#include "genesis/version/Profile.hpp"
#include <string>
#include <vector>
#include <optional>
#include <functional>

namespace genesis::version {

using VersionProgressFn = std::function<void(const std::string& step, float progress)>;

class VersionManager {
public:
    static constexpr const char* MANIFEST_URL =
        "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json";

    explicit VersionManager(std::string cache_dir);

    core::Result<VersionList>  fetch_version_list(bool force_refresh = false);
    core::Result<VersionMeta>  fetch_version_meta(const std::string& version_id);
    core::Result<void>         ensure_version_downloaded(const std::string& version_id,
                                                         const std::string& game_dir,
                                                         VersionProgressFn  progress_fn = {});

    core::Result<std::vector<RuntimeProfile>> list_profiles() const;
    core::Result<RuntimeProfile>              get_profile(const std::string& profile_id) const;
    core::Result<void>                        save_profile(const RuntimeProfile& profile);
    core::Result<void>                        delete_profile(const std::string& profile_id);

    [[nodiscard]] bool is_version_cached(const std::string& version_id) const;
    [[nodiscard]] std::string version_json_path(const std::string& version_id) const;

private:
    core::Result<std::string>  download_to_string(const std::string& url);
    core::Result<VersionList>  load_cached_manifest();
    core::Result<void>         save_manifest(const std::string& json);

    std::string                     cache_dir_;
    std::optional<VersionList>      cached_list_;
    std::chrono::system_clock::time_point list_fetched_at_;
};

}
