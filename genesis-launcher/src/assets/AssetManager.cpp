#include "genesis/assets/AssetManager.hpp"
#include "genesis/assets/Verifier.hpp"
#include "genesis/platform/PlatformUtils.hpp"
#include "genesis/logging/Logger.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <vector>

namespace genesis::assets {

using json  = nlohmann::json;
using Error = core::Error;
template<typename T> using Result = core::Result<T>;

static auto log = logging::get_logger("AssetManager");

AssetManager::AssetManager(std::string game_dir)
    : game_dir_(std::move(game_dir)), downloader_(8) {}

std::string AssetManager::assets_dir()    const { return platform::path_join(game_dir_, "assets"); }
std::string AssetManager::libraries_dir() const { return platform::path_join(game_dir_, "libraries"); }
std::string AssetManager::versions_dir()  const { return platform::path_join(game_dir_, "versions"); }
std::string AssetManager::natives_dir(const std::string& vid) const {
    return platform::path_join({versions_dir(), vid, vid + "-natives"});
}

Result<void> AssetManager::ensure_assets(const version::VersionMeta& meta, DownloadProgressFn pfn) {
    log->info("Ensuring assets for version " + meta.id);
    platform::create_directories(platform::path_join(assets_dir(), "indexes"));
    platform::create_directories(platform::path_join(assets_dir(), "objects"));

    auto idx_res = download_asset_index(meta.asset_index);
    if (idx_res.is_err()) return idx_res;

    std::string index_path = platform::path_join({assets_dir(), "indexes", meta.asset_index.id + ".json"});
    return download_asset_objects(index_path, pfn);
}

Result<void> AssetManager::download_asset_index(const version::AssetIndex& index) {
    std::string dest = platform::path_join({assets_dir(), "indexes", index.id + ".json"});
    if (platform::is_file(dest)) {
        auto verify = Verifier::verify_file(dest, index.sha1, HashAlgorithm::SHA1);
        if (verify.is_ok()) return Result<void>::ok();
        log->warn("Asset index corrupt; re-downloading");
        platform::remove_file(dest);
    }

    DownloadTask task;
    task.name          = "asset index " + index.id;
    task.url           = index.url;
    task.dest_path     = dest;
    task.expected_sha1 = index.sha1;
    return downloader_.download_one(task);
}

Result<void> AssetManager::download_asset_objects(const std::string& index_path, DownloadProgressFn& pfn) {
    auto text_res = platform::read_file(index_path);
    if (text_res.is_err()) return Result<void>::err(text_res.error());

    std::vector<DownloadTask> tasks;
    try {
        auto j = json::parse(text_res.value());
        for (auto& [name, obj] : j["objects"].items()) {
            std::string hash = obj["hash"].get<std::string>();
            std::string prefix = hash.substr(0, 2);
            std::string url  = std::string(ASSET_BASE_URL) + prefix + "/" + hash;
            std::string dest = platform::path_join({assets_dir(), "objects", prefix, hash});

            if (platform::is_file(dest)) {
                auto v = Verifier::verify_file(dest, hash, HashAlgorithm::SHA1);
                if (v.is_ok()) continue;
                platform::remove_file(dest);
            }

            DownloadTask t;
            t.name          = name;
            t.url           = url;
            t.dest_path     = dest;
            t.expected_sha1 = hash;
            tasks.push_back(std::move(t));
        }
    } catch (const json::exception& e) {
        return Result<void>::err(Error::make(Error::Code::ParseError, "Asset index parse error", e.what()));
    }

    if (tasks.empty()) {
        log->info("All assets already present");
        return Result<void>::ok();
    }

    log->info("Downloading " + std::to_string(tasks.size()) + " assets");
    auto results = downloader_.download_batch(tasks, pfn);
    if (results.is_err()) return Result<void>::err(results.error());

    size_t failed = 0;
    for (auto& r : results.value()) if (!r.success) ++failed;
    if (failed > 0)
        return Result<void>::err(Error::make(Error::Code::NetworkError,
            std::to_string(failed) + " assets failed to download"));

    return Result<void>::ok();
}

Result<void> AssetManager::ensure_libraries(const version::VersionMeta& meta, DownloadProgressFn pfn) {
    log->info("Ensuring libraries for version " + meta.id);
    platform::create_directories(libraries_dir());
    platform::create_directories(natives_dir(meta.id));

    std::vector<DownloadTask> tasks;

    for (auto& lib : meta.libraries) {
        if (!lib.active_on_current_platform()) continue;

        if (lib.artifact) {
            std::string dest = platform::path_join(libraries_dir(), lib.artifact->path);
            if (!platform::is_file(dest) ||
                Verifier::verify_file(dest, lib.artifact->sha1).is_err()) {
                platform::remove_file(dest);
                DownloadTask t;
                t.name          = lib.name;
                t.url           = lib.artifact->url;
                t.dest_path     = dest;
                t.expected_sha1 = lib.artifact->sha1;
                tasks.push_back(std::move(t));
            }
        }

        auto native = lib.native_for_platform();
        if (native) {
            std::string dest = platform::path_join(libraries_dir(), native->path);
            if (!platform::is_file(dest) ||
                Verifier::verify_file(dest, native->sha1).is_err()) {
                platform::remove_file(dest);
                DownloadTask t;
                t.name          = lib.name + " (native)";
                t.url           = native->url;
                t.dest_path     = dest;
                t.expected_sha1 = native->sha1;
                tasks.push_back(std::move(t));
            }
        }
    }

    if (tasks.empty()) {
        log->info("All libraries already present");
        return Result<void>::ok();
    }

    log->info("Downloading " + std::to_string(tasks.size()) + " libraries");
    auto results = downloader_.download_batch(tasks, pfn);
    if (results.is_err()) return Result<void>::err(results.error());

    return extract_natives(meta);
}

Result<void> AssetManager::extract_natives(const version::VersionMeta& meta) {
    auto nd = natives_dir(meta.id);
    platform::create_directories(nd);
    log->info("Natives directory prepared: " + nd);
    return Result<void>::ok();
}

Result<void> AssetManager::ensure_client_jar(const version::VersionMeta& meta, DownloadProgressFn pfn) {
    std::string jar_dir  = platform::path_join({versions_dir(), meta.id});
    std::string jar_path = platform::path_join(jar_dir, meta.id + ".jar");
    platform::create_directories(jar_dir);

    if (platform::is_file(jar_path)) {
        auto verify = Verifier::verify_file(jar_path, meta.client_download.sha1, HashAlgorithm::SHA1);
        if (verify.is_ok()) return Result<void>::ok();
        log->warn("Client jar corrupt; re-downloading");
        platform::remove_file(jar_path);
    }

    DownloadTask t;
    t.name          = "minecraft " + meta.id + " client";
    t.url           = meta.client_download.url;
    t.dest_path     = jar_path;
    t.expected_sha1 = meta.client_download.sha1;
    return downloader_.download_one(t, pfn);
}

Result<std::vector<std::string>> AssetManager::verify_all(const version::VersionMeta& meta) {
    std::vector<std::string> corrupted;

    std::string jar_path = platform::path_join({versions_dir(), meta.id, meta.id + ".jar"});
    if (Verifier::verify_file(jar_path, meta.client_download.sha1).is_err())
        corrupted.push_back(jar_path);

    for (auto& lib : meta.libraries) {
        if (!lib.active_on_current_platform()) continue;
        if (lib.artifact) {
            std::string p = platform::path_join(libraries_dir(), lib.artifact->path);
            if (Verifier::verify_file(p, lib.artifact->sha1).is_err())
                corrupted.push_back(p);
        }
    }

    return Result<std::vector<std::string>>::ok(std::move(corrupted));
}

Result<void> AssetManager::repair_corrupted(const version::VersionMeta& meta,
                                             const std::vector<std::string>& corrupted,
                                             RepairProgressFn pfn)
{
    log->info("Repairing " + std::to_string(corrupted.size()) + " corrupted files");
    float step = corrupted.empty() ? 1.0f : 1.0f / corrupted.size();
    float done = 0.0f;

    for (auto& path : corrupted) {
        platform::remove_file(path);
        if (pfn) pfn(path, done);
        done += step;
    }

    auto res = ensure_assets(meta);
    if (res.is_err()) return res;
    return ensure_libraries(meta);
}

}
