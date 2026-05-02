#pragma once

#include "genesis/core/Result.hpp"
#include <string>
#include <vector>
#include <optional>

namespace genesis::mods {

struct ModrinthFile {
    std::string url;
    std::string filename;
    std::string sha1;
    int64_t     size = 0;
    bool        primary = false;
};

struct ModrinthVersion {
    std::string                 id;
    std::string                 version_number;
    std::string                 name;
    std::string                 loader;          // "fabric", "forge", ...
    std::string                 game_version;
    std::vector<ModrinthFile>   files;
    std::vector<std::string>    dependencies;    // project_ids of required deps

    const ModrinthFile* primary_file() const {
        for (auto& f : files) if (f.primary) return &f;
        return files.empty() ? nullptr : &files.front();
    }
};

struct FabricLoaderInfo {
    std::string loader_version;
    std::string game_version;
    std::string profile_json_url;   // launchermeta-style profile JSON
};

class ModrinthClient {
public:
    // Fetch the newest version of `project_slug` (e.g. "sodium") that targets
    // `game_version` and uses `loader` (e.g. "fabric"). Returns std::nullopt if
    // no compatible version exists.
    core::Result<std::optional<ModrinthVersion>>
    latest_version(const std::string& project_slug,
                   const std::string& game_version,
                   const std::string& loader = "fabric");

    // Fetch the latest stable Fabric loader release.
    core::Result<FabricLoaderInfo>
    latest_fabric_loader(const std::string& game_version);

    static constexpr const char* MODRINTH_API = "https://api.modrinth.com/v2";
    static constexpr const char* FABRIC_META  = "https://meta.fabricmc.net/v2";
};

}
