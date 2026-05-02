#include "genesis/auth/SecureStorage.hpp"
#include "genesis/platform/PlatformUtils.hpp"
#include "genesis/logging/Logger.hpp"
#include <nlohmann/json.hpp>

namespace genesis::auth {

using json  = nlohmann::json;
using Error = core::Error;
template<typename T> using Result = core::Result<T>;

// ─── File-fallback storage (used if OS secure storage is unavailable) ─────────
// NOTE: On production, use platform-specific implementations below.
// This fallback stores credentials in a JSON file with restricted permissions.

class FileFallbackStorage : public ISecureStorage {
public:
    explicit FileFallbackStorage(std::string service_name)
        : path_(platform::path_join(
            platform::user_data_dir(service_name),
            ".secure_store.json"))
    {
        platform::create_directories(platform::path_parent(path_));
        load_store();
    }

    Result<void> store(const std::string& key, const std::string& value) override {
        store_[key] = value;
        return flush();
    }

    Result<std::string> load(const std::string& key) override {
        auto it = store_.find(key);
        if (it == store_.end())
            return Result<std::string>::err(Error::make(Error::Code::IoError,
                                                         "Key not found: " + key));
        return Result<std::string>::ok(it.value().get<std::string>());
    }

    Result<void> remove(const std::string& key) override {
        store_.erase(key);
        return flush();
    }

    bool has(const std::string& key) override {
        return store_.contains(key);
    }

private:
    void load_store() {
        auto text = platform::read_file(path_);
        if (text.is_err()) { store_ = json::object(); return; }
        try { store_ = json::parse(text.value()); }
        catch (...) { store_ = json::object(); }
    }

    Result<void> flush() {
        return platform::atomic_write_file(path_, store_.dump(2));
    }

    std::string path_;
    json        store_ = json::object();
};

}

// ─── Platform-specific implementations ────────────────────────────────────────

#ifdef GENESIS_PLATFORM_WINDOWS
#include <windows.h>
#include <wincrypt.h>
#pragma comment(lib, "crypt32")

namespace genesis::auth {

class WindowsSecureStorage : public ISecureStorage {
public:
    explicit WindowsSecureStorage(std::string service_name)
        : service_(std::move(service_name))
        , fallback_(service_)
    {}

    Result<void> store(const std::string& key, const std::string& value) override {
        std::string target = service_ + ":" + key;
        CREDENTIALA cred{};
        cred.Type                 = CRED_TYPE_GENERIC;
        cred.TargetName           = const_cast<char*>(target.c_str());
        cred.CredentialBlobSize   = static_cast<DWORD>(value.size());
        cred.CredentialBlob       = reinterpret_cast<LPBYTE>(const_cast<char*>(value.data()));
        cred.Persist              = CRED_PERSIST_LOCAL_MACHINE;

        if (!CredWriteA(&cred, 0))
            return fallback_.store(key, value);
        return Result<void>::ok();
    }

    Result<std::string> load(const std::string& key) override {
        std::string target = service_ + ":" + key;
        PCREDENTIALA cred = nullptr;
        if (!CredReadA(target.c_str(), CRED_TYPE_GENERIC, 0, &cred))
            return fallback_.load(key);

        std::string value(reinterpret_cast<char*>(cred->CredentialBlob),
                          cred->CredentialBlobSize);
        CredFree(cred);
        return Result<std::string>::ok(std::move(value));
    }

    Result<void> remove(const std::string& key) override {
        std::string target = service_ + ":" + key;
        CredDeleteA(target.c_str(), CRED_TYPE_GENERIC, 0);
        fallback_.remove(key);
        return Result<void>::ok();
    }

    bool has(const std::string& key) override {
        std::string target = service_ + ":" + key;
        PCREDENTIALA cred = nullptr;
        if (CredReadA(target.c_str(), CRED_TYPE_GENERIC, 0, &cred)) {
            CredFree(cred); return true;
        }
        return fallback_.has(key);
    }

private:
    std::string         service_;
    FileFallbackStorage fallback_;
};

std::unique_ptr<ISecureStorage> create_secure_storage(const std::string& service_name) {
    return std::make_unique<WindowsSecureStorage>(service_name);
}

} // namespace genesis::auth

#elif defined(GENESIS_PLATFORM_MACOS)
#include <Security/Security.h>

namespace genesis::auth {

class MacKeychain : public ISecureStorage {
public:
    explicit MacKeychain(std::string service) : service_(std::move(service)), fallback_(service_) {}

    Result<void> store(const std::string& key, const std::string& value) override {
        SecKeychainItemRef item = nullptr;
        OSStatus status = SecKeychainFindGenericPassword(
            nullptr,
            static_cast<UInt32>(service_.size()), service_.c_str(),
            static_cast<UInt32>(key.size()),      key.c_str(),
            nullptr, nullptr, &item);

        if (status == errSecSuccess && item) {
            status = SecKeychainItemModifyAttributesAndData(
                item, nullptr,
                static_cast<UInt32>(value.size()),
                value.data());
            CFRelease(item);
        } else {
            status = SecKeychainAddGenericPassword(
                nullptr,
                static_cast<UInt32>(service_.size()), service_.c_str(),
                static_cast<UInt32>(key.size()),      key.c_str(),
                static_cast<UInt32>(value.size()),    value.data(),
                nullptr);
        }

        if (status != errSecSuccess)
            return fallback_.store(key, value);
        return Result<void>::ok();
    }

