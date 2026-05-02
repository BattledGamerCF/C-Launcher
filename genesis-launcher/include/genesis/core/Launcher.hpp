#pragma once

#include "genesis/core/State.hpp"
#include "genesis/core/EventBus.hpp"
#include "genesis/core/Result.hpp"
#include "genesis/auth/AuthManager.hpp"
#include "genesis/version/VersionManager.hpp"
#include "genesis/assets/AssetManager.hpp"
#include "genesis/jvm/JvmOrchestrator.hpp"
#include "genesis/instance/InstanceManager.hpp"
#include "genesis/update/Updater.hpp"
#include "genesis/logging/Logger.hpp"

#include <memory>
#include <string>
#include <functional>

namespace genesis::core {

struct LaunchRequest {
    std::string instance_id;
    std::string version_id;
    std::string jvm_profile_override;
};

struct LaunchReport {
    int32_t     exit_code;
    std::string instance_id;
    std::string version_id;
    uint64_t    play_time_seconds;
};

class Launcher {
public:
    Launcher();
    ~Launcher();

    Result<void> initialize(const std::string& config_path);
    Result<void> shutdown();

    Result<LaunchReport>  launch(LaunchRequest req);

    [[nodiscard]] LauncherState           state()            const;
    [[nodiscard]] auth::AuthManager&      auth_manager()     const;
    [[nodiscard]] version::VersionManager& version_manager() const;
    [[nodiscard]] assets::AssetManager&   asset_manager()    const;
    [[nodiscard]] jvm::JvmOrchestrator&   jvm_orchestrator() const;
    [[nodiscard]] instance::InstanceManager& instance_manager() const;
    [[nodiscard]] update::Updater&        updater()          const;

    void on_state_change(StateObserver observer);

private:
    Result<void> load_config(const std::string& path);
    Result<void> prepare_directories();
    Result<void> run_update_check();

    std::unique_ptr<StateMachine>              state_machine_;
    std::unique_ptr<auth::AuthManager>         auth_manager_;
    std::unique_ptr<version::VersionManager>   version_manager_;
    std::unique_ptr<assets::AssetManager>      asset_manager_;
    std::unique_ptr<jvm::JvmOrchestrator>      jvm_orchestrator_;
    std::unique_ptr<instance::InstanceManager> instance_manager_;
    std::unique_ptr<update::Updater>           updater_;

    std::string root_dir_;
    bool        initialized_ = false;
};

}
