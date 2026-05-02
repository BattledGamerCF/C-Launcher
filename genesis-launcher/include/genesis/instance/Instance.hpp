#pragma once

#include "genesis/version/Profile.hpp"
#include <string>
#include <vector>
#include <optional>
#include <chrono>

namespace genesis::instance {

struct InstanceConfig {
    std::string                           id;
    std::string                           display_name;
    std::string                           icon_path;
    std::string                           game_version;
    std::optional<version::ModLoaderSpec> mod_loader;
    std::string                           jvm_profile_id;
    std::string                           description;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point last_played;
    uint64_t                              total_play_seconds = 0;
    bool                                  is_portable        = false;
    std::vector<std::string>              extra_jvm_args;
    std::vector<std::string>              extra_game_args;
};

class Instance {
public:
    explicit Instance(std::string root_dir, InstanceConfig config);

    [[nodiscard]] const std::string&     id()           const;
    [[nodiscard]] const std::string&     display_name() const;
    [[nodiscard]] const InstanceConfig&  config()       const;
    [[nodiscard]] const std::string&     root_dir()     const;

    [[nodiscard]] std::string saves_dir()         const;
    [[nodiscard]] std::string mods_dir()          const;
    [[nodiscard]] std::string resource_packs_dir() const;
    [[nodiscard]] std::string config_dir()        const;
    [[nodiscard]] std::string screenshots_dir()   const;
    [[nodiscard]] std::string logs_dir()          const;

    void record_play_session(uint64_t seconds);
    void save() const;

private:
    std::string    root_dir_;
    InstanceConfig config_;
};

}