    Result<std::string> load(const std::string& key) override {
        void*  data     = nullptr;
        UInt32 data_len = 0;
        OSStatus status = SecKeychainFindGenericPassword(
            nullptr,
            static_cast<UInt32>(service_.size()), service_.c_str(),
            static_cast<UInt32>(key.size()),      key.c_str(),
            &data_len, &data, nullptr);

        if (status != errSecSuccess)
            return fallback_.load(key);

        std::string value(static_cast<char*>(data), data_len);
        SecKeychainItemFreeContent(nullptr, data);
        return Result<std::string>::ok(std::move(value));
    }

    Result<void> remove(const std::string& key) override {
        SecKeychainItemRef item = nullptr;
        OSStatus status = SecKeychainFindGenericPassword(
            nullptr,
            static_cast<UInt32>(service_.size()), service_.c_str(),
            static_cast<UInt32>(key.size()),      key.c_str(),
            nullptr, nullptr, &item);

        if (status == errSecSuccess && item) {
            SecKeychainItemDelete(item);
            CFRelease(item);
        }
        fallback_.remove(key);
        return Result<void>::ok();
    }

    bool has(const std::string& key) override {
        OSStatus status = SecKeychainFindGenericPassword(
            nullptr,
            static_cast<UInt32>(service_.size()), service_.c_str(),
            static_cast<UInt32>(key.size()),      key.c_str(),
            nullptr, nullptr, nullptr);
        return status == errSecSuccess || fallback_.has(key);
    }

private:
    std::string         service_;
    FileFallbackStorage fallback_;
};

std::unique_ptr<ISecureStorage> create_secure_storage(const std::string& service_name) {
    return std::make_unique<MacKeychain>(service_name);
}

} // namespace genesis::auth

#else
// Linux: libsecret (D-Bus Secret Service) with file fallback

namespace genesis::auth {

#ifdef GENESIS_HAS_LIBSECRET
#include <libsecret/secret.h>

class LibSecretStorage : public ISecureStorage {
public:
    explicit LibSecretStorage(std::string service)
        : service_(std::move(service)), fallback_(service_) {}

    Result<void> store(const std::string& key, const std::string& value) override {
        GError* err = nullptr;
        auto schema = make_schema();
        secret_password_store_sync(schema.get(), SECRET_COLLECTION_DEFAULT,
                                   (service_ + ":" + key).c_str(),
                                   value.c_str(), nullptr, &err,
                                   "service", service_.c_str(),
                                   "key",     key.c_str(),
                                   nullptr);
        if (err) {
            g_error_free(err);
            return fallback_.store(key, value);
        }
        return Result<void>::ok();
    }

    Result<std::string> load(const std::string& key) override {
        GError* err = nullptr;
        auto schema = make_schema();
        gchar* pass = secret_password_lookup_sync(schema.get(), nullptr, &err,
                                                   "service", service_.c_str(),
                                                   "key",     key.c_str(),
                                                   nullptr);
        if (err || !pass) {
            if (err) g_error_free(err);
            return fallback_.load(key);
        }
        std::string v(pass);
        secret_password_free(pass);
        return Result<std::string>::ok(std::move(v));
    }

    Result<void> remove(const std::string& key) override {
        GError* err = nullptr;
        auto schema = make_schema();
        secret_password_clear_sync(schema.get(), nullptr, &err,
                                   "service", service_.c_str(),
                                   "key",     key.c_str(),
                                   nullptr);
        if (err) g_error_free(err);
        fallback_.remove(key);
        return Result<void>::ok();
    }

    bool has(const std::string& key) override {
        auto res = load(key);
        return res.is_ok();
    }

private:
    std::shared_ptr<SecretSchema> make_schema() {
        static SecretSchema schema = {
            "io.genesis_launcher.credentials",
            SECRET_SCHEMA_NONE,
            {{"service", SECRET_SCHEMA_ATTRIBUTE_STRING},
             {"key",     SECRET_SCHEMA_ATTRIBUTE_STRING},
             {nullptr,   SecretSchemaAttributeType(0)}}
        };
        return std::shared_ptr<SecretSchema>(&schema, [](SecretSchema*){});
    }

    std::string         service_;
    FileFallbackStorage fallback_;
};

std::unique_ptr<ISecureStorage> create_secure_storage(const std::string& service_name) {
    return std::make_unique<LibSecretStorage>(service_name);
}

#else
std::unique_ptr<ISecureStorage> create_secure_storage(const std::string& service_name) {
    return std::make_unique<FileFallbackStorage>(service_name);
}
#endif

} // namespace genesis::auth

#endif
