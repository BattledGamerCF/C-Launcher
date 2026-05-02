#include "genesis/version/Profile.hpp"

namespace genesis::version {

std::string mod_loader_to_string(ModLoaderType t) {
    switch (t) {
        case ModLoaderType::Vanilla:  return "vanilla";
        case ModLoaderType::Forge:    return "forge";
        case ModLoaderType::Fabric:   return "fabric";
        case ModLoaderType::Quilt:    return "quilt";
        case ModLoaderType::NeoForge: return "neoforge";
    }
    return "vanilla";
}

ModLoaderType mod_loader_from_string(const std::string& s) {
    if (s == "forge")    return ModLoaderType::Forge;
    if (s == "fabric")   return ModLoaderType::Fabric;
    if (s == "quilt")    return ModLoaderType::Quilt;
    if (s == "neoforge") return ModLoaderType::NeoForge;
    return ModLoaderType::Vanilla;
}

std::string RuntimeProfile::effective_id() const {
    if (!mod_loader) return game_version;
    return game_version + "-" + mod_loader_to_string(mod_loader->type) + "-" + mod_loader->version;
}

}
