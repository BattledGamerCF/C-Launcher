#pragma once

#include <string>
#include <chrono>
#include <cstdint>

namespace genesis::auth {

using Clock     = std::chrono::system_clock;
using TimePoint = std::chrono::time_point<Clock>;

struct MsaToken {
    std::string access_token;
    std::string refresh_token;
    std::string id_token;
    TimePoint   expires_at;
    std::string scope;

    [[nodiscard]] bool is_expired() const {
        return Clock::now() >= expires_at;
    }
    [[nodiscard]] bool expires_soon(std::chrono::seconds margin = std::chrono::seconds(300)) const {
        return Clock::now() >= (expires_at - margin);
    }
};

struct XblToken {
    std::string token;
    std::string user_hash;
    TimePoint   expires_at;

    [[nodiscard]] bool is_expired() const { return Clock::now() >= expires_at; }
};

struct XstsToken {
    std::string token;
    std::string user_hash;
    TimePoint   expires_at;

    [[nodiscard]] bool is_expired() const { return Clock::now() >= expires_at; }
};

struct MinecraftToken {
    std::string access_token;
    TimePoint   expires_at;

    [[nodiscard]] bool is_expired() const { return Clock::now() >= expires_at; }
};

struct MinecraftProfile {
    std::string uuid;
    std::string username;
    bool        owns_game = false;
};

struct AuthCredential {
    MsaToken       msa;
    MinecraftToken minecraft;
    MinecraftProfile profile;

    [[nodiscard]] bool fully_valid() const {
        return !minecraft.access_token.empty()
            && !minecraft.is_expired()
            && !profile.uuid.empty()
            && profile.owns_game;
    }
};

}
