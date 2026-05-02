#pragma once

#include "genesis/core/Result.hpp"
#include "genesis/assets/Downloader.hpp"
#include "genesis/version/VersionManifest.hpp"
#include <string>
#include <functional>
#include <vector>

namespace genesis::assets {

using RepairProgressFn = std::function<void(const std::string& name, float fraction)>;

class AssetManager {
public:
    static constexpr const char* ASSET_BASE_URL =
        "https://resources.download.minecraft.net/";
    static constexpr const char* LIBRARY_BASE_URL =
        "https://libraries.minecraft.net/";

    explicit AssetManager(std::string game_dir);

    core::Result<void> ensure_assets(const version::VersionMeta& meta,
                                     DownloadProgressFn          progress_fn = {});

    core::Result<void> ensure_libraries(const version::VersionMeta& meta,
                                        DownloadProgressFn          progress_fn = {});

    core::Result<void> ensure_client_jar(const version::VersionMeta& meta,
                                         DownloadProgressFn          progress_fn = {});

    core::Result<std::vector<std::string>> verify_all(const version::VersionMeta& meta);

    core::Result<void> repair_corrupted(const version::VersionMeta& meta,
                                        const std::vector<std::string>& corrupted,
                                        RepairProgressFn               progress_fn = {});

    [[nodiscard]] std::string assets_dir()   const;
    [[nodiscard]] std::string libraries_dir() const;
    [[nodiscard]] std::string versions_dir()  const;
    [[nodiscard]] std::string natives_dir(const std::string& version_id) const;

private:
    core::Result<void> download_asset_index(const version::AssetIndex& index);
    core::Result<void> download_asset_objects(const std::string& index_path,
                                              DownloadProgressFn& progress_fn);
    core::Result<void> extract_natives(const version::VersionMeta& meta);

    std::string  game_dir_;
    Downloader   downloader_;
};

}
