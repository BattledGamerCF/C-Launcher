#pragma once

#include <string>
#include <vector>
#include <optional>

namespace genesis::version {

enum class ModLoaderType { Vanilla, Forge, Fabric, Quilt, NeoForge };
std::string mod_loader_to_string(ModLoaderType t);
ModLoaderType mod_loader_from_string(const std::string& s);

struct ModLoaderSpec {
    ModLoaderType type;
    std::string   version;
    std::string   extra_args;
};

struct RuntimeProfile {
    std::string                  id;
    std::string                  display_name;
    std::string                  game_version;
    std::optional<ModLoaderSpec> mod_loader;
    std::vector<std::string>     extra_jvm_args;
    std::vector<std::string>     extra_game_args;
    std::string                  description;

    [[nodiscard]] bool is_vanilla() const { return !mod_loader.has_value(); }
    [[nodiscard]] std::string effective_id() const;
};

}
