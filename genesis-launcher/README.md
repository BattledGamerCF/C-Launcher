# Genesis Launcher

A production-grade, cross-platform Minecraft launcher written in C++20.

- **Native UI** — Dear ImGui + OpenGL on Linux/macOS, Win32 backend on Windows.
- **Real auth** — Microsoft device-code OAuth → Xbox Live → XSTS → Minecraft profile chain.
- **Secure credential storage** — macOS Keychain, Windows Credential Manager (crypt32),
  Linux libsecret (Secret Service / KWallet) with a permission-restricted file fallback.
- **Hardened JVM lifecycle** — single-source-of-truth `ProcessHandle` with a 5-step
  shutdown ordering, `setsid()` process-group leadership, `killpg`-based group teardown,
  and absorbing terminal states. Verified by a runtime harness running 500 cycles
  across 6 invariants.
- **Zero hidden state** — every async operation is observable via the in-app console.

> **Status:** alpha. The launcher core, auth, JVM orchestration, UI shell, and
> packaging are functional. End-to-end Minecraft launch with full asset
> download + verification is gated on per-user Azure AD client ID.

---

## Building

### Linux (Ubuntu / Debian)

```bash
sudo apt-get install -y \
    build-essential cmake ninja-build pkg-config \
    libcurl4-openssl-dev libssl-dev \
    libglfw3-dev libsecret-1-dev \
    libgl1-mesa-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev

cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/Genesis
```

### macOS (11.0+)

```bash
brew install cmake ninja glfw curl openssl@3
cmake -B build -S . -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DOPENSSL_ROOT_DIR=$(brew --prefix openssl@3)
cmake --build build --parallel
open build/Genesis.app
```

### Windows (MSVC 2022)

```cmd
:: Requires vcpkg in PATH and cmake 3.20+
cmake -B build -S . ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release --parallel
build\Release\Genesis.exe
```

---

## Packaging

CPack is configured for all three platforms:

```bash
cd build
cpack                      # picks per-platform default
cpack -G TGZ               # tarball (any OS)
cpack -G DEB               # Debian (.deb)
cpack -G DragNDrop         # macOS (.dmg)
cpack -G NSIS              # Windows installer (.exe)
```

### Linux AppImage

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
cmake --install build --prefix build/AppDir/usr
./packaging/linux/build-appimage.sh build/AppDir
```

The CI matrix in `.github/workflows/release.yml` produces all of the above
on every push.

---

## Microsoft authentication setup

The launcher uses the OAuth 2.0 device authorization grant against Microsoft
consumer accounts. **You must register your own Azure AD application** —
distributing a launcher with the official Mojang client ID is not permitted.

1. Visit <https://portal.azure.com> → **Azure Active Directory** → **App registrations** → **New registration**.
2. **Name:** `Genesis Launcher` (or whatever you prefer).
3. **Supported account types:** *Personal Microsoft accounts only* (consumers).
4. **Redirect URI:** leave blank — device code flow does not need one.
5. After creation, open **Authentication** and enable
   *Allow public client flows* (`allowPublicClient: true`).
6. Copy the **Application (client) ID** and pass it to Genesis via:
   - environment variable `GENESIS_MS_CLIENT_ID`, or
   - `microsoft_client_id` in `~/.local/share/Genesis/config.json` (Linux),
     `~/Library/Application Support/Genesis/config.json` (macOS),
     `%APPDATA%\Genesis\config.json` (Windows).

The token, refresh token, and Minecraft access token are persisted via the
platform secure-storage backend (Keychain / Credential Manager / libsecret),
**never** to a plain config file.

---

## Code signing & notarization

CI produces **unsigned** binaries by default. To distribute them publicly,
sign with the relevant platform tools as a manual post-CI step.

### macOS (Developer ID + notarization)

Requires an Apple Developer account ($99/yr).

```bash
# 1. Codesign with hardened runtime
codesign --force --deep \
    --options runtime \
    --entitlements packaging/macos/entitlements.plist \
    --sign "Developer ID Application: Your Name (TEAMID)" \
    build/Genesis.app

