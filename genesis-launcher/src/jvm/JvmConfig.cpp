#include "genesis/jvm/JvmConfig.hpp"
#include "genesis/platform/PlatformUtils.hpp"
#include <sstream>

namespace genesis::jvm {

static std::string gc_flag(GcStrategy gc) {
    switch (gc) {
        case GcStrategy::G1GC:          return "-XX:+UseG1GC";
        case GcStrategy::ZGC:           return "-XX:+UseZGC";
        case GcStrategy::ShenandoahGC:  return "-XX:+UseShenandoahGC";
        case GcStrategy::SerialGC:      return "-XX:+UseSerialGC";
        case GcStrategy::ParallelGC:    return "-XX:+UseParallelGC";
        default:                        return "";
    }
}

MemorySpec MemorySpec::default_for_system() {
    int64_t total = platform::total_memory_mb();
    int32_t max_mb = (total > 8192) ? 4096
                   : (total > 4096) ? 2048
                   : 1024;
    return MemorySpec{512, max_mb};
}

std::vector<std::string> JvmConfig::build_argv() const {
    std::vector<std::string> args;
    args.push_back(java_executable);
    args.push_back("-Xms" + std::to_string(memory.min_mb) + "M");
    args.push_back("-Xmx" + std::to_string(memory.max_mb) + "M");

    std::string gc = gc_flag(gc_strategy);
    if (!gc.empty()) args.push_back(gc);

    if (gc_strategy == GcStrategy::G1GC) {
        args.push_back("-XX:G1NewSizePercent=20");
        args.push_back("-XX:G1ReservePercent=20");
        args.push_back("-XX:MaxGCPauseMillis=50");
        args.push_back("-XX:G1HeapRegionSize=32M");
    }

    args.push_back("-XX:-UseAdaptiveSizePolicy");
    args.push_back("-XX:-OmitStackTraceInFastThrow");
    args.push_back("-Dlog4j2.formatMsgNoLookups=true");
    args.push_back("-Dfml.ignorePatchDiscrepancies=true");
    args.push_back("-Dfml.ignoreInvalidMinecraftCertificates=true");

    if (!natives_path.empty())
        args.push_back("-Djava.library.path=" + natives_path);

    for (auto& a : extra_jvm_args) args.push_back(a);

    args.push_back("-cp");
    args.push_back(classpath);
    args.push_back(main_class);

    args.push_back("--username");   args.push_back(username);
    args.push_back("--version");    args.push_back(version_id);
    args.push_back("--gameDir");    args.push_back(game_dir);
    args.push_back("--assetsDir");  args.push_back(assets_dir);
    args.push_back("--assetIndex"); args.push_back(asset_index_id);
    args.push_back("--uuid");       args.push_back(uuid);
    args.push_back("--accessToken");args.push_back(access_token);
    args.push_back("--userType");   args.push_back("msa");
    args.push_back("--versionType");args.push_back(version_type.empty() ? "Genesis" : version_type);
    args.push_back("--width");      args.push_back(std::to_string(window_width));
    args.push_back("--height");     args.push_back(std::to_string(window_height));

    for (auto& a : game_args) args.push_back(a);
    return args;
}

bool JvmConfig::is_valid(std::string& err) const {
    if (java_executable.empty())   { err = "No Java executable specified"; return false; }
    if (main_class.empty())        { err = "No main class specified";      return false; }
    if (classpath.empty())         { err = "Empty classpath";              return false; }
    if (username.empty())          { err = "No username";                  return false; }
    if (uuid.empty())              { err = "No UUID";                      return false; }
    if (access_token.empty())      { err = "No access token";              return false; }
    if (memory.min_mb <= 0)        { err = "Invalid min memory";           return false; }
    if (memory.max_mb < memory.min_mb) { err = "max memory < min memory"; return false; }
    if (memory.max_mb > 65536)     { err = "Excessive max memory (>64GB)"; return false; }
    return true;
}

}
