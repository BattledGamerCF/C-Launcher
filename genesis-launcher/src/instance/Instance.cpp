#include "genesis/instance/Instance.hpp"
#include "genesis/platform/PlatformUtils.hpp"
#include <nlohmann/json.hpp>
#include <fstream>

namespace genesis::instance {

using json = nlohmann::json;

Instance::Instance(std::string root_dir, InstanceConfig config)
    : root_dir_(std::move(root_dir)), config_(std::move(config)) {}

const std::string& Instance::id()           const { return config_.id; }
const std::string& Instance::display_name() const { return config_.display_name; }
const InstanceConfig& Instance::config()    const { return config_; }
const std::string& Instance::root_dir()     const { return root_dir_; }

std::string Instance::saves_dir()          const { return platform::path_join(root_dir_, "saves"); }
std::string Instance::mods_dir()           const { return platform::path_join(root_dir_, "mods"); }
std::string Instance::resource_packs_dir() const { return platform::path_join(root_dir_, "resourcepacks"); }
std::string Instance::config_dir()         const { return platform::path_join(root_dir_, "config"); }
std::string Instance::screenshots_dir()    const { return platform::path_join(root_dir_, "screenshots"); }
std::string Instance::logs_dir()           const { return platform::path_join(root_dir_, "logs"); }

void Instance::record_play_session(uint64_t seconds) {
    config_.total_play_seconds += seconds;
    config_.last_played = std::chrono::system_clock::now();
    save();
}

void Instance::save() const {
    auto config_path = platform::path_join(root_dir_, "genesis_instance.json");
    auto now_secs = [](std::chrono::system_clock::time_point tp) {
        return std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count();
    };
    json j;
    j["id"]           = config_.id;
    j["display_name"] = config_.display_name;
    j["game_version"] = config_.game_version;
    j["description"]  = config_.description;
    j["jvm_profile"]  = config_.jvm_profile_id;
    j["is_portable"]  = config_.is_portable;
    j["play_seconds"] = config_.total_play_seconds;
    j["created_at"]   = now_secs(config_.created_at);
    j["last_played"]  = now_secs(config_.last_played);

    platform::write_file(config_path, j.dump(2));
}

}
