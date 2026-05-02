#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace genesis::jvm {

enum class GcStrategy {
    G1GC,
    ZGC,
    ShenandoahGC,
    SerialGC,
    ParallelGC,
    Default,
};

struct MemorySpec {
    int32_t min_mb;
    int32_t max_mb;

    static MemorySpec default_for_system();
    static MemorySpec from_gb(float gb) { return {512, static_cast<int32_t>(gb * 1024)}; }
};

struct JvmConfig {
    std::string              java_executable;
    MemorySpec               memory;
    GcStrategy               gc_strategy    = GcStrategy::G1GC;
    std::vector<std::string> extra_jvm_args;
    std::vector<std::string> game_args;
    std::string              main_class;
    std::string              classpath;
    std::string              natives_path;
    std::string              game_dir;
    std::string              assets_dir;
    std::string              asset_index_id;
    std::string              username;
    std::string              uuid;
    std::string              access_token;
    std::string              version_id;
    std::string              version_type;
    int32_t                  window_width   = 854;
    int32_t                  window_height  = 480;

    [[nodiscard]] std::vector<std::string> build_argv() const;
    [[nodiscard]] bool                     is_valid(std::string& error_out) const;
};

struct JvmProfile {
    std::string              id;
    std::string              display_name;
    MemorySpec               memory;
    GcStrategy               gc_strategy = GcStrategy::G1GC;
    std::vector<std::string> extra_jvm_args;
    bool                     is_default  = false;
};

}
