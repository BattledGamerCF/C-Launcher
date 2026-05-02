#pragma once

#include "genesis/core/Result.hpp"
#include "genesis/jvm/JvmConfig.hpp"
#include "genesis/jvm/JavaFinder.hpp"
#include "genesis/version/VersionManifest.hpp"
#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <optional>

namespace genesis::jvm {

using ProcessExitFn = std::function<void(int32_t exit_code)>;
using ProcessOutputFn = std::function<void(const std::string& line)>;

struct ProcessHandle {
    int64_t     pid;
    std::string instance_id;

    [[nodiscard]] bool is_running() const;
    core::Result<void> wait(int timeout_ms = -1);
    core::Result<void> terminate();
    core::Result<void> kill();
};

class JvmOrchestrator {
public:
    explicit JvmOrchestrator(std::string game_dir);

    core::Result<JvmConfig> build_config(
        const version::VersionMeta& meta,
        const std::string&          instance_dir,
        const std::string&          username,
        const std::string&          uuid,
        const std::string&          access_token,
        const std::optional<JvmProfile>& profile_override = {});

    core::Result<ProcessHandle> launch(
        const JvmConfig&   config,
        ProcessExitFn      on_exit   = {},
        ProcessOutputFn    on_stdout = {},
        ProcessOutputFn    on_stderr = {});

    core::Result<void>    save_profile(const JvmProfile& profile);
    core::Result<void>    delete_profile(const std::string& profile_id);
    std::vector<JvmProfile> list_profiles() const;
    std::optional<JvmProfile> default_profile() const;
    JvmProfile               get_or_create_default() const;

private:
    core::Result<JvmConfig> apply_profile_overrides(JvmConfig        base,
                                                     const JvmProfile& profile) const;
    core::Result<std::string> build_classpath(const version::VersionMeta& meta,
                                               const std::string&          version_dir) const;

    std::string              game_dir_;
    std::vector<JvmProfile>  profiles_;
};

}
