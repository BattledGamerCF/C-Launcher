#pragma once

#include "genesis/core/Result.hpp"
#include <string>
#include <vector>

namespace genesis::jvm {

struct JavaInstall {
    std::string executable_path;
    int         major_version;
    std::string vendor;
    std::string full_version;

    [[nodiscard]] bool meets_requirement(int required_major) const {
        return major_version >= required_major;
    }
};

class JavaFinder {
public:
    static core::Result<std::vector<JavaInstall>> find_all();
    static core::Result<JavaInstall>              find_best(int required_major = 17);
    static core::Result<JavaInstall>              probe(const std::string& path);
    static core::Result<std::string>              detect_version_string(const std::string& java_exe);

private:
    static std::vector<std::string> candidate_paths();
};

}
