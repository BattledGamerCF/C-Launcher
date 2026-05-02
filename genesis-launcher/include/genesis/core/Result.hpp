#pragma once

#include <string>
#include <variant>
#include <functional>
#include <stdexcept>

namespace genesis::core {

struct Error {
    enum class Code {
        Unknown,
        NetworkError,
        ParseError,
        IoError,
        AuthError,
        VersionNotFound,
        HashMismatch,
        JvmNotFound,
        JvmLaunchFailed,
        InstanceNotFound,
        InstanceAlreadyExists,
        PermissionDenied,
        UpdateFailed,
        InvalidConfig,
        ProcessError,
        Timeout,
        Cancelled,
    };

    Code        code   = Code::Unknown;
    std::string message;
    std::string detail;

    static Error make(Code c, std::string msg, std::string det = {}) {
        return Error{c, std::move(msg), std::move(det)};
    }

    [[nodiscard]] std::string full() const {
        if (detail.empty()) return message;
        return message + ": " + detail;
    }
};

template <typename T>
class Result {
public:
    static Result ok(T value) {
        Result r;
        r.storage_ = std::move(value);
        return r;
    }

    static Result err(Error error) {
        Result r;
        r.storage_ = std::move(error);
        return r;
    }

    [[nodiscard]] bool is_ok()  const noexcept { return std::holds_alternative<T>(storage_); }
    [[nodiscard]] bool is_err() const noexcept { return std::holds_alternative<Error>(storage_); }

    [[nodiscard]] const T& value() const & {
        if (is_err()) throw std::logic_error("Result::value() called on error: " + std::get<Error>(storage_).full());
        return std::get<T>(storage_);
    }

    [[nodiscard]] T& value() & {
        if (is_err()) throw std::logic_error("Result::value() called on error: " + std::get<Error>(storage_).full());
        return std::get<T>(storage_);
    }

    [[nodiscard]] T value_or(T fallback) const {
        return is_ok() ? std::get<T>(storage_) : std::move(fallback);
    }

    [[nodiscard]] T&& take() && {
        if (is_err()) throw std::logic_error("Result::take() called on error: " + std::get<Error>(storage_).full());
        return std::move(std::get<T>(storage_));
    }

    [[nodiscard]] const Error& error() const {
        if (is_ok()) throw std::logic_error("Result::error() called on ok value");
        return std::get<Error>(storage_);
    }

    template <typename F>
    auto map(F&& fn) const -> Result<decltype(fn(std::declval<T>()))> {
        using U = decltype(fn(std::declval<T>()));
        if (is_ok()) return Result<U>::ok(fn(std::get<T>(storage_)));
        return Result<U>::err(std::get<Error>(storage_));
    }

    template <typename F>
    Result<T> and_then(F&& fn) const {
        if (is_ok()) return fn(std::get<T>(storage_));
        return *this;
    }

    template <typename F>
    Result<T> or_else(F&& fn) const {
        if (is_err()) return fn(std::get<Error>(storage_));
        return *this;
    }

private:
    std::variant<T, Error> storage_;
    Result() = default;
};

template <>
class Result<void> {
public:
    static Result ok() {
        Result r;
        r.ok_ = true;
        return r;
    }

    static Result err(Error error) {
        Result r;
        r.ok_     = false;
        r.error_  = std::move(error);
        return r;
    }

    [[nodiscard]] bool is_ok()  const noexcept { return ok_; }
    [[nodiscard]] bool is_err() const noexcept { return !ok_; }

    [[nodiscard]] const Error& error() const {
        if (ok_) throw std::logic_error("Result::error() called on ok void");
        return error_;
    }

private:
    bool  ok_    = false;
    Error error_;
};

}
