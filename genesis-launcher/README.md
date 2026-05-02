# Genesis Launcher

A production-grade, lightweight C++ Minecraft launcher. Modern C++20 codebase targeting macOS and Windows, with Linux as a secondary target.

## Architecture

```
genesis-launcher/
├── include/genesis/
│   ├── core/          Result<T>, StateMachine, EventBus, Launcher
│   ├── auth/          MicrosoftAuth (OAuth 2.0 device flow), SecureStorage, Token types
│   ├── version/       VersionManager, VersionManifest parser, RuntimeProfile
│   ├── assets/        AssetManager, Downloader (parallel + resumable), SHA-1 Verifier
│   ├── jvm/           JvmOrchestrator, JvmConfig builder, JavaFinder
│   ├── instance/      InstanceManager, Instance (sandboxed dirs)
│   ├── update/        Self-updater (versioned, checksum-validated, atomic replace)
│   ├── logging/       spdlog wrapper — structured, rotating logs, no secret data
│   ├── platform/      Cross-platform abstractions (filesystem, process, memory)
│   └── ui/            Dear ImGui views (MainWindow, AuthView, InstanceView…)
└── src/               Implementations (one .cpp per header)
    └── platform/
        ├── windows/   Credential Manager, CreateProcess
        ├── macos/     Keychain Services, posix_spawn
        └── linux/     libsecret / file-fallback, fork/exec
```

### Design principles

| Principle | Implementation |
|-----------|---------------|
| No raw booleans / exceptions for control flow | `Result<T,E>` everywhere |
| Observable state machine | `StateMachine` with allowed-transition table |
| Loose coupling | `EventBus` for cross-module events |
| No global mutable state | Dependency injection via `Launcher` |
| Secrets never in plaintext | OS secure storage (Keychain / Credential Manager / libsecret) |
| Deterministic launches | SHA-1 verification before every launch |
| Resumable downloads | libcurl `RESUME_FROM_LARGE` per file |
| Parallel downloads | Thread pool via `Downloader::download_batch` |
| Atomic self-update | `MoveFileEx` / `rename` with backup + rollback |

## Dependencies

