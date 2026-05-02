#include "genesis/assets/Downloader.hpp"
#include "genesis/assets/Verifier.hpp"
#include "genesis/platform/PlatformUtils.hpp"
#include "genesis/logging/Logger.hpp"
#include <curl/curl.h>
#include <fstream>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
#include <functional>
#include <future>

namespace genesis::assets {

using Error = core::Error;
template<typename T> using Result = core::Result<T>;

static auto log = logging::get_logger("Downloader");

namespace {

size_t write_to_string(void* ptr, size_t size, size_t nmemb, std::string* out) {
    out->append(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

size_t write_to_file(void* ptr, size_t size, size_t nmemb, std::ofstream* out) {
    out->write(static_cast<char*>(ptr), static_cast<std::streamsize>(size * nmemb));
    return size * nmemb;
}

struct ProgressState {
    std::string             name;
    DownloadProgressFn*     fn;
    std::atomic<bool>*      cancelled;
};

int progress_cb(void* client, curl_off_t dl_total, curl_off_t dl_now, curl_off_t, curl_off_t) {
    auto* state = static_cast<ProgressState*>(client);
    if (state->cancelled && state->cancelled->load()) return 1;
    if (state->fn && *state->fn)
        (*state->fn)(state->name, static_cast<int64_t>(dl_now), static_cast<int64_t>(dl_total));
    return 0;
}

} // namespace

Downloader::Downloader(int max_parallel) : max_parallel_(max_parallel) {
    curl_global_init(CURL_GLOBAL_ALL);
}

Downloader::~Downloader() {
    curl_global_cleanup();
}

void Downloader::cancel() {
    cancelled_.store(true);
}

Result<void> Downloader::download_with_retry(const DownloadTask& task, DownloadProgressFn& pfn) {
    for (int attempt = 0; attempt <= task.max_retries; ++attempt) {
        if (cancelled_.load())
            return Result<void>::err(Error::make(Error::Code::Cancelled, "Download cancelled"));

        if (attempt > 0)
            log->info("Retry " + std::to_string(attempt) + " for: " + task.name);

        auto dir = platform::path_parent(task.dest_path);
        platform::create_directories(dir);

        int64_t resume_from = 0;
        if (platform::is_file(task.dest_path)) {
            auto sz = Verifier::file_size(task.dest_path);
            if (sz.is_ok()) resume_from = sz.value();
        }

        std::ofstream outfile;
        if (resume_from > 0)
            outfile.open(task.dest_path, std::ios::binary | std::ios::app);
        else
            outfile.open(task.dest_path, std::ios::binary | std::ios::trunc);

        if (!outfile.is_open())
            return Result<void>::err(Error::make(Error::Code::IoError,
                "Cannot open output file: " + task.dest_path));

        CURL* curl = curl_easy_init();
        if (!curl) continue;

        ProgressState ps{task.name, &pfn, &cancelled_};

        curl_easy_setopt(curl, CURLOPT_URL,              task.url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,    write_to_file);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA,        &outfile);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_cb);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA,     &ps);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS,       0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER,   1L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION,   1L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT,   15L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT,  1L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME,   30L);

        if (resume_from > 0)
            curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, static_cast<curl_off_t>(resume_from));

        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        outfile.close();

        if (cancelled_.load())
            return Result<void>::err(Error::make(Error::Code::Cancelled, "Download cancelled"));

        if (res != CURLE_OK) {
            log->warn("Download failed (" + task.name + "): " + curl_easy_strerror(res));
            platform::remove_file(task.dest_path);
            continue;
        }

        if (!task.expected_sha1.empty()) {
            auto verify = Verifier::verify_file(task.dest_path, task.expected_sha1, HashAlgorithm::SHA1);
            if (verify.is_err()) {
                log->warn("Hash mismatch for " + task.name + "; re-downloading");
                platform::remove_file(task.dest_path);
                continue;
            }
        }

        return Result<void>::ok();
    }

    return Result<void>::err(Error::make(Error::Code::NetworkError,
                                          "Download failed after retries: " + task.name));
}

Result<void> Downloader::download_one(const DownloadTask& task, DownloadProgressFn pfn) {
    return download_with_retry(task, pfn);
}

Result<std::vector<DownloadResult>> Downloader::download_batch(
    const std::vector<DownloadTask>& tasks,
    DownloadProgressFn               pfn)
{
    std::vector<DownloadResult> results;
    results.reserve(tasks.size());
    std::mutex results_mutex;

    std::atomic<size_t> pending{tasks.size()};
    std::vector<std::future<void>> futures;

    auto worker_pool_size = std::min(static_cast<size_t>(max_parallel_), tasks.size());
    std::atomic<size_t> task_index{0};

    auto worker = [&]() {
        while (true) {
            size_t idx = task_index.fetch_add(1);
            if (idx >= tasks.size()) break;
            const auto& task = tasks[idx];

            auto res = download_with_retry(task, pfn);
            DownloadResult dr;
            dr.name    = task.name;
            dr.success = res.is_ok();
            if (res.is_err()) dr.error_message = res.error().full();

            std::lock_guard lock(results_mutex);
            results.push_back(std::move(dr));
        }
    };

    std::vector<std::thread> threads;
    for (size_t i = 0; i < worker_pool_size; ++i)
        threads.emplace_back(worker);
    for (auto& t : threads) t.join();

    return Result<std::vector<DownloadResult>>::ok(std::move(results));
}

Result<std::string> Downloader::fetch_string(const std::string& url,
                                               const std::vector<std::string>& headers)
{
    CURL* curl = curl_easy_init();
    if (!curl)
        return Result<std::string>::err(Error::make(Error::Code::NetworkError, "curl_easy_init failed"));

    std::string response;
    struct curl_slist* hdr = nullptr;
    for (auto& h : headers) hdr = curl_slist_append(hdr, h.c_str());

    curl_easy_setopt(curl, CURLOPT_URL,            url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  write_to_string);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &response);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,     hdr);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        30L);

    CURLcode res = curl_easy_perform(curl);
    if (hdr) curl_slist_free_all(hdr);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
        return Result<std::string>::err(Error::make(Error::Code::NetworkError,
                                                     "GET failed: " + url, curl_easy_strerror(res)));
    return Result<std::string>::ok(std::move(response));
}

Result<std::string> Downloader::post_string(const std::string& url,
                                              const std::string& body,
                                              const std::vector<std::string>& headers)
{
    CURL* curl = curl_easy_init();
    if (!curl)
        return Result<std::string>::err(Error::make(Error::Code::NetworkError, "curl_easy_init failed"));

    std::string response;
    struct curl_slist* hdr = nullptr;
    for (auto& h : headers) hdr = curl_slist_append(hdr, h.c_str());

    curl_easy_setopt(curl, CURLOPT_URL,            url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,     body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  write_to_string);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &response);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,     hdr);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        30L);

    CURLcode res = curl_easy_perform(curl);
    if (hdr) curl_slist_free_all(hdr);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
        return Result<std::string>::err(Error::make(Error::Code::NetworkError,
                                                     "POST failed: " + url, curl_easy_strerror(res)));
    return Result<std::string>::ok(std::move(response));
}

}
