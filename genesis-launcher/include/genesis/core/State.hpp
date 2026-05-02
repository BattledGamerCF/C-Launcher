#pragma once

#include <string>
#include <functional>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace genesis::core {

enum class LauncherState {
    Uninitialized,
    Initializing,
    Idle,
    Authenticating,
    Authenticated,
    ResolvingVersion,
    DownloadingAssets,
    PreparingLaunch,
    Launching,
    Running,
    Stopping,
    UpdatingSelf,
    Error,
    Shutdown,
};

std::string to_string(LauncherState s);

struct StateTransition {
    LauncherState from;
    LauncherState to;
};

using StateObserver = std::function<void(StateTransition)>;

class StateMachine {
public:
    StateMachine();

    [[nodiscard]] LauncherState current() const;

    bool can_transition_to(LauncherState target) const;
    bool transition_to(LauncherState target);

    void on_transition(StateObserver observer);

private:
    void register_allowed_transitions();
    void notify(StateTransition t);

    mutable std::mutex                                           mutex_;
    LauncherState                                               current_ = LauncherState::Uninitialized;
    std::unordered_map<LauncherState, std::vector<LauncherState>> allowed_;
    std::vector<StateObserver>                                   observers_;
};

}
