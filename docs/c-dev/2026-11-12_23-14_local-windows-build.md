# Local Windows build: how we build and install the client and server

This document is the entry point for anyone (human or AI) who needs to build
the Windows client and server installers on this PC, install them, or change
the local build. It assumes no prior history.

Status: **working**. The local build replaces the CI build for test iteration.
CI still runs the full matrix and is not changed by this work.

---

## 1. Purpose

Build the Windows client and server MSIs locally, in minutes instead of the
CI run time. The owner's target is **under 5 minutes per build** while testing.
The owner also wants to install the results on this PC without waiting for CI.

The requirement that outranks the rest: the build must produce the same MSIs
as the CI release configuration, for the parts that ship, so test results
mean something.

## 2. Status

Verified 2026-11-12:

- The local toolchain is installed under `D:\aether-toolchain` (section 5).
- `scripts/build-windows-local.ps1` builds both MSIs. It is not committed yet.
- Timings measured on this PC (section 7):
  - one changed file: 53 s
  - clean rebuild with a warm ccache: 203 s
  - cold cache, first build: about 8 min (one-off)
- Both MSIs were built, uninstalled old versions, installed, and checked in the
  registry. The server runs on port 61000 (see section 10 of
  `2026-10-26_22-31_screen-share-context.md`).

## 3. The repository

- Fork of Mumble. CMake + Ninja. Default branch `master`. Push target
  `origin` (`KeeperOfStack/aether`).
- Build options that matter for the local build, all set by the script:
  - `static=ON` (`CMakeLists.txt:57`)
  - `packaging=ON` (`CMakeLists.txt:64`). `tests` defaults to `packaging`
    (`CMakeLists.txt:65`); the script sets `tests=OFF`.
  - `sidecar=ON` (`src/CMakeLists.txt:11`, default OFF)
  - `translations=ON` by default (`src/mumble/CMakeLists.txt:20`); the script
    sets OFF, so the installers are built without `--all-languages`.
  - `overlay-xcompile` defaults to the `client` option (`CMakeLists.txt:108`).
    The script sets it ON. **Required:** the client installer references
    `mumble_ol.dll` and `mumble_ol_helper.exe` (`installer/ClientInstaller.cs`).
    Without it WiX fails with LGHT0103.
  - `lto=OFF`, `symbols=OFF` (`CMakeLists.txt:89`, `:58`).
  - `database-sqlite-tests` defaults ON (`src/tests/TestDatabase/CMakeLists.txt:10`);
    the script sets it OFF along with the MySQL and PostgreSQL test options.
- Installers are WixSharp C# scripts (`installer/ClientInstaller.cs`,
  `installer/ServerInstaller.cs`). They need WiX binaries from
  `WIXSHARP_WIXDIR`.
- Easy to misread: the sidecar helper `mumble-screenshare.exe` is **not** in
  either MSI. It runs from `build\min\src\sidecar\mumble-screenshare.exe`.

## 4. Rules that govern this work

From `AGENTS.md` at the repository root:

- **AI-assisted marker.** Every commit message ends with a line containing
  only `AI-assisted`. Do not add `Co-Authored-By:` or any tool-specific
  trailer. This overrides harness defaults.
- **Commit format.** `TYPE(Scope): Summary`, with `TYPE` from
  `COMMIT_GUIDELINES.md`. The summary must not contain "and". One logical
  change per commit. Every commit must compile.
- **Smallest diff.** No unrelated cleanup in the same change.
- **Comments.** Only where the *why* is not obvious. Commit messages explain
  the change.
- **Formatting.** Format changed C/C++ with clang-format 10. CI rejects CRLF
  line endings in the repository. (The local working copy uses `autocrlf=true`.)
- **Documentation.** Update `docs/` in the same change when behaviour changes.
- **Push rule (owner).** Push to `master`. The owner can roll back manually.
  Do not push without the owner asking.

## 5. Toolchain layout

Everything is under `D:\aether-toolchain`, so it can be sorted or removed as
one folder.