| Library | Purpose | How it's fetched |
|---------|---------|-----------------|
| [nlohmann/json](https://github.com/nlohmann/json) | JSON parsing | CMake FetchContent |
| [spdlog](https://github.com/gabime/spdlog) | Structured logging | CMake FetchContent |
| [Dear ImGui](https://github.com/ocornut/imgui) | UI | CMake FetchContent |
| libcurl | HTTP / downloads | System package |
| OpenSSL | SHA-1/SHA-256 hashing, TLS | System package |
| GLFW3 (Linux/macOS) | Window + OpenGL context | System package |

**macOS only:** Security.framework, CoreFoundation.framework  
**Windows only:** crypt32, wintrust, advapi32, shell32, ncrypt, winhttp

## Build

### Prerequisites

**macOS**
```bash
brew install cmake curl openssl glfw
```

**Windows** (Developer PowerShell or x64 Native Tools)
```powershell
vcpkg install curl openssl
# or use the bundled vcpkg manifest (vcpkg.json) if present
```

**Linux**
```bash
sudo apt install cmake libcurl4-openssl-dev libssl-dev \
                 libglfw3-dev libsecret-1-dev
```

### Configure & Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The compiled binary will be at `build/Genesis` (or `build\Genesis.exe` on Windows).

### Debug build

```bash
cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug --parallel
```

## Module reference

### `core::Result<T>`
Every operation returns `Result<T>` or `Result<void>`. Never throws.
```cpp
auto res = manager.fetch_version_meta("1.21.4");
if (res.is_err()) {
    log->error(res.error().full());
    return;
}
auto& meta = res.value();
```

### `core::StateMachine`
```
Uninitialized → Initializing → Idle
Idle → Authenticating → Authenticated → ResolvingVersion
     → DownloadingAssets → PreparingLaunch → Launching → Running
     → Stopping → Idle
Idle → UpdatingSelf → Idle / Shutdown
```

### `auth::AuthManager`
1. `login(prompt_fn)` — Opens Microsoft device-code flow; calls `prompt_fn` with code info
2. `ensure_valid_credential()` — Returns cached credential or auto-refreshes MSA token
3. `logout()` — Clears OS secure storage

No raw tokens are ever written to log files. All persistence goes through `ISecureStorage`.

### `assets::AssetManager`
- `ensure_assets()` — Downloads and verifies asset objects from `resources.download.minecraft.net`
- `ensure_libraries()` — Downloads and verifies all libraries + extracts natives
- `verify_all()` — Returns list of corrupted file paths
- `repair_corrupted()` — Re-downloads only broken files

### `jvm::JvmOrchestrator`
- `build_config()` — Constructs `JvmConfig` from version metadata + profile overrides
- `launch()` — Spawns process; callbacks for stdout/stderr/exit
- `JvmConfig::is_valid()` — Validates all fields before launch; rejects bad configs with a clear message

### `instance::InstanceManager`
Each instance is an isolated directory:
```
instances/<id>/
  saves/
  mods/
  resourcepacks/
  config/
  screenshots/
  logs/
  genesis_instance.json
```
Instances are fully portable — no absolute path references baked into saves.

### `update::Updater`
1. `check_for_update()` — Fetches release manifest, compares semver
2. `download_update()` — Downloads + SHA-256 verifies
3. `apply_update()` — Backs up current binary, atomically replaces, cleans backup
4. `rollback()` — Restores previous binary if update produces a broken executable

## Automatic performance pack (1.21.11+)

For every Minecraft version 1.21.11 and newer (the post-Microsoft naming
scheme), Genesis automatically installs a curated Fabric mod pack into newly
created instances:

| Mod | Project | Required? |
|-----|---------|-----------|
| Sodium       | `sodium`        | yes |
| Sodium Extra | `sodium-extra`  | optional |
| Lithium      | `lithium`       | yes |
| Iris Shaders | `iris`          | optional |

How it works:

1. `mods::PerformancePack::qualifies(version)` returns `true` for any version
   `>= 1.21.11` (snapshots / RCs / pre-releases are excluded — they often
   pre-date the matching mod release).
2. `Launcher::create_instance_with_performance_pack()` calls
   `InstanceManager::create()` and then, if eligible, fetches the latest
   compatible Fabric build of each mod from the Modrinth API
   (`/v2/project/<slug>/version?game_versions=[…]&loaders=["fabric"]`).
3. The newest stable Fabric loader for that Minecraft version is resolved via
   `https://meta.fabricmc.net/v2/versions/loader/<mc>` and recorded in the
   install summary. The loader profile JSON is merged into the version meta at
   launch time.
4. Each mod is downloaded with SHA-1 verification and dropped into
   `<instance>/mods/`. Installation is **idempotent** — re-running on an
   existing instance just verifies the present files.
5. If a non-required mod has no compatible release yet (typical right after a
   Minecraft drop), it is silently skipped and noted in the
   `PerformancePackResult::skipped` list rather than failing the install.

Disabling: pass an instance through plain `InstanceManager::create()` instead
of `Launcher::create_instance_with_performance_pack()` — the manager itself
has no knowledge of the mod pack and never installs anything.

## Extending for mod loaders

`RuntimeProfile` carries an optional `ModLoaderSpec`:
```cpp
struct ModLoaderSpec {
    ModLoaderType type;    // Forge, Fabric, Quilt, NeoForge
    std::string   version;
    std::string   extra_args;
};
```

To add Fabric support:
1. After `VersionManager::ensure_version_downloaded`, call a `FabricInstaller` that patches the version JSON
2. Set `profile.mod_loader = ModLoaderSpec{ModLoaderType::Fabric, fabric_version, ""}`
3. The `JvmOrchestrator::build_classpath` already handles arbitrary libraries; Fabric's loader JAR just becomes another library entry

## Logging

All logs go to `<game_dir>/logs/genesis.log` (rotating, max 5 MB × 3 files). Never contains tokens, UUIDs, or access tokens — those paths are never passed to the logger.

## Crash diagnostics

On abnormal exit, the last known launcher state is recoverable from the structured log. A crash bundle can be generated by collecting:
- `logs/genesis.log`
- `cache/version_manifest_v2.json`
- `instances/<id>/genesis_instance.json`

No private credentials appear in any of these files.
