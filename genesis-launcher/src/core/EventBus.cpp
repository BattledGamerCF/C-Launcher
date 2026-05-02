#include "genesis/core/EventBus.hpp"
#include <algorithm>

namespace genesis::core {

void EventBus::unsubscribe(HandlerId id) {
    std::lock_guard lock(mutex_);
    for (auto& [type, entries] : handlers_) {
        auto it = std::remove_if(entries.begin(), entries.end(),
                                 [id](const Entry& e) { return e.id == id; });
        entries.erase(it, entries.end());
    }
}

EventBus& EventBus::global() {
    static EventBus instance;
    return instance;
}

}
