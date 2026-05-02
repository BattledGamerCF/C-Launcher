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
    performance_pack_ = std::make_unique<mods::PerformancePack>();

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

Result<std::shared_ptr<jvm::ProcessHandle>> Launcher::launch(LaunchRequest req) {
    using R = Result<std::shared_ptr<jvm::ProcessHandle>>;
    log->info("Launch requested: instance=" + req.instance_id + " version=" + req.version_id);

    auto cred_res = auth_manager_->ensure_valid_credential();
    if (cred_res.is_err()) {
        state_machine_->transition_to(LauncherState::Error);
        return R::err(cred_res.error());
    }
    auto& cred = cred_res.value();

    if (!state_machine_->transition_to(LauncherState::ResolvingVersion))
        return R::err(Error::make(Error::Code::Unknown, "Cannot begin launch: bad state"));

    auto meta_res = version_manager_->fetch_version_meta(req.version_id);
    if (meta_res.is_err()) {
        state_machine_->transition_to(LauncherState::Error);
        return R::err(meta_res.error());
    }
    auto& meta = meta_res.value();

    state_machine_->transition_to(LauncherState::DownloadingAssets);
    EventBus::global().publish(DownloadProgressEvent{"Verifying assets", 0, 1});

    auto asset_res = asset_manager_->ensure_assets(meta, [](const std::string& n, int64_t d, int64_t t) {
        EventBus::global().publish(DownloadProgressEvent{n, d, t});
    });
    if (asset_res.is_err()) {
        state_machine_->transition_to(LauncherState::Error);
        return R::err(asset_res.error());
    }

    auto lib_res = asset_manager_->ensure_libraries(meta, [](const std::string& n, int64_t d, int64_t t) {
        EventBus::global().publish(DownloadProgressEvent{n, d, t});
    });
    if (lib_res.is_err()) {
        state_machine_->transition_to(LauncherState::Error);
        return R::err(lib_res.error());
    }

    state_machine_->transition_to(LauncherState::PreparingLaunch);

    auto inst_opt = instance_manager_->find(req.instance_id);
    if (!inst_opt) {
        state_machine_->transition_to(LauncherState::Error);
        return R::err(Error::make(Error::Code::InstanceNotFound,
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
        return R::err(jvm_res.error());
    }

    state_machine_->transition_to(LauncherState::Launching);

    auto proc_res = jvm_orchestrator_->launch(jvm_res.value(), req.instance_id);
    if (proc_res.is_err()) {
        state_machine_->transition_to(LauncherState::Error);
        return R::err(proc_res.error());
    }

    // Process is alive; wait/terminate is now the caller's responsibility.
    state_machine_->transition_to(LauncherState::Running);
    return R::ok(std::move(proc_res.value()));
}

LauncherState Launcher::state() const { return state_machine_->current(); }
auth::AuthManager& Launcher::auth_manager() const { return *auth_manager_; }
version::VersionManager& Launcher::version_manager() const { return *version_manager_; }
assets::AssetManager& Launcher::asset_manager() const { return *asset_manager_; }
jvm::JvmOrchestrator& Launcher::jvm_orchestrator() const { return *jvm_orchestrator_; }
instance::InstanceManager& Launcher::instance_manager() const { return *instance_manager_; }
update::Updater& Launcher::updater() const { return *updater_; }
mods::PerformancePack& Launcher::performance_pack() const { return *performance_pack_; }

Result<instance::Instance>
Launcher::create_instance_with_performance_pack(
    instance::InstanceConfig   config,
    mods::ModInstallProgressFn progress)
{
    const std::string version = config.game_version;
    auto created = instance_manager_->create(std::move(config));
    if (created.is_err()) return created;

    if (!mods::PerformancePack::qualifies(version)) {
        log->info("Instance created without performance pack (version "
                  + version + " does not qualify)");
        return created;
    }

    auto inst_opt = instance_manager_->find(created.value().id());
    if (!inst_opt) {
        return Result<instance::Instance>::err(Error::make(Error::Code::InstanceNotFound,
            "Created instance vanished from registry"));
    }

    auto pack_res = performance_pack_->install_for(
        inst_opt->get(), version, std::move(progress));
    if (pack_res.is_err()) {
        log->warn("Performance pack install failed: " + pack_res.error().full());
        EventBus::global().publish(ErrorEvent{
            "performance-pack",
            "Could not install Sodium / Lithium / Iris: " + pack_res.error().full()});
    } else {
        auto& summary = pack_res.value();
        std::string installed_list;
        for (auto& s : summary.installed) installed_list += s + ", ";
        log->info("Performance pack ready (Fabric " + summary.fabric_loader_version
                  + "): " + installed_list);
    }

    return created;
}

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
