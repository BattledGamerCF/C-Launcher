#pragma once

#include "genesis/core/Result.hpp"
#include "genesis/instance/Instance.hpp"
#include <string>
#include <vector>
#include <memory>
#include <optional>

namespace genesis::instance {

class InstanceManager {
public:
    explicit InstanceManager(std::string instances_root);

    core::Result<void>    load_all();
    core::Result<Instance> create(InstanceConfig config);
    core::Result<void>    update(const std::string& id, InstanceConfig updated_config);
    core::Result<void>    remove(const std::string& id);
    core::Result<void>    duplicate(const std::string& id, const std::string& new_name);
    core::Result<void>    import_portable(const std::string& zip_path);
    core::Result<void>    export_portable(const std::string& id, const std::string& zip_path);

    [[nodiscard]] std::optional<std::reference_wrapper<Instance>> find(const std::string& id);
    [[nodiscard]] const std::vector<std::unique_ptr<Instance>>& all() const;
    [[nodiscard]] bool exists(const std::string& id) const;

private:
    core::Result<void>    scaffold_instance_dir(const std::string& instance_root);
    core::Result<void>    persist(const Instance& inst);

    std::string                          instances_root_;
    std::vector<std::unique_ptr<Instance>> instances_;
};

}
