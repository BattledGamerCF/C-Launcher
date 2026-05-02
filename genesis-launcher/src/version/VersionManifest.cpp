#include "genesis/version/VersionManifest.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>

namespace genesis::version {

using json  = nlohmann::json;
using Error = core::Error;
template<typename T> using Result = core::Result<T>;

ReleaseType release_type_from_string(const std::string& s) {
    if (s == "release")   return ReleaseType::Release;
    if (s == "snapshot")  return ReleaseType::Snapshot;
    if (s == "old_beta")  return ReleaseType::OldBeta;
    if (s == "old_alpha") return ReleaseType::OldAlpha;
    return ReleaseType::Unknown;
}

std::string release_type_to_string(ReleaseType t) {
    switch (t) {
        case ReleaseType::Release:   return "release";
        case ReleaseType::Snapshot:  return "snapshot";
        case ReleaseType::OldBeta:   return "old_beta";
        case ReleaseType::OldAlpha:  return "old_alpha";
        default:                     return "unknown";
    }
}

std::optional<VersionEntry> VersionList::find(const std::string& id) const {
    for (auto& v : versions)
        if (v.id == id) return v;
    return std::nullopt;
}

std::vector<VersionEntry> VersionList::releases() const {
    std::vector<VersionEntry> out;
    for (auto& v : versions)
        if (v.type == ReleaseType::Release) out.push_back(v);
    return out;
}

static std::string safe_string(const json& j, const std::string& key) {
    return j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : "";
}

static int64_t safe_int64(const json& j, const std::string& key) {
    return j.contains(key) && j[key].is_number() ? j[key].get<int64_t>() : 0;
}

Result<VersionList> parse_version_list(const std::string& raw_json) {
    try {
        auto j = json::parse(raw_json);
        VersionList list;
        list.latest_release  = safe_string(j["latest"], "release");
        list.latest_snapshot = safe_string(j["latest"], "snapshot");

        for (auto& v : j["versions"]) {
            VersionEntry e;
            e.id           = safe_string(v, "id");
            e.type         = release_type_from_string(safe_string(v, "type"));
            e.url          = safe_string(v, "url");
            e.sha1         = safe_string(v, "sha1");
            e.release_time = safe_string(v, "releaseTime");
            list.versions.push_back(std::move(e));
        }
        return Result<VersionList>::ok(std::move(list));
    } catch (const json::exception& e) {
        return Result<VersionList>::err(Error::make(Error::Code::ParseError,
                                                     "Version list parse error", e.what()));
    }
}

static DownloadInfo parse_download(const json& j) {
    return DownloadInfo{
        safe_string(j, "sha1"),
        safe_int64(j, "size"),
        safe_string(j, "url")
    };
}

static LibraryArtifact parse_artifact(const json& j) {
    return LibraryArtifact{
        safe_string(j, "path"),
        safe_string(j, "sha1"),
        safe_int64(j, "size"),
        safe_string(j, "url")
    };
}

bool Library::active_on_current_platform() const {
    if (rules.empty()) return true;
    return true;
}

std::optional<LibraryArtifact> Library::native_for_platform() const {
    if (!classifiers) return std::nullopt;
#if defined(GENESIS_PLATFORM_WINDOWS)
    return classifiers->natives_windows;
#elif defined(GENESIS_PLATFORM_MACOS)
    return classifiers->natives_macos;
#else
    return classifiers->natives_linux;
#endif
}

Result<VersionMeta> parse_version_meta(const std::string& raw_json) {
    try {
        auto j = json::parse(raw_json);
        VersionMeta meta;
        meta.id          = safe_string(j, "id");
        meta.main_class  = safe_string(j, "mainClass");
        meta.release_time = safe_string(j, "releaseTime");
        meta.type        = release_type_from_string(safe_string(j, "type"));

        if (j.contains("minecraftArguments"))
            meta.minecraft_arguments = j["minecraftArguments"].get<std::string>();
        else if (j.contains("arguments"))
            meta.arguments_json = j["arguments"].dump();

        if (j.contains("assetIndex")) {
            auto& ai      = j["assetIndex"];
            meta.asset_index.id         = safe_string(ai, "id");
            meta.asset_index.sha1       = safe_string(ai, "sha1");
            meta.asset_index.size       = safe_int64(ai, "size");
            meta.asset_index.total_size = safe_int64(ai, "totalSize");
            meta.asset_index.url        = safe_string(ai, "url");
        }

        if (j.contains("downloads")) {
            auto& dl = j["downloads"];
            if (dl.contains("client")) meta.client_download = parse_download(dl["client"]);
            if (dl.contains("server")) meta.server_download = parse_download(dl["server"]);
        }

        if (j.contains("javaVersion")) {
            meta.java_version.component     = safe_string(j["javaVersion"], "component");
            meta.java_version.major_version = j["javaVersion"].value("majorVersion", 8);
        } else {
            meta.java_version.major_version = 8;
        }

        for (auto& lib : j.value("libraries", json::array())) {
            Library l;
            l.name = safe_string(lib, "name");

            if (lib.contains("downloads")) {
                auto& downloads = lib["downloads"];
                if (downloads.contains("artifact"))
                    l.artifact = parse_artifact(downloads["artifact"]);

                if (downloads.contains("classifiers")) {
                    LibraryClassifiers cls;
                    auto& c = downloads["classifiers"];
                    if (c.contains("natives-windows")) cls.natives_windows = parse_artifact(c["natives-windows"]);
                    if (c.contains("natives-osx"))     cls.natives_macos   = parse_artifact(c["natives-osx"]);
                    if (c.contains("natives-linux"))   cls.natives_linux   = parse_artifact(c["natives-linux"]);
                    l.classifiers = cls;
                }
            }

            meta.libraries.push_back(std::move(l));
        }

        return Result<VersionMeta>::ok(std::move(meta));
    } catch (const json::exception& e) {
        return Result<VersionMeta>::err(Error::make(Error::Code::ParseError,
                                                     "Version meta parse error", e.what()));
    }
}

}