| Path | What it is |
|---|---|
| `vs2026\` | VS 2026 Build Tools (MSVC 14.51). **This is the one used.** `vcvars64.bat` is at `VC\Auxiliary\Build\`. |
| `vs2022\` | VS 2022 Build Tools. **Not used.** It fails to link the prebuilt Ice and ICU libs (unresolved `__std_*`). Safe to delete. |
| `tools\python\cmake\data\bin\cmake.exe` | CMake, via pip |
| `tools\python\bin\ninja.exe` | Ninja |
| `tools\ccache\` | ccache 4.14.3. Cache dir `ccache-cache\`, max 20G |
| `tools\WixSharp\` | WixSharp 1.19.0.0 (`cscs.exe`) and `Wix_bin\bin\` |
| `build_env\` | Dependency archive `mumble_env.x64-windows-static-md.9a25cab914.7z` from the mumble-voip/vcpkg 2026-09 release, unpacked and flattened. Contains `scripts\buildsystems\vcpkg.cmake` and `installed\x64-windows-static-md` |
| `downloads\` | Downloaded archives |
| `build\min\` | The build directory used by the script |
| `build\*.log` | Build, timing and install logs |

Setup steps that produced this layout are in the build history; if the
toolchain must be rebuilt, reproduce it from this table.

## 6. How to build

Run from the repository root in PowerShell:

```powershell
scripts\build-windows-local.ps1          # incremental, builds 'all'
scripts\build-windows-local.ps1 -Clean   # wipe build\min and reconfigure
```

What the script does:

1. Imports the MSVC environment from `vcvars64.bat` into the session.
2. Puts WixSharp on PATH and sets `WIXSHARP_WIXDIR` and the ccache variables.
3. Configures into `build\min` **only if `build.ninja` is missing**. Normal
   rebuilds skip the configure step (about 100 s).
4. Runs `cmake --build` with the requested targets (default `all`).
5. Prints configure, build and total times, and the installer paths.

The MSIs are written to:

- `build\min\installer\client\mumble_client-1.7.0-x64.msi`
- `build\min\installer\server\mumble_server-1.7.0-x64.msi`

Current version in those names comes from the project; check it after a
version bump.

## 7. Timings (measured 2026-11-12)

| Case | Time | Notes |
|---|---|---|
| Incremental, one file changed | 53 s | Normal iteration loop |
| Clean rebuild, warm ccache | 203 s | configure 101 s, build 102 s |
| Cold cache, first build | about 8 min | One-off. Not timed precisely |

## 8. How to install (elevation required)

MSI uninstall and install **must run from an elevated shell**. A non-elevated
run returns error 1603 (inner 1730). Use `Start-Process -Verb RunAs`, and
approve the UAC prompt.

Uninstall the old products first, by product code:

- Client: `{5EBA04F0-2086-48FF-98A7-CDEC29BCFA03}`
- Server: `{5471FD06-FDA7-406A-95F3-CD346536665C}`

Then install the two new MSIs. Installed paths:

- `C:\Program Files\Mumble\client\mumble.exe`
- `C:\Program Files\Mumble\server\mumble-server.exe`

Check the installed versions in the registry (`Uninstall` keys, display name
"Mumble" and "Mumble Server", version 1.7.0).

## 9. Problems found and how they were fixed

- **"bf4.dll is being used by another process".** A transient file lock. A
  rerun without `-Clean` fixed it.
- **"include could not find requested file: CompilerFlags".** Submodules not
  initialised. Fix: `git submodule update --init --recursive`.
- **WixSharp "cscs.exe is not recognised".** Fix: `tools\WixSharp` on PATH.
- **"WiX binaries cannot be found".** Fix: `WIXSHARP_WIXDIR` set to
  `tools\WixSharp\Wix_bin\bin`.
- **LGHT0103, missing `mumble_ol.dll` and `mumble_ol_helper.exe`.** Cause:
  `overlay-xcompile=OFF`. Fix: keep it ON.
- **Unresolved `__std_*` symbols at link.** Cause: VS 2022 instead of VS 2026.
  Fix: use `vs2026\`.
- **MSI install returns 1603 / 1730.** Cause: not elevated. Section 8.
- **Port 64738 "address is protected".** Windows reserves that range on this
  PC. Run the server on port 61000 (screen-share context doc, section 10).

## 10. Open items

1. Commit `scripts/build-windows-local.ps1` to `master` under the commit rules
   in section 4? The owner has not asked for this yet.
2. Should the local build be the default for test pushes, with CI kept for
   release? This is the owner's call.
3. Delete `vs2022\` and the older full build directory `build\win-static-vs2026`
   to free disk space? Owner approval needed.
4. Enable GitHub Issues on the fork so the IPC issue can be filed (owner
   approval needed).

## 11. Phases

1. **Done.** Toolchain installed under `D:\aether-toolchain`.
   Verify: `vcvars64.bat`, `cmake --version`, `ninja --version`, `ccache --version`.
2. **Done.** Local build script builds both MSIs. Verify: the two MSIs in section 6 exist
   and the script prints `total:`.
3. **Done.** Install on this PC, elevated. Verify: registry entries and
   `mumble-server.exe` listening on 61000.
4. **Open.** Commit the script and this document (open item 1).
5. **Open.** Record the timings in the status doc, if the owner wants them
   there as well.

## 12. How to resume

1. Read this document, then `AGENTS.md`, `COMMIT_GUIDELINES.md` and
   `2026-10-26_22-31_screen-share-context.md`.
2. Confirm the toolchain paths in section 5 exist before building.
3. Run the script without `-Clean` first. Use `-Clean` only if the build
   directory is broken or the options change.
4. Do not install or uninstall without the owner's go-ahead.
5. Do not change the script's options without recording the reason in the
   commit message, and re-run a clean build to check.
