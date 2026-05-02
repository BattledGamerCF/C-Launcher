#include "genesis/platform/PlatformUtils.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include <thread>

#ifdef GENESIS_PLATFORM_WINDOWS
#   include <windows.h>
#   include <shlobj.h>
#elif defined(GENESIS_PLATFORM_MACOS)
#   include <mach-o/dyld.h>
#   include <sys/sysctl.h>
#   include <unistd.h>
#   include <sys/wait.h>
#   include <signal.h>
#   include <errno.h>
#elif defined(GENESIS_PLATFORM_LINUX)
#   include <unistd.h>
#   include <sys/sysinfo.h>
#   include <sys/wait.h>
#   include <signal.h>
#   include <errno.h>
#endif

namespace fs = std::filesystem;

namespace genesis::platform {

using Error = core::Error;
template<typename T> using Result = core::Result<T>;

std::string default_game_dir() {
#ifdef GENESIS_PLATFORM_WINDOWS
    char buf[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, buf)))
        return path_join(buf, ".minecraft");
    return "C:\\Users\\Public\\.minecraft";
#elif defined(GENESIS_PLATFORM_MACOS)
    const char* home = getenv("HOME");
    return path_join(home ? home : "/tmp", "Library/Application Support/minecraft");
#else
    const char* home = getenv("HOME");
    return path_join(home ? home : "/tmp", ".minecraft");
#endif
}

std::string user_data_dir(const std::string& app_name) {
#ifdef GENESIS_PLATFORM_WINDOWS
    char buf[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, buf)))
        return path_join(buf, app_name);
    return path_join("C:\\Users\\Public", app_name);
#elif defined(GENESIS_PLATFORM_MACOS)
    const char* home = getenv("HOME");
    return path_join(home ? home : "/tmp", "Library/Application Support/" + app_name);
#else
    const char* xdg = getenv("XDG_DATA_HOME");
    if (xdg) return path_join(xdg, app_name);
    const char* home = getenv("HOME");
    return path_join(home ? home : "/tmp", ".local/share/" + app_name);
#endif
}

std::string temp_dir() {
    return fs::temp_directory_path().string();
}

std::string executable_path() {
#ifdef GENESIS_PLATFORM_WINDOWS
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return buf;
#elif defined(GENESIS_PLATFORM_MACOS)
    char buf[1024]; uint32_t sz = sizeof(buf);
    if (_NSGetExecutablePath(buf, &sz) == 0) return buf;
    return "";
#else
    char buf[1024];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) { buf[len] = 0; return buf; }
    return "";
#endif
}

bool path_exists(const std::string& p)    { return fs::exists(p); }
bool is_directory(const std::string& p)   { return fs::is_directory(p); }
bool is_file(const std::string& p)        { return fs::is_regular_file(p); }

Result<void> create_directories(const std::string& p) {
    std::error_code ec;
    fs::create_directories(p, ec);
    if (ec) return Result<void>::err(Error::make(Error::Code::IoError,
                                                  "create_directories failed: " + p, ec.message()));
    return Result<void>::ok();
}

Result<void> remove_file(const std::string& p) {
    if (!fs::exists(p)) return Result<void>::ok();
    std::error_code ec;
    fs::remove(p, ec);
    if (ec) return Result<void>::err(Error::make(Error::Code::IoError,
                                                  "remove_file failed: " + p, ec.message()));
    return Result<void>::ok();
}

Result<void> remove_directory_recursive(const std::string& p) {
    if (!fs::exists(p)) return Result<void>::ok();
    std::error_code ec;
    fs::remove_all(p, ec);
    if (ec) return Result<void>::err(Error::make(Error::Code::IoError,
                                                  "remove_all failed: " + p, ec.message()));
    return Result<void>::ok();
}

Result<void> copy_file(const std::string& src, const std::string& dst, bool overwrite) {
    std::error_code ec;
    auto opts = overwrite ? fs::copy_options::overwrite_existing : fs::copy_options::none;
    fs::copy(src, dst, opts | fs::copy_options::recursive, ec);
    if (ec) return Result<void>::err(Error::make(Error::Code::IoError,
                                                  "copy failed: " + src + " -> " + dst, ec.message()));
    return Result<void>::ok();
}

