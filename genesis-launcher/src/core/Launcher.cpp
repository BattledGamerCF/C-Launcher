#include "genesis/core/Launcher.hpp"
#include "genesis/platform/PlatformUtils.hpp"
#include "genesis/logging/Logger.hpp"
#include <nlohmann/json.hpp>
#include <fstream>

namespace genesis::core {

using json = nlohmann::json;
static auto log = logging::get_logger("Launcher");

Launcher::Launcher()
    : state_machine_(std::make_unique<StateMachine>())
{
}

Launcher::~Launcher() {
    if (initialized_) shutdown();
}

Result<void> Launcher::initialize(const std::string& config_path) {
    if (initialized_) return Result<void>::ok();
    if (!state_machine_->transition_to(LauncherState::Initializing))
        return Result<void>::err(Error::make(Error::Code::InvalidConfig, "Cannot start initialization from current state"));

    log->info("Genesis Launcher initializing...");

    auto cfg_res = load_config(config_path);
    if (cfg_res.is_err()) {
        state_machine_->transition_to(LauncherState::Error);
        return cfg_res;
    }

    auto dir_res = prepare_directories();
    if (dir_res.is_err()) {
        state_machine_->transition_to(LauncherState::Error);
        return dir_res;
    }

    const std::string logs_dir   = platform::path_join(root_dir_, "logs");
    const std::string cache_dir  = platform::path_join(root_dir_, "cache");
    const std::string inst_dir   = platform::path_join(root_dir_, "instances");
    const std::string update_dir = platform::path_join(root_dir_, "updates");

    logging::init_logging(logs_dir, true);

    auth_manager_     = std::make_unique<auth::AuthManager>("Genesis");
    version_manager_  = std::make_unique<version::VersionManager>(cache_dir);
    asset_manager_    = std::make_unique<assets::AssetManager>(root_dir_);
    jvm_orchestrator_ = std::make_unique<jvm::JvmOrchestrator>(root_dir_);
    instance_manager_ = std::make_unique<instance::InstanceManager>(inst_dir);
    updater_          = std::make_unique<update::Updater>("stable");

    auto load_res = instance_manager_->load_all();
    if (load_res.is_err()) {
        log->warn("Failed to load instances: " + load_res.error().full());
    }

    initialized_ = true;
    state_machine_->transition_to(LauncherState::Idle);
    log->info("Genesis Launcher ready.");
    return Result<void>::ok();
}

Result<void> Launcher::shutdown() {
    log->info("Launcher shutting down.");
    state_machine_->transition_to(LauncherState::Shutdown);
    logging::shutdown_logging();
    initialized_ = false;
    return Result<void>::ok();
}

Result<LaunchReport> Launcher::launch(LaunchRequest req) {
    log->info("Launch requested: instance=" + req.instance_id + " version=" + req.version_id);

    auto cred_res = auth_manager_->ensure_valid_credential();
    if (cred_res.is_err()) {
        state_machine_->transition_to(LauncherState::Error);
        return Result<LaunchReport>::err(cred_res.error());
    }
    auto& cred = cred_res.value();

    if (!state_machine_->transition_to(LauncherState::ResolvingVersion))
        return Result<LaunchReport>::err(Error::make(Error::Code::Unknown, "Cannot begin launch: bad state"));

    auto meta_res = version_manager_->fetch_version_meta(req.version_id);
    if (meta_res.is_err()) {
        state_machine_->transition_to(LauncherState::Error);
        return Result<LaunchReport>::err(meta_res.error());
    }
    auto& meta = meta_res.value();

    state_machine_->transition_to(LauncherState::DownloadingAssets);
    EventBus::global().publish(DownloadProgressEvent{"Verifying assets", 0, 1});

    auto asset_res = asset_manager_->ensure_assets(meta, [](const std::string& n, int64_t d, int64_t t) {
        EventBus::global().publish(DownloadProgressEvent{n, d, t});
    });
    if (asset_res.is_err()) {
        state_machine_->transition_to(LauncherState::Error);
        return Result<LaunchReport>::err(asset_res.error());
    }

    auto lib_res = asset_manager_->ensure_libraries(meta, [](const std::string& n, int64_t d, int64_t t) {
        EventBus::global().publish(DownloadProgressEvent{n, d, t});
    });
    if (lib_res.is_err()) {
        state_machine_->transition_to(LauncherState::Error);
        return Result<LaunchReport>::err(lib_res.error());
    }

    state_machine_->transition_to(LauncherState::PreparingLaunch);

    auto inst_opt = instance_manager_->find(req.instance_id);
    if (!inst_opt) {
        state_machine_->transition_to(LauncherState::Error);
        return Result<LaunchReport>::err(Error::make(Error::Code::InstanceNotFound,
                                                      "Instance not found: " + req.instance_id));
    }
    auto& inst = inst_opt->get();

    auto jvm_res = jvm_orchestrator_->build_config(
        meta, inst.root_dir(),
        cred.profile.username,
        cred.profile.uuid,
        cred.minecraft.access_token);
    if (jvm_res.is_err()) {
        state_machine_->transition_to(LauncherState::Error);
        return Result<LaunchReport>::err(jvm_res.error());
    }

    state_machine_->transition_to(LauncherState::Launching);

    auto launch_start = std::chrono::system_clock::now();
    int32_t exit_code = -1;

    auto proc_res = jvm_orchestrator_->launch(
        jvm_res.value(),
        [&](int32_t code) { exit_code = code; },
        [](const std::string& line) { logging::get_logger("MC")->info(line); },
        [](const std::string& line) { logging::get_logger("MC")->warn(line); });

    if (proc_res.is_err()) {
        state_machine_->transition_to(LauncherState::Error);
        return Result<LaunchReport>::err(proc_res.error());
    }

    state_machine_->transition_to(LauncherState::Running);
    proc_res.value().wait();
    state_machine_->transition_to(LauncherState::Stopping);

    auto launch_end = std::chrono::system_clock::now();
    auto play_secs  = std::chrono::duration_cast<std::chrono::seconds>(launch_end - launch_start).count();
    inst.record_play_session(static_cast<uint64_t>(play_secs));

    state_machine_->transition_to(LauncherState::Idle);

    return Result<LaunchReport>::ok(LaunchReport{
        exit_code,
        req.instance_id,
        req.version_id,
        static_cast<uint64_t>(play_secs)
    });
}

LauncherState Launcher::state() const { return state_machine_->current(); }
auth::AuthManager& Launcher::auth_manager() const { return *auth_manager_; }
version::VersionManager& Launcher::version_manager() const { return *version_manager_; }
assets::AssetManager& Launcher::asset_manager() const { return *asset_manager_; }
jvm::JvmOrchestrator& Launcher::jvm_orchestrator() const { return *jvm_orchestrator_; }
instance::InstanceManager& Launcher::instance_manager() const { return *instance_manager_; }
update::Updater& Launcher::updater() const { return *updater_; }

void Launcher::on_state_change(StateObserver observer) {
    state_machine_->on_transition(std::move(observer));
}

Result<void> Launcher::load_config(const std::string& path) {
    if (!path.empty() && platform::is_file(path)) {
        auto text_res = platform::read_file(path);
        if (text_res.is_err()) return Result<void>::err(text_res.error());
        try {
            auto j = json::parse(text_res.value());
            root_dir_ = j.value("root_dir", platform::default_game_dir());
        } catch (const json::exception& e) {
            return Result<void>::err(Error::make(Error::Code::ParseError,
                                                  "Config parse error", e.what()));
        }
    } else {
        root_dir_ = platform::default_game_dir();
    }
    log->info("Root directory: " + root_dir_);
    return Result<void>::ok();
}

Result<void> Launcher::prepare_directories() {
    for (auto& sub : {"cache", "instances", "logs", "updates"}) {
        auto dir = platform::path_join(root_dir_, sub);
        auto res = platform::create_directories(dir);
        if (res.is_err()) return res;
    }
    return Result<void>::ok();
}

}
