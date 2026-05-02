#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <any>
#include <typeindex>

namespace genesis::core {

class EventBus {
public:
    using HandlerId = uint64_t;

    template <typename EventT>
    HandlerId subscribe(std::function<void(const EventT&)> handler) {
        std::lock_guard lock(mutex_);
        auto id = next_id_++;
        handlers_[std::type_index(typeid(EventT))].push_back({id, [h = std::move(handler)](const std::any& e) {
            h(std::any_cast<const EventT&>(e));
        }});
        return id;
    }

    template <typename EventT>
    void publish(EventT event) {
        std::vector<std::function<void(const std::any&)>> snapshot;
        {
            std::lock_guard lock(mutex_);
            auto it = handlers_.find(std::type_index(typeid(EventT)));
            if (it == handlers_.end()) return;
            for (auto& [id, fn] : it->second)
                snapshot.push_back(fn);
        }
        std::any wrapped = std::move(event);
        for (auto& fn : snapshot) fn(wrapped);
    }

    void unsubscribe(HandlerId id);

    static EventBus& global();

private:
    struct Entry {
        HandlerId                          id;
        std::function<void(const std::any&)> fn;
    };

    std::mutex                                                     mutex_;
    std::unordered_map<std::type_index, std::vector<Entry>>        handlers_;
    HandlerId                                                      next_id_ = 1;
};

struct DownloadProgressEvent {
    std::string name;
    int64_t     downloaded;
    int64_t     total;
    float       fraction() const { return total > 0 ? float(downloaded) / float(total) : 0.f; }
};

struct LaunchReadyEvent {
    std::string instance_id;
};

struct ErrorEvent {
    std::string context;
    std::string message;
};

struct AuthStatusEvent {
    bool        authenticated;
    std::string username;
};

struct UpdateAvailableEvent {
    std::string current_version;
    std::string new_version;
    std::string download_url;
};

}
