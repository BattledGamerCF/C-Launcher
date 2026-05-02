#pragma once

#include "genesis/core/Result.hpp"
#include "genesis/assets/Downloader.hpp"
#include "genesis/instance/Instance.hpp"
#include "genesis/mods/ModrinthClient.hpp"
#include <string>
#include <vector>
#include <functional>

namespace genesis::mods {

// One performance mod entry — Modrinth slug + display name.
struct PerformanceMod {
    std::string slug;          // Modrinth project slug (e.g. "sodium")
    std::string display_name;  // Human-readable name
    bool        required;      // If false, failure to fetch is logged but tolerated
};

struct PerformancePackResult {
    std::vector<std::string> installed;   // mod display names that succeeded
    std::vector<std::string> skipped;     // mods with no compatible version
    std::vector<std::string> failed;      // mods that errored out
    std::string              fabric_loader_version;
};

using ModInstallProgressFn =
    std::function<void(const std::string& mod_name, float fraction_complete)>;

class PerformancePack {
public:
    // The four mods Genesis ships automatically for new-naming versions.
    // Order matters for display; required=true means a hard failure.
    static const std::vector<PerformanceMod>& default_mods();

    // True for Minecraft versions 1.21.11 and above (the post-Microsoft
    // versioning scheme). Vanilla / old-naming versions return false.
    static bool qualifies(const std::string& mc_version);

    // Install Fabric loader + every default_mods() entry compatible with
    // `mc_version` into the instance's mods/ directory. Skips already-present
    // files (idempotent). Returns a summary.
    core::Result<PerformancePackResult>
    install_for(instance::Instance&  instance,
                const std::string&   mc_version,
                ModInstallProgressFn progress = {});

private:
    ModrinthClient      modrinth_;
    assets::Downloader  downloader_{4};

    core::Result<void> install_one(const PerformanceMod& mod,
                                    const std::string&    mc_version,
                                    const std::string&    mods_dir,
                                    PerformancePackResult& out,
                                    ModInstallProgressFn  progress);
};

}
