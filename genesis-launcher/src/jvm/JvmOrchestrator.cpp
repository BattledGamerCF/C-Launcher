#include "genesis/jvm/JvmOrchestrator.hpp"
#include "genesis/platform/PlatformUtils.hpp"
#include "genesis/logging/Logger.hpp"
#include <sstream>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <fstream>

namespace genesis::jvm {

using json  = nlohmann::json;
using Error = core::Error;
template<typename T> using Result = core::Result<T>;

static auto log = logging::get_logger("JvmOrchestrator");

JvmOrchestrator::JvmOrchestrator(std::string game_dir)
    : game_dir_(std::move(game_dir)) {}

Result<std::string> JvmOrchestrator::build_classpath(
    const version::VersionMeta& meta,
    const std::string& version_dir) const
{
    std::string cp;
    std::string sep;

#ifdef GENESIS_PLATFORM_WINDOWS
    sep = ";";
#else
    sep = ":";
#endif

    std::string libs_dir = platform::path_join(game_dir_, "libraries");
    for (auto& lib : meta.libraries) {
        if (!lib.active_on_current_platform()) continue;
        if (!lib.artifact) continue;
        cp += platform::path_join(libs_dir, lib.artifact->path) + sep;
    }

    cp += platform::path_join(version_dir, meta.id + ".jar");
    return Result<std::string>::ok(std::move(cp));
}

Result<JvmConfig> JvmOrchestrator::build_config(
    const version::VersionMeta& meta,
    const std::string& instance_dir,
    const std::string& username,
    const std::string& uuid,
    const std::string& access_token,
    const std::optional<JvmProfile>& profile_override)
{
    auto java_res = JavaFinder::find_best(meta.java_version.major_version);
    if (java_res.is_err())
        return Result<JvmConfig>::err(Error::make(Error::Code::JvmNotFound,
            "Could not find Java " + std::to_string(meta.java_version.major_version) + "+",
            java_res.error().full()));

    std::string version_dir = platform::path_join({game_dir_, "versions", meta.id});
    auto cp_res = build_classpath(meta, version_dir);
    if (cp_res.is_err()) return Result<JvmConfig>::err(cp_res.error());

    JvmProfile profile = profile_override.value_or(get_or_create_default());

    JvmConfig cfg;
    cfg.java_executable = java_res.value().executable_path;
    cfg.memory          = profile.memory;
    cfg.gc_strategy     = profile.gc_strategy;
    cfg.extra_jvm_args  = profile.extra_jvm_args;
    cfg.main_class      = meta.main_class;
    cfg.classpath       = cp_res.value();
    cfg.natives_path    = platform::path_join({game_dir_, "versions", meta.id, meta.id + "-natives"});
    cfg.game_dir        = instance_dir;
    cfg.assets_dir      = platform::path_join(game_dir_, "assets");
    cfg.asset_index_id  = meta.asset_index.id;
    cfg.username        = username;
    cfg.uuid            = uuid;
    cfg.access_token    = access_token;
    cfg.version_id      = meta.id;
    cfg.version_type    = version::release_type_to_string(meta.type);

    std::string err;
    if (!cfg.is_valid(err))
        return Result<JvmConfig>::err(Error::make(Error::Code::InvalidConfig,
                                                   "JVM config validation failed", err));

    log->info("JVM config built for version " + meta.id + " using " + cfg.java_executable);
    return Result<JvmConfig>::ok(std::move(cfg));
}

Result<ProcessHandle> JvmOrchestrator::launch(
    const JvmConfig& config,
    ProcessExitFn   on_exit,
    ProcessOutputFn on_stdout,
    ProcessOutputFn on_stderr)
{
    auto argv = config.build_argv();
    log->info("Spawning JVM: " + config.java_executable);

    auto res = platform::run_process(config.java_executable,
                                     std::vector<std::string>(argv.begin() + 1, argv.end()),
                                     config.game_dir);
    if (res.is_err())
        return Result<ProcessHandle>::err(Error::make(Error::Code::JvmLaunchFailed,
                                                       "Process spawn failed", res.error().full()));

    if (on_stdout && !res.value().stdout_output.empty())
        on_stdout(res.value().stdout_output);
    if (on_stderr && !res.value().stderr_output.empty())
        on_stderr(res.value().stderr_output);
    if (on_exit)
        on_exit(res.value().exit_code);

    ProcessHandle handle;
    handle.pid         = 0;
    handle.instance_id = config.game_dir;
    return Result<ProcessHandle>::ok(std::move(handle));
}

JvmProfile JvmOrchestrator::get_or_create_default() const {
    for (auto& p : profiles_)
        if (p.is_default) return p;

    JvmProfile def;
    def.id          = "default";
    def.display_name = "Default";
    def.memory      = MemorySpec::default_for_system();
    def.gc_strategy = GcStrategy::G1GC;
    def.is_default  = true;
    return def;
}

Result<void> JvmOrchestrator::save_profile(const JvmProfile& profile) {
    auto it = std::find_if(profiles_.begin(), profiles_.end(),
                           [&](const JvmProfile& p) { return p.id == profile.id; });
    if (it != profiles_.end()) *it = profile;
    else profiles_.push_back(profile);
    return Result<void>::ok();
}

Result<void> JvmOrchestrator::delete_profile(const std::string& id) {
    auto it = std::find_if(profiles_.begin(), profiles_.end(),
                           [&](const JvmProfile& p) { return p.id == id; });
    if (it == profiles_.end())
        return Result<void>::err(Error::make(Error::Code::InvalidConfig, "Profile not found: " + id));
    profiles_.erase(it);
    return Result<void>::ok();
}

std::vector<JvmProfile> JvmOrchestrator::list_profiles() const { return profiles_; }

std::optional<JvmProfile> JvmOrchestrator::default_profile() const {
    for (auto& p : profiles_) if (p.is_default) return p;
    return std::nullopt;
}

bool ProcessHandle::is_running() const { return false; }
core::Result<void> ProcessHandle::wait(int) { return core::Result<void>::ok(); }
core::Result<void> ProcessHandle::terminate() { return core::Result<void>::ok(); }
core::Result<void> ProcessHandle::kill() { return core::Result<void>::ok(); }

}