Result<void> move_file(const std::string& src, const std::string& dst) {
    std::error_code ec;
    fs::rename(src, dst, ec);
    if (ec) return Result<void>::err(Error::make(Error::Code::IoError,
                                                  "move failed: " + src + " -> " + dst, ec.message()));
    return Result<void>::ok();
}

Result<void> atomic_write_file(const std::string& path, const std::string& content) {
    std::string tmp = path + ".tmp";
    auto res = write_file(tmp, content);
    if (res.is_err()) return res;
    return move_file(tmp, path);
}

std::string path_join(const std::string& a, const std::string& b) {
    return (fs::path(a) / b).string();
}

std::string path_join(std::initializer_list<std::string> parts) {
    fs::path p;
    for (auto& s : parts) p /= s;
    return p.string();
}

std::string path_parent(const std::string& p) {
    return fs::path(p).parent_path().string();
}
std::string path_filename(const std::string& p) { return fs::path(p).filename().string(); }
std::string path_extension(const std::string& p) { return fs::path(p).extension().string(); }
std::string path_stem(const std::string& p) { return fs::path(p).stem().string(); }

Result<std::string> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
        return Result<std::string>::err(Error::make(Error::Code::IoError,
                                                     "Cannot read file: " + path));
    std::ostringstream ss;
    ss << f.rdbuf();
    return Result<std::string>::ok(ss.str());
}

Result<void> write_file(const std::string& path, const std::string& content) {
    create_directories(path_parent(path));
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f.is_open())
        return Result<void>::err(Error::make(Error::Code::IoError,
                                              "Cannot write file: " + path));
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
    return Result<void>::ok();
}

Result<std::vector<std::string>> list_directory(const std::string& path) {
    std::error_code ec;
    std::vector<std::string> names;
    for (auto& entry : fs::directory_iterator(path, ec)) {
        names.push_back(entry.path().filename().string());
    }
    if (ec) return Result<std::vector<std::string>>::err(
        Error::make(Error::Code::IoError, "list_directory failed: " + path, ec.message()));
    return Result<std::vector<std::string>>::ok(std::move(names));
}

int64_t total_memory_mb() {
#ifdef GENESIS_PLATFORM_WINDOWS
    MEMORYSTATUSEX ms; ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);
    return static_cast<int64_t>(ms.ullTotalPhys / (1024*1024));
#elif defined(GENESIS_PLATFORM_MACOS)
    int64_t mem = 0; size_t len = sizeof(mem);
    sysctlbyname("hw.memsize", &mem, &len, nullptr, 0);
    return mem / (1024*1024);
#else
    struct sysinfo info{}; sysinfo(&info);
    return static_cast<int64_t>(info.totalram / (1024*1024));
#endif
}

int32_t cpu_core_count() {
    return static_cast<int32_t>(std::thread::hardware_concurrency());
}

std::string os_name() {
#ifdef GENESIS_PLATFORM_WINDOWS
    return "windows";
#elif defined(GENESIS_PLATFORM_MACOS)
    return "osx";
#else
    return "linux";
#endif
}

std::string os_arch() {
#if defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#else
    return "x86";
#endif
}

std::string os_version() { return ""; }

