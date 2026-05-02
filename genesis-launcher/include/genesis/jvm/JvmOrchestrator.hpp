#pragma once

#include "genesis/core/Result.hpp"
#include "genesis/jvm/JvmConfig.hpp"
#include "genesis/jvm/JavaFinder.hpp"
#include "genesis/jvm/ProcessHandle.hpp"
#include "genesis/version/VersionManifest.hpp"
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <cstdint>
#include <optional>

namespace genesis::jvm {

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

    // Non-blocking spawn. The returned ProcessHandle is the authoritative
    // owner of the JVM process: the caller must hold it for the lifetime
    // of the process and use its terminate/kill/wait APIs to control it.
    // Returns immediately with handle.state() == Running and a valid PID.
    core::Result<std::shared_ptr<ProcessHandle>> launch(
        const JvmConfig&   config,
        const std::string& instance_id);

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
