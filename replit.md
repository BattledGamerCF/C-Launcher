# Workspace

## Overview

pnpm workspace monorepo using TypeScript. Each package manages its own dependencies.

Also contains `genesis-launcher/` — a standalone C++20 Minecraft launcher project (built with CMake, not pnpm).

## Stack

- **Monorepo tool**: pnpm workspaces
- **Node.js version**: 24
- **Package manager**: pnpm
- **TypeScript version**: 5.9
- **API framework**: Express 5
- **Database**: PostgreSQL + Drizzle ORM
- **Validation**: Zod (`zod/v4`), `drizzle-zod`
- **API codegen**: Orval (from OpenAPI spec)
- **Build**: esbuild (CJS bundle)

## Key Commands (TypeScript monorepo)

- `pnpm run typecheck` — full typecheck across all packages
- `pnpm run build` — typecheck + build all packages
- `pnpm --filter @workspace/api-spec run codegen` — regenerate API hooks and Zod schemas from OpenAPI spec
- `pnpm --filter @workspace/db run push` — push DB schema changes (dev only)
- `pnpm --filter @workspace/api-server run dev` — run API server locally

## Genesis Launcher (genesis-launcher/)

C++20 Minecraft launcher. See `genesis-launcher/README.md` for full details.

### Key Commands (C++ launcher)

```bash
# macOS
brew install cmake curl openssl glfw
cmake -B genesis-launcher/build genesis-launcher -DCMAKE_BUILD_TYPE=Release
cmake --build genesis-launcher/build --parallel

# Windows (Developer PowerShell + vcpkg)
cmake -B genesis-launcher/build genesis-launcher -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build genesis-launcher/build --parallel

# Linux
sudo apt install cmake libcurl4-openssl-dev libssl-dev libglfw3-dev libsecret-1-dev
cmake -B genesis-launcher/build genesis-launcher -DCMAKE_BUILD_TYPE=Release
cmake --build genesis-launcher/build --parallel
```

### Architecture modules

| Module | Location | Description |
|--------|----------|-------------|
| `core` | `include/genesis/core/` | `Result<T>`, `StateMachine`, `EventBus`, `Launcher` |
| `auth` | `include/genesis/auth/` | Microsoft OAuth device flow, OS secure storage |
| `version` | `include/genesis/version/` | Mojang version manifest, profiles, mod loader specs |
| `assets` | `include/genesis/assets/` | Parallel downloads, SHA-1 verify, repair |
| `jvm` | `include/genesis/jvm/` | JVM config builder, Java auto-detection, process spawn |
| `instance` | `include/genesis/instance/` | Sandboxed instance directories |
| `update` | `include/genesis/update/` | Self-updater with checksum + rollback |
| `logging` | `include/genesis/logging/` | spdlog wrapper, rotating files, no secret data |
| `platform` | `include/genesis/platform/` | Cross-platform FS, process, clipboard, memory |
| `ui` | `include/genesis/ui/` | Dear ImGui views |

See the `pnpm-workspace` skill for TypeScript workspace structure, TypeScript setup, and package details.
