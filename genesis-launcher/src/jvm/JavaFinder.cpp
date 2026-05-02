#include "genesis/jvm/JavaFinder.hpp"
#include "genesis/platform/PlatformUtils.hpp"
#include "genesis/logging/Logger.hpp"
#include <algorithm>
#include <regex>
#include <sstream>
#include <cstdlib>
#include <filesystem>

namespace genesis::jvm {

namespace fs = std::filesystem;
using Error  = core::Error;
template<typename T> using Result = core::Result<T>;

static auto log = logging::get_logger("JavaFinder");

static int parse_major(const std::string& version_str) {
    std::regex re(R"((\d+)\.(\d+)\.(\d+)|(\d+))");
    std::smatch m;
    if (std::regex_search(version_str, m, re)) {
        int first = std::stoi(m[1].matched ? m[1].str() : m[4].str());
        if (first == 1 && m[2].matched)
            return std::stoi(m[2].str());
        return first;
    }
    return 0;
}

Result<std::string> JavaFinder::detect_version_string(const std::string& java_exe) {
    auto res = platform::run_process(java_exe, {"-version"}, {}, 5000);
    if (res.is_err()) return Result<std::string>::err(res.error());
    std::string output = res.value().stderr_output + res.value().stdout_output;
    std::regex ver_re(R"RX(version "([^"]+)")RX");
    std::smatch m;
    if (std::regex_search(output, m, ver_re))
        return Result<std::string>::ok(m[1].str());
    return Result<std::string>::err(Error::make(Error::Code::JvmNotFound,
                                                 "Could not parse java -version output"));
}

Result<JavaInstall> JavaFinder::probe(const std::string& path) {
    if (!platform::is_file(path))
        return Result<JavaInstall>::err(Error::make(Error::Code::JvmNotFound, "Not a file: " + path));

    auto ver_res = detect_version_string(path);
    if (ver_res.is_err()) return Result<JavaInstall>::err(ver_res.error());

    JavaInstall j;
    j.executable_path = path;
    j.full_version    = ver_res.value();
    j.major_version   = parse_major(j.full_version);
    j.vendor          = "unknown";
    return Result<JavaInstall>::ok(std::move(j));
}

std::vector<std::string> JavaFinder::candidate_paths() {
    std::vector<std::string> candidates;

    const char* java_home = getenv("JAVA_HOME");
    if (java_home) {
#ifdef GENESIS_PLATFORM_WINDOWS
        candidates.push_back(platform::path_join(java_home, "bin\\java.exe"));
#else
        candidates.push_back(platform::path_join(java_home, "bin/java"));
#endif
    }

    const char* path_env = getenv("PATH");
    if (path_env) {
        std::istringstream ss(path_env);
        std::string segment;
#ifdef GENESIS_PLATFORM_WINDOWS
        while (std::getline(ss, segment, ';')) {
            candidates.push_back(platform::path_join(segment, "java.exe"));
        }
#else
        while (std::getline(ss, segment, ':')) {
            candidates.push_back(platform::path_join(segment, "java"));
        }
#endif
    }

#ifdef GENESIS_PLATFORM_WINDOWS
    std::vector<std::string> search_roots = {
        "C:\\Program Files\\Java",
        "C:\\Program Files\\Eclipse Adoptium",
        "C:\\Program Files\\Microsoft",
        "C:\\Program Files\\Amazon Corretto",
    };
    for (auto& root : search_roots) {
        std::error_code ec;
        for (auto& entry : fs::directory_iterator(root, ec)) {
            auto exe = platform::path_join(entry.path().string(), "bin\\java.exe");
            candidates.push_back(exe);
        }
    }
#elif defined(GENESIS_PLATFORM_MACOS)
    std::vector<std::string> mac_roots = {
        "/Library/Java/JavaVirtualMachines",
        "/System/Library/Java/JavaVirtualMachines",
    };
    for (auto& root : mac_roots) {
        std::error_code ec;
        for (auto& jvm : fs::directory_iterator(root, ec)) {
            auto exe = platform::path_join(jvm.path().string(), "Contents/Home/bin/java");
            candidates.push_back(exe);
        }
    }
    candidates.push_back("/usr/bin/java");
#else
    std::vector<std::string> linux_roots = {
        "/usr/lib/jvm",
        "/usr/java",
        "/opt/java",
        "/opt/jdk",
    };
    for (auto& root : linux_roots) {
        std::error_code ec;
        for (auto& jvm : fs::directory_iterator(root, ec)) {
            auto exe = platform::path_join(jvm.path().string(), "bin/java");
            candidates.push_back(exe);
        }
    }
    candidates.push_back("/usr/bin/java");
#endif

    return candidates;
}

Result<std::vector<JavaInstall>> JavaFinder::find_all() {
    std::vector<JavaInstall> found;
    for (auto& candidate : candidate_paths()) {
        if (!platform::is_file(candidate)) continue;
        auto res = probe(candidate);
        if (res.is_ok() && res.value().major_version > 0)
            found.push_back(std::move(res.value()));
    }
    std::sort(found.begin(), found.end(), [](const JavaInstall& a, const JavaInstall& b) {
        return a.major_version > b.major_version;
    });
    return Result<std::vector<JavaInstall>>::ok(std::move(found));
}

Result<JavaInstall> JavaFinder::find_best(int required_major) {
    auto all_res = find_all();
    if (all_res.is_err()) return Result<JavaInstall>::err(all_res.error());

    for (auto& j : all_res.value())
        if (j.meets_requirement(required_major))
            return Result<JavaInstall>::ok(j);

    return Result<JavaInstall>::err(Error::make(Error::Code::JvmNotFound,
        "No Java installation found satisfying major version >= " +
        std::to_string(required_major)));
}

}
