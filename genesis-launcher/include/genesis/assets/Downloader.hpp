#pragma once

#include "genesis/core/Result.hpp"
#include <string>
#include <functional>
#include <vector>
#include <cstdint>
#include <atomic>

namespace genesis::assets {

using DownloadProgressFn = std::function<void(const std::string& name,
                                               int64_t downloaded,
                                               int64_t total)>;

struct DownloadTask {
    std::string name;
    std::string url;
    std::string dest_path;
    std::string expected_sha1;
    int64_t     expected_size = 0;
    int         max_retries   = 3;
};

struct DownloadResult {
    std::string name;
    bool        success;
    std::string error_message;
};

class Downloader {
public:
    explicit Downloader(int max_parallel = 8);
    ~Downloader();

    core::Result<void> download_one(const DownloadTask& task,
                                    DownloadProgressFn progress_fn = {});

    core::Result<std::vector<DownloadResult>> download_batch(
        const std::vector<DownloadTask>& tasks,
        DownloadProgressFn               progress_fn = {});

    core::Result<std::string> fetch_string(const std::string& url,
                                            const std::vector<std::string>& headers = {});

    core::Result<std::string> post_string(const std::string& url,
                                           const std::string& body,
                                           const std::vector<std::string>& headers = {});

    void cancel();

    [[nodiscard]] bool is_cancelled() const { return cancelled_.load(); }

private:
    core::Result<void> download_with_retry(const DownloadTask& task,
                                           DownloadProgressFn& progress_fn);

    int                max_parallel_;
    std::atomic<bool>  cancelled_{false};
};

}
