#include "genesis/mods/ModrinthClient.hpp"
#include "genesis/assets/Downloader.hpp"
#include "genesis/logging/Logger.hpp"
#include <nlohmann/json.hpp>

namespace genesis::mods {

using json  = nlohmann::json;
using Error = core::Error;
template<typename T> using Result = core::Result<T>;

static auto log = logging::get_logger("Modrinth");

static const std::vector<std::string> kUserAgent = {
    "User-Agent: GenesisLauncher/" GENESIS_VERSION_STRING " (github.com/genesis-launcher)"
};

Result<std::optional<ModrinthVersion>>
ModrinthClient::latest_version(const std::string& project_slug,
                                const std::string& game_version,
                                const std::string& loader)
{
    // Modrinth filter syntax: ?game_versions=["1.21.11"]&loaders=["fabric"]
    std::string url = std::string(MODRINTH_API) + "/project/" + project_slug + "/version"
                    + "?game_versions=%5B%22" + game_version + "%22%5D"
                    + "&loaders=%5B%22"       + loader       + "%22%5D";

    assets::Downloader dl;
    auto resp = dl.fetch_string(url, kUserAgent);
    if (resp.is_err()) {
        log->warn("Modrinth fetch failed for " + project_slug + ": " + resp.error().full());
        return Result<std::optional<ModrinthVersion>>::err(resp.error());
    }

    try {
        auto j = json::parse(resp.value());
        if (!j.is_array() || j.empty())
            return Result<std::optional<ModrinthVersion>>::ok(std::nullopt);

        // Modrinth returns versions newest-first; pick first 'release' if any,
        // otherwise the very first entry.
        const json* chosen = nullptr;
        for (auto& v : j) {
            if (v.value("version_type", "") == "release") { chosen = &v; break; }
        }
        if (!chosen) chosen = &j.front();

        ModrinthVersion mv;
        mv.id             = chosen->value("id", "");
        mv.version_number = chosen->value("version_number", "");
        mv.name           = chosen->value("name", "");
        mv.loader         = loader;
        mv.game_version   = game_version;

        for (auto& f : (*chosen)["files"]) {
            ModrinthFile file;
            file.url      = f.value("url", "");
            file.filename = f.value("filename", "");
            file.size     = f.value("size", int64_t{0});
            file.primary  = f.value("primary", false);
            if (f.contains("hashes") && f["hashes"].contains("sha1"))
                file.sha1 = f["hashes"]["sha1"].get<std::string>();
            mv.files.push_back(std::move(file));
        }

        if (chosen->contains("dependencies")) {
            for (auto& d : (*chosen)["dependencies"]) {
                if (d.value("dependency_type", "") == "required" &&
                    d.contains("project_id"))
                    mv.dependencies.push_back(d["project_id"].get<std::string>());
            }
        }

        return Result<std::optional<ModrinthVersion>>::ok(std::move(mv));
    } catch (const json::exception& e) {
        return Result<std::optional<ModrinthVersion>>::err(
            Error::make(Error::Code::ParseError,
                        "Modrinth response parse failed for " + project_slug, e.what()));
    }
}

Result<FabricLoaderInfo>
ModrinthClient::latest_fabric_loader(const std::string& game_version)
{
    // FabricMC meta API: https://meta.fabricmc.net/v2/versions/loader/<mc>
    std::string url = std::string(FABRIC_META) + "/versions/loader/" + game_version;
    assets::Downloader dl;
    auto resp = dl.fetch_string(url, kUserAgent);
    if (resp.is_err())
        return Result<FabricLoaderInfo>::err(resp.error());

    try {
        auto j = json::parse(resp.value());
        if (!j.is_array() || j.empty())
            return Result<FabricLoaderInfo>::err(Error::make(Error::Code::NotFound,
                "No Fabric loader available for " + game_version));

        // Pick first entry whose loader is marked stable.
        const json* chosen = nullptr;
        for (auto& entry : j) {
            if (entry.contains("loader") && entry["loader"].value("stable", false)) {
                chosen = &entry;
                break;
            }
        }
        if (!chosen) chosen = &j.front();

        FabricLoaderInfo info;
        info.loader_version   = (*chosen)["loader"].value("version", "");
        info.game_version     = game_version;
        info.profile_json_url = std::string(FABRIC_META) + "/versions/loader/"
                              + game_version + "/" + info.loader_version
                              + "/profile/json";
        return Result<FabricLoaderInfo>::ok(std::move(info));
    } catch (const json::exception& e) {
        return Result<FabricLoaderInfo>::err(
            Error::make(Error::Code::ParseError,
                        "Fabric meta parse failed", e.what()));
    }
}

}
