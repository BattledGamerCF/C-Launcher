#include "genesis/instance/InstanceManager.hpp"
#include "genesis/platform/PlatformUtils.hpp"
#include "genesis/logging/Logger.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>

namespace genesis::instance {

using json  = nlohmann::json;
using Error = core::Error;
template<typename T> using Result = core::Result<T>;

static auto log = logging::get_logger("InstanceManager");

InstanceManager::InstanceManager(std::string root)
    : instances_root_(std::move(root)) {}

Result<void> InstanceManager::load_all() {
    instances_.clear();
    platform::create_directories(instances_root_);

    auto entries_res = platform::list_directory(instances_root_);
    if (entries_res.is_err()) {
        log->warn("Could not list instances dir: " + entries_res.error().full());
        return Result<void>::ok();
    }

    for (auto& name : entries_res.value()) {
        auto inst_dir    = platform::path_join(instances_root_, name);
        auto config_path = platform::path_join(inst_dir, "genesis_instance.json");
        if (!platform::is_file(config_path)) continue;

        auto text = platform::read_file(config_path);
        if (text.is_err()) continue;

        try {
            auto j = json::parse(text.value());
            InstanceConfig cfg;
            cfg.id             = j.value("id", name);
            cfg.display_name   = j.value("display_name", name);
            cfg.game_version   = j.value("game_version", "");
            cfg.description    = j.value("description", "");
            cfg.jvm_profile_id = j.value("jvm_profile", "default");
            cfg.is_portable    = j.value("is_portable", false);
            cfg.total_play_seconds = j.value("play_seconds", uint64_t{0});

            auto tp = [](int64_t s) {
                return std::chrono::system_clock::time_point{std::chrono::seconds{s}};
            };
            cfg.created_at  = tp(j.value("created_at", int64_t{0}));
            cfg.last_played = tp(j.value("last_played", int64_t{0}));

            instances_.push_back(std::make_unique<Instance>(inst_dir, std::move(cfg)));
        } catch (const json::exception& e) {
            log->warn("Could not parse instance config " + config_path + ": " + e.what());
        }
    }

    log->info("Loaded " + std::to_string(instances_.size()) + " instances");
    return Result<void>::ok();
}

Result<Instance> InstanceManager::create(InstanceConfig config) {
    if (config.id.empty())
        return Result<Instance>::err(Error::make(Error::Code::InvalidConfig, "Instance id cannot be empty"));

    if (exists(config.id))
        return Result<Instance>::err(Error::make(Error::Code::InstanceAlreadyExists,
                                                  "Instance already exists: " + config.id));

    auto root = platform::path_join(instances_root_, config.id);
    auto scaffold_res = scaffold_instance_dir(root);
    if (scaffold_res.is_err()) return Result<Instance>::err(scaffold_res.error());

    config.created_at  = std::chrono::system_clock::now();
    config.last_played = config.created_at;

    Instance inst(root, config);
    inst.save();

    auto& ref = *instances_.emplace_back(std::make_unique<Instance>(root, config));
    log->info("Created instance: " + config.id);
    return Result<Instance>::ok(Instance(root, config));
}

Result<void> InstanceManager::remove(const std::string& id) {
    auto it = std::find_if(instances_.begin(), instances_.end(),
                           [&](const auto& i) { return i->id() == id; });
    if (it == instances_.end())
        return Result<void>::err(Error::make(Error::Code::InstanceNotFound, "Instance not found: " + id));

    auto root = (*it)->root_dir();
    instances_.erase(it);

    platform::remove_directory_recursive(root);
    log->info("Removed instance: " + id);
    return Result<void>::ok();
}

Result<void> InstanceManager::update(const std::string& id, InstanceConfig cfg) {
    auto it = std::find_if(instances_.begin(), instances_.end(),
                           [&](const auto& i) { return i->id() == id; });
    if (it == instances_.end())
        return Result<void>::err(Error::make(Error::Code::InstanceNotFound, "Instance not found: " + id));

    auto root = (*it)->root_dir();
    *it       = std::make_unique<Instance>(root, std::move(cfg));
    (*it)->save();
    return Result<void>::ok();
}

Result<void> InstanceManager::duplicate(const std::string& id, const std::string& new_name) {
    auto it = std::find_if(instances_.begin(), instances_.end(),
                           [&](const auto& i) { return i->id() == id; });
    if (it == instances_.end())
        return Result<void>::err(Error::make(Error::Code::InstanceNotFound, "Instance not found: " + id));

    auto new_id = new_name;
    if (exists(new_id))
        return Result<void>::err(Error::make(Error::Code::InstanceAlreadyExists, "Already exists: " + new_id));

    auto src_root  = (*it)->root_dir();
    auto dest_root = platform::path_join(instances_root_, new_id);
    auto copy_res  = platform::copy_file(src_root, dest_root, false);
    if (copy_res.is_err()) return copy_res;

    InstanceConfig cfg = (*it)->config();
    cfg.id             = new_id;
    cfg.display_name   = new_name;
    cfg.created_at     = std::chrono::system_clock::now();
    instances_.push_back(std::make_unique<Instance>(dest_root, std::move(cfg)));
    instances_.back()->save();
    return Result<void>::ok();
}

std::optional<std::reference_wrapper<Instance>> InstanceManager::find(const std::string& id) {
    for (auto& inst : instances_)
        if (inst->id() == id) return std::ref(*inst);
    return std::nullopt;
}

const std::vector<std::unique_ptr<Instance>>& InstanceManager::all() const { return instances_; }
bool InstanceManager::exists(const std::string& id) const {
    for (auto& i : instances_) if (i->id() == id) return true;
    return false;
}

Result<void> InstanceManager::scaffold_instance_dir(const std::string& root) {
    for (auto& sub : {"saves", "mods", "resourcepacks", "config", "screenshots", "logs"}) {
        auto res = platform::create_directories(platform::path_join(root, sub));
        if (res.is_err()) return res;
    }
    return Result<void>::ok();
}

Result<void> InstanceManager::persist(const Instance& inst) {
    inst.save();
    return Result<void>::ok();
}

Result<void> InstanceManager::import_portable(const std::string&) {
    return Result<void>::err(Error::make(Error::Code::Unknown, "Import not yet implemented"));
}

Result<void> InstanceManager::export_portable(const std::string&, const std::string&) {
    return Result<void>::err(Error::make(Error::Code::Unknown, "Export not yet implemented"));
}

}
