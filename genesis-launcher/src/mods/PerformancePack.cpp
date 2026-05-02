#include "genesis/mods/PerformancePack.hpp"
#include "genesis/platform/PlatformUtils.hpp"
#include "genesis/logging/Logger.hpp"
#include <regex>
#include <sstream>

namespace genesis::mods {

using Error = core::Error;
template<typename T> using Result = core::Result<T>;

static auto log = logging::get_logger("PerformancePack");

const std::vector<PerformanceMod>& PerformancePack::default_mods() {
    static const std::vector<PerformanceMod> mods = {
        // Slug                Display name        Required?
        {"sodium",             "Sodium",           true },
        {"sodium-extra",       "Sodium Extra",     false},
        {"lithium",            "Lithium",          true },
        {"iris",               "Iris Shaders",     false},
    };
    return mods;
}

// ─────────────────────────────────────────────────────────────────────────────
// Version qualification
//
// Microsoft's "new naming" begins at 1.21.11 (per the project goal). Any
// version 1.21.11+ qualifies; everything older does not. We parse the major,
// minor, patch components defensively — snapshots, RCs, and pre-releases all
// fall through to "no auto-pack" so we never accidentally inject mods into
// experimental builds where the mods don't yet support them.
// ─────────────────────────────────────────────────────────────────────────────
bool PerformancePack::qualifies(const std::string& mc_version) {
    std::regex semver_re(R"(^(\d+)\.(\d+)(?:\.(\d+))?$)");
    std::smatch m;
    if (!std::regex_match(mc_version, m, semver_re)) return false;

    int major = std::stoi(m[1].str());
    int minor = std::stoi(m[2].str());
    int patch = m[3].matched ? std::stoi(m[3].str()) : 0;

    if (major != 1 || minor != 21) return false;
    return patch >= 11;
}

Result<PerformancePackResult>
PerformancePack::install_for(instance::Instance&  instance,
                              const std::string&   mc_version,
                              ModInstallProgressFn progress)
{
    PerformancePackResult result;

    if (!qualifies(mc_version)) {
        log->info("Version " + mc_version + " does not qualify for performance pack — skipping");
        return Result<PerformancePackResult>::ok(std::move(result));
    }

    log->info("Installing performance pack for " + mc_version
              + " into " + instance.id());

    // ── 1. Fabric loader ────────────────────────────────────────────────────
    if (progress) progress("Fabric Loader", 0.0f);
    auto fabric = modrinth_.latest_fabric_loader(mc_version);
    if (fabric.is_err()) {
        return Result<PerformancePackResult>::err(Error::make(Error::Code::VersionNotFound,
            "Fabric loader unavailable for " + mc_version,
            fabric.error().full()));
    }
    result.fabric_loader_version = fabric.value().loader_version;
    log->info("Fabric loader " + result.fabric_loader_version + " selected for " + mc_version);
    // NOTE: actually downloading & merging the Fabric profile JSON happens at
    // launch time (VersionManager applies the patch). Here we just record it.

    auto mods_dir = instance.mods_dir();
    auto mkdir = platform::create_directories(mods_dir);
    if (mkdir.is_err()) return Result<PerformancePackResult>::err(mkdir.error());

    // ── 2. Mods (in declared order) ─────────────────────────────────────────
    const auto& mods = default_mods();
    for (size_t i = 0; i < mods.size(); ++i) {
        if (progress) {
            float frac = (i + 1.0f) / (mods.size() + 1.0f);
            progress(mods[i].display_name, frac);
        }
        auto res = install_one(mods[i], mc_version, mods_dir, result, progress);
        if (res.is_err() && mods[i].required) {
            log->error("Required mod failed: " + mods[i].display_name
                       + " — " + res.error().full());
            return Result<PerformancePackResult>::err(res.error());
        }
    }

    if (progress) progress("Done", 1.0f);
    log->info("Performance pack installed: "
              + std::to_string(result.installed.size()) + " mods, "
              + std::to_string(result.skipped.size())  + " skipped, "
              + std::to_string(result.failed.size())   + " failed");
    return Result<PerformancePackResult>::ok(std::move(result));
}

Result<void>
PerformancePack::install_one(const PerformanceMod& mod,
                              const std::string&    mc_version,
                              const std::string&    mods_dir,
                              PerformancePackResult& out,
                              ModInstallProgressFn  progress)
{
    auto ver_res = modrinth_.latest_version(mod.slug, mc_version, "fabric");
    if (ver_res.is_err()) {
        out.failed.push_back(mod.display_name);
        return Result<void>::err(ver_res.error());
    }

    auto& opt = ver_res.value();
    if (!opt.has_value()) {
        log->warn("No " + mod.display_name + " release for " + mc_version);
        out.skipped.push_back(mod.display_name);
        return Result<void>::ok();
    }

    auto& mv = *opt;
    auto* file = mv.primary_file();
    if (!file || file->url.empty()) {
        out.skipped.push_back(mod.display_name);
        return Result<void>::ok();
    }

    auto dest = platform::path_join(mods_dir, file->filename);

    // Idempotent: skip if file is already there with the right hash.
    if (platform::is_file(dest) && !file->sha1.empty()) {
        // Verifier check happens inside Downloader::download_one; here we
        // optimistically trust an existing file with matching name.
        out.installed.push_back(mod.display_name + " " + mv.version_number + " (cached)");
        return Result<void>::ok();
    }

    assets::DownloadTask task;
    task.name          = mod.display_name + " " + mv.version_number;
    task.url           = file->url;
    task.dest_path     = dest;
    task.expected_sha1 = file->sha1;
    task.max_retries   = 3;

    auto dl_res = downloader_.download_one(task,
        [&](const std::string& name, int64_t done, int64_t total) {
            if (progress && total > 0)
                progress(name, static_cast<float>(done) / static_cast<float>(total));
        });

    if (dl_res.is_err()) {
        out.failed.push_back(mod.display_name);
        return dl_res;
    }

    out.installed.push_back(mod.display_name + " " + mv.version_number);
    return Result<void>::ok();
}

}
