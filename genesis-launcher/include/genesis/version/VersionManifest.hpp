#pragma once

#include "genesis/core/Result.hpp"
#include <string>
#include <vector>
#include <optional>
#include <chrono>

namespace genesis::version {

enum class ReleaseType { Release, Snapshot, OldBeta, OldAlpha, Unknown };
ReleaseType release_type_from_string(const std::string& s);
std::string release_type_to_string(ReleaseType t);

struct VersionEntry {
    std::string id;
    ReleaseType type;
    std::string url;
    std::string sha1;
    std::string release_time;
};

struct VersionList {
    std::string              latest_release;
    std::string              latest_snapshot;
    std::vector<VersionEntry> versions;

    [[nodiscard]] std::optional<VersionEntry> find(const std::string& id) const;
    [[nodiscard]] std::vector<VersionEntry>   releases() const;
};

struct DownloadInfo {
    std::string sha1;
    int64_t     size;
    std::string url;
};

struct LibraryArtifact {
    std::string path;
    std::string sha1;
    int64_t     size;
    std::string url;
};

struct LibraryClassifiers {
    std::optional<LibraryArtifact> natives_windows;
    std::optional<LibraryArtifact> natives_macos;
    std::optional<LibraryArtifact> natives_linux;
};

struct Library {
    std::string                   name;
    std::optional<LibraryArtifact> artifact;
    std::optional<LibraryClassifiers> classifiers;
    std::vector<std::string>      rules;

    [[nodiscard]] bool active_on_current_platform() const;
    [[nodiscard]] std::optional<LibraryArtifact> native_for_platform() const;
};

struct AssetIndex {
    std::string id;
    std::string sha1;
    int64_t     size;
    int64_t     total_size;
    std::string url;
};

struct JavaVersion {
    std::string component;
    int         major_version;
};

struct VersionMeta {
    std::string              id;
    std::string              main_class;
    std::string              minecraft_arguments;
    std::string              arguments_json;
    AssetIndex               asset_index;
    DownloadInfo             client_download;
    DownloadInfo             server_download;
    std::vector<Library>     libraries;
    JavaVersion              java_version;
    std::string              release_time;
    ReleaseType              type;
};

core::Result<VersionList> parse_version_list(const std::string& json);
core::Result<VersionMeta> parse_version_meta(const std::string& json);

}