# 2. Build & sign DMG
cpack -G DragNDrop
codesign --force --sign "Developer ID Application: Your Name (TEAMID)" \
    build/Genesis-*.dmg

# 3. Notarize
xcrun notarytool submit build/Genesis-*.dmg \
    --apple-id you@example.com \
    --team-id TEAMID \
    --password "@keychain:notarytool" \
    --wait

# 4. Staple
xcrun stapler staple build/Genesis-*.dmg
```

### Windows (Authenticode)

Requires an EV or OV code-signing certificate from a CA (DigiCert, Sectigo, etc.).

```powershell
signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 `
    /a /n "Your Company" `
    build\Genesis.exe build\Genesis-*.exe
```

For SmartScreen reputation, an **EV** certificate is strongly preferred —
OV-signed installers will warn users for the first ~3000 downloads.

### Linux

GPG-sign the release tarball / AppImage / `.deb`:

```bash
gpg --armor --detach-sign Genesis-x86_64.AppImage
gpg --armor --detach-sign Genesis-1.0.0-Linux.tar.gz
dpkg-sig --sign builder Genesis-1.0.0-Linux.deb
```

For Flatpak or Snap, use the standard distribution channels — those are
out of scope for this repo.

---

## Architecture

```
genesis-launcher/
├── include/genesis/        Public headers (one folder per subsystem)
├── src/
│   ├── core/               Launcher, EventBus, State, Result<T>/Error
│   ├── auth/               MicrosoftAuth, AuthManager, SecureStorage
│   ├── version/            Mojang version manifest, profile compositing
│   ├── assets/             AssetManager, Downloader, Verifier (SHA-1)
│   ├── jvm/                JavaFinder, ProcessHandle, JvmOrchestrator
│   ├── instance/           Per-instance config, mods, world data
│   ├── update/             Self-update check
│   ├── mods/               Modrinth client, performance pack installer
│   ├── logging/            spdlog wrapper + ring-buffer for the UI
│   ├── ui/                 ImGui shell, views, async dispatcher
│   └── platform/           PlatformUtils + per-OS glue
├── cmake/                  CompilerFlags, Platform, Dependencies
├── packaging/              .desktop, AppImage builder, plist templates
└── .github/workflows/      Build+release matrix
```

### Lifecycle invariants (verified)

`src/jvm/ProcessHandle.cpp` is the single authority for the JVM child process
lifecycle. The runtime harness at `audit/harness.cpp` enforces:

| # | Invariant                                                          | Cycles | Pass |
|---|--------------------------------------------------------------------|-------:|:----:|
| 1 | `wait_gate` — `register_handle_` blocks until child has spawned    |    500 |  ✓   |
| 2 | `dispatch_order` — state events fire in monotonic order            |    500 |  ✓   |
| 3 | `setsid_pgid` — child becomes session + process group leader       |    500 |  ✓   |
| 4 | `killpg_chain` — SIGTERM → SIGKILL escalation hits the entire PG   |    500 |  ✓   |
| 5 | `zombie_reap` — `waitpid` is called exactly once per spawned PID   |    500 |  ✓   |
| 6 | `post_reap_run` — no `Running` dispatch after `Stopped`/`Crashed`  |    500 |  ✓   |

Run locally:

```bash
g++ -std=c++20 -O2 -pthread \
    -I genesis-launcher/include \
    audit/harness.cpp audit/logger_stub.cpp \
    genesis-launcher/src/jvm/ProcessHandle.cpp \
    -o /tmp/harness
/tmp/harness
```

The same harness runs in CI on every push (`lifecycle-harness` job).

---

## License

MIT. See `LICENSE`. Genesis is not affiliated with Mojang or Microsoft —
"Minecraft" is a trademark of Mojang Synergies AB.
