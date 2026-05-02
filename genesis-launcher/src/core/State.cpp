#include "genesis/core/State.hpp"

namespace genesis::core {

std::string to_string(LauncherState s) {
    switch (s) {
        case LauncherState::Uninitialized:    return "Uninitialized";
        case LauncherState::Initializing:     return "Initializing";
        case LauncherState::Idle:             return "Idle";
        case LauncherState::Authenticating:   return "Authenticating";
        case LauncherState::Authenticated:    return "Authenticated";
        case LauncherState::ResolvingVersion: return "ResolvingVersion";
        case LauncherState::DownloadingAssets:return "DownloadingAssets";
        case LauncherState::PreparingLaunch:  return "PreparingLaunch";
        case LauncherState::Launching:        return "Launching";
        case LauncherState::Running:          return "Running";
        case LauncherState::Stopping:         return "Stopping";
        case LauncherState::UpdatingSelf:     return "UpdatingSelf";
        case LauncherState::Error:            return "Error";
        case LauncherState::Shutdown:         return "Shutdown";
    }
    return "Unknown";
}

StateMachine::StateMachine() {
    register_allowed_transitions();
}

LauncherState StateMachine::current() const {
    std::lock_guard lock(mutex_);
    return current_;
}

bool StateMachine::can_transition_to(LauncherState target) const {
    std::lock_guard lock(mutex_);
    auto it = allowed_.find(current_);
    if (it == allowed_.end()) return false;
    for (auto& s : it->second)
        if (s == target) return true;
    return false;
}

bool StateMachine::transition_to(LauncherState target) {
    StateTransition t;
    {
        std::lock_guard lock(mutex_);
        auto it = allowed_.find(current_);
        if (it == allowed_.end()) return false;
        bool allowed = false;
        for (auto& s : it->second)
            if (s == target) { allowed = true; break; }
        if (!allowed) return false;
        t = {current_, target};
        current_ = target;
    }
    notify(t);
    return true;
}

void StateMachine::on_transition(StateObserver observer) {
    std::lock_guard lock(mutex_);
    observers_.push_back(std::move(observer));
}

void StateMachine::notify(StateTransition t) {
    std::vector<StateObserver> snap;
    {
        std::lock_guard lock(mutex_);
        snap = observers_;
    }
    for (auto& obs : snap) obs(t);
}

void StateMachine::register_allowed_transitions() {
    using S = LauncherState;
    allowed_ = {
        {S::Uninitialized,     {S::Initializing}},
        {S::Initializing,      {S::Idle, S::Error}},
        {S::Idle,              {S::Authenticating, S::ResolvingVersion, S::UpdatingSelf, S::Shutdown}},
        {S::Authenticating,    {S::Authenticated, S::Idle, S::Error}},
        {S::Authenticated,     {S::ResolvingVersion, S::Idle, S::Shutdown}},
        {S::ResolvingVersion,  {S::DownloadingAssets, S::PreparingLaunch, S::Error, S::Idle}},
        {S::DownloadingAssets, {S::PreparingLaunch, S::Error, S::Idle}},
        {S::PreparingLaunch,   {S::Launching, S::Error, S::Idle}},
        {S::Launching,         {S::Running, S::Error, S::Idle}},
        {S::Running,           {S::Stopping, S::Idle}},
        {S::Stopping,          {S::Idle, S::Error}},
        {S::UpdatingSelf,      {S::Idle, S::Error, S::Shutdown}},
        {S::Error,             {S::Idle, S::Shutdown}},
        {S::Shutdown,          {}},
    };
}

}