Result<ProcessResult> run_process(const std::string& exe,
                                   const std::vector<std::string>& args,
                                   const std::string& working_dir,
                                   int timeout_ms)
{
#ifdef GENESIS_PLATFORM_WINDOWS
    std::string cmd = "\"" + exe + "\"";
    for (auto& a : args) cmd += " \"" + a + "\"";

    STARTUPINFOA si{}; si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    PROCESS_INFORMATION pi{};

    if (!CreateProcessA(nullptr, const_cast<char*>(cmd.c_str()),
                        nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr,
                        working_dir.empty() ? nullptr : working_dir.c_str(),
                        &si, &pi))
    {
        return Result<ProcessResult>::err(Error::make(Error::Code::ProcessError,
            "CreateProcess failed for: " + exe));
    }

    DWORD wait_ms = (timeout_ms < 0) ? INFINITE : static_cast<DWORD>(timeout_ms);
    WaitForSingleObject(pi.hProcess, wait_ms);

    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return Result<ProcessResult>::ok(ProcessResult{static_cast<int>(exit_code), {}, {}});

#else
    pid_t pid = fork();
    if (pid == -1)
        return Result<ProcessResult>::err(Error::make(Error::Code::ProcessError, "fork() failed"));

    if (pid == 0) {
        if (!working_dir.empty()) chdir(working_dir.c_str());
        std::vector<char*> argv;
        std::string exe_copy = exe;
        argv.push_back(exe_copy.data());
        std::vector<std::string> args_copy = args;
        for (auto& a : args_copy) argv.push_back(a.data());
        argv.push_back(nullptr);
        execvp(exe.c_str(), argv.data());
        _exit(127);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return Result<ProcessResult>::ok(ProcessResult{exit_code, {}, {}});
#endif
}

// ─── Non-blocking spawn ──────────────────────────────────────────────────────
Result<SpawnedProcess> spawn_process(const std::string& exe,
                                      const std::vector<std::string>& args,
                                      const std::string& working_dir)
{
#ifdef GENESIS_PLATFORM_WINDOWS
    std::string cmd = "\"" + exe + "\"";
    for (auto& a : args) cmd += " \"" + a + "\"";

    STARTUPINFOA si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    if (!CreateProcessA(nullptr, const_cast<char*>(cmd.c_str()),
                        nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr,
                        working_dir.empty() ? nullptr : working_dir.c_str(),
                        &si, &pi))
    {
        return Result<SpawnedProcess>::err(Error::make(Error::Code::ProcessError,
            "CreateProcess failed for: " + exe,
            std::to_string(::GetLastError())));
    }

    CloseHandle(pi.hThread);
    SpawnedProcess sp;
    sp.pid       = static_cast<int64_t>(pi.dwProcessId);
    sp.os_handle = reinterpret_cast<void*>(pi.hProcess);
    return Result<SpawnedProcess>::ok(sp);

#else
    pid_t pid = fork();
    if (pid == -1)
        return Result<SpawnedProcess>::err(Error::make(Error::Code::ProcessError,
            "fork() failed", std::to_string(errno)));

    if (pid == 0) {
        if (!working_dir.empty()) (void)!chdir(working_dir.c_str());
        std::vector<char*> argv;
        std::string exe_copy = exe;
        argv.push_back(exe_copy.data());
        std::vector<std::string> args_copy = args;
        for (auto& a : args_copy) argv.push_back(a.data());
        argv.push_back(nullptr);
        execvp(exe.c_str(), argv.data());
        _exit(127);
    }

    SpawnedProcess sp;
    sp.pid       = static_cast<int64_t>(pid);
    sp.os_handle = nullptr;
    return Result<SpawnedProcess>::ok(sp);
#endif
}

void open_url_in_browser(const std::string& url) {
#ifdef GENESIS_PLATFORM_WINDOWS
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(GENESIS_PLATFORM_MACOS)
    system(("open \"" + url + "\" &").c_str());
#else
    system(("xdg-open \"" + url + "\" &").c_str());
#endif
}

void open_in_file_manager(const std::string& path) {
#ifdef GENESIS_PLATFORM_WINDOWS
    ShellExecuteA(nullptr, "explore", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(GENESIS_PLATFORM_MACOS)
    system(("open \"" + path + "\"").c_str());
#else
    system(("xdg-open \"" + path + "\"").c_str());
#endif
}

void copy_to_clipboard(const std::string& text) {
#ifdef GENESIS_PLATFORM_WINDOWS
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    if (!h) { CloseClipboard(); return; }
    char* p = static_cast<char*>(GlobalLock(h));
    memcpy(p, text.c_str(), text.size() + 1);
    GlobalUnlock(h);
    SetClipboardData(CF_TEXT, h);
    CloseClipboard();
#elif defined(GENESIS_PLATFORM_MACOS)
    system(("echo -n \"" + text + "\" | pbcopy").c_str());
#else
    system(("echo -n \"" + text + "\" | xclip -selection clipboard 2>/dev/null || "
            "echo -n \"" + text + "\" | xsel --clipboard --input 2>/dev/null").c_str());
#endif
}

}
