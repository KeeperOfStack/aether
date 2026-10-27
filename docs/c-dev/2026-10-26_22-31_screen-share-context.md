# Screen Share Project: Context and Decisions

This document is the entry point for anyone (human or AI) picking up the
screen-share work in this repository. It is written so that a new session with
no prior history can understand the project, its constraints and what has and
has not been decided yet.

Status: **planning**. No screen-share code exists yet. Several decisions below
are recommendations waiting for owner approval.

---

## 1. What we are building

A better, more resilient "share screen" for voice chat, built on a fork of
Mumble (this repository, `aether`).

The defining requirement: **voice must never be interrupted by screen sharing.**
Users run demanding games and applications, so the screen-share feature must
also never degrade the machine it runs on. Reliability and lightweight
operation rank above features.

The original design spec is the owner's file
`C:\Users\Administrator\Downloads\aether-projectoutline.md`. It is not in the
repository. This document summarises it and records where we have diverged.

## 2. The repository

- `aether` is a fork of **Mumble**, an open-source low-latency voice-chat
  program written in C++ on Qt and Opus.
- Two components: the client (`mumble`) and the server (`murmur`).
- Build system: CMake. Root options include `client` (ON), `server` (ON),
  `packaging` (OFF) and `tests` (defaults to `packaging`), see `CMakeLists.txt`.
- Third-party code lives in `3rdparty/`, partly as git submodules. Run
  `git submodule update --init` before building.
- Relevant existing code:
  - `src/Connection.cpp`: already sets TOS/DSCP via `setTos` and uses Windows
    qWave QoS (`QOSAddSocketToFlow`). **Not yet verified which DSCP values
    voice uses.**
  - `src/Mumble.proto`: contains `PluginDataTransmission` (around line 629),
    a message that can carry plugin data between clients.
  - `src/MumbleProtocol.*`, `src/Connection.*`, `src/Net.*`: protocol and
    networking.
  - `plugins/`: game positional-audio plugins. **This is not a metadata
    channel**; do not use the plugin API for screen-share signalling.
- Installer: `docs/dev/TheMumbleSourceCode.md` says the Windows installer uses
  **WixSharp** (`installer/`). This conflicts with the CPack/WiX plan in the
  spec (see section 6).

## 3. Rules that govern this work

These come from the repository and the owner. Read them before doing anything.

1. **AI-assisted marker.** `AGENTS.md` requires every commit to end with an
   `AI-assisted` line and no tool-specific trailers such as `Co-Authored-By`.
   The owner removed the earlier push gate from `AGENTS.md`; the owner decides
   what gets pushed.
2. **Commit format.** `COMMIT_GUIDELINES.md`: `TYPE(Scope): Summary`, with an
   all-uppercase TYPE (`FEAT`, `FIX`, `DOCS`, `BUILD`, `CI`, ...). Each change
   gets its own commit, and each commit should compile.
3. **No local compilation of the installer.** The spec (section 3) forbids
   running MSVC, CPack or WiX builds locally. Compile feedback comes from CI.
   Static checks (clang-format, grep gates, CMake configure) are allowed.
4. **Isolation.** Screen-share code lives under `src/sidecar/`. WebRTC, capture
   and SFU headers must not be included anywhere in `src/mumble/` (the voice
   client). Enforce this with a CI grep gate.
5. **`CODING_GUIDELINES.md`** and `docs/dev/` govern style and design.

## 4. Architecture (from the spec, with our decisions)

Out-of-process sidecar. Screen sharing runs in a separate executable so a
capture, encode or network fault cannot touch the voice thread.

```
Mumble client (voice, Qt)  --- named pipe IPC --->  mumble-screenshare.exe (sidecar)
        |                                                   |
  raw UDP voice (Murmur)                          WebRTC to SFU (LiveKit)
        |                                                   |
  Murmur server (voice + text state only)          SFU (separate service, self-hosted)
```

- Process isolation: sidecar is `mumble-screenshare.exe`.
- IPC: Windows named pipe `\\.\pipe\mumble-sidecar-ipc` with length-prefixed,
  versioned messages. A bounded queue sits between the pipe thread and the rest
  of the app.
- Voice stays on its own thread and socket.

## 5. Decisions (recommended, awaiting owner approval)

| # | Topic | Decision | Why |
|---|-------|----------|-----|
| D1 | SFU | **LiveKit**, self-hosted, behind an `IMediaTransport` adapter so it can be swapped | Handles TURN, simulcast and bandwidth estimation; Mediasoup needs a Node runtime on the server. **Verify the C++ client SDK is maintained before committing**; fallback is LiveKit's signalling protocol directly. |
| D2 | Build verification | CI-only compiles. Local static checks only. Batch changes to keep the push loop short. | Honours the spec's rule. Each push needs owner approval under `AGENTS.md`. |
| D3 | Signalling | **`PluginDataTransmission`** for stream start/stop and room ID | No client protocol change. **Caveat:** SFU join tokens must be minted by a trusted party. Murmur holds the ACLs, so v1 needs a small server change where Murmur signs short-lived tokens with a server-held key. The client never holds that key. This contradicts the spec's "no server change" claim. **Confirm Murmur relays `PluginDataTransmission` to the intended recipients.** |
| D4 | Game safety: capture | Windows Graphics Capture; DXGI Desktop Duplication as fallback | Capture from the compositor. **No DLL injection or hooking into games**: it is an anti-cheat risk and a common crash cause. (The repo's overlay code does injection; the sidecar must not copy that approach.) |
| D5 | Game safety: priority | Below-normal priority, Windows EcoQoS, and a Job Object CPU cap (~15%) | The sidecar yields to games and cannot run away. |
| D6 | Game safety: fullscreen | When a fullscreen app is foreground, drop to 15 fps and a lower resolution; restore on exit | Keeps the sidecar's footprint small while a game runs. |
| D7 | Encode | Hardware H.264 via Media Foundation only; no software fallback in v1 | Software encoding steals CPU from games. On machines without a hardware encoder the stream fails cleanly. |
| D8 | Defaults | 1080p max, 30 fps, 4 Mbps video cap, no screen audio in v1 | Bounded cost. Screen audio adds capture complexity and is out of scope. |
| D9 | Resilience | Crash-only sidecar. Voice is independent. Restart with capped backoff. Preallocated, bounded buffers; drop frames under load rather than queue. | A sidecar crash affects only video. No state must survive a restart. No unbounded memory growth in long sessions. |
| D10 | QoS | Voice keeps its existing marking (to be verified). Sidecar video uses its own socket, marked AF41. | Per-flow marking so video cannot degrade voice. |

## 6. Problems in the original spec (must be fixed before work starts)

1. The vcpkg step `./scripts/buildsystems/vcpkg.cmake` is a CMake toolchain
   file, not a runnable script. vcpkg must be bootstrapped first.
2. `dotnet tool install wix --version 3.11.2` does not match how the .NET WiX
   tool is distributed. Pin one WiX version and verify the install command.
3. The MSI artifact path `build/_CPack_Packages/*.msi` is probably wrong.
   Verify on a real run.
4. The installer approach (CPack/WiX templates) conflicts with the repo's
   WixSharp setup. Choose one.
5. The Plugin API is the wrong mechanism for signalling (see D3).
6. "Raw UDP only" and "EF" DSCP claims must be checked against
   `src/Connection.cpp` before we build on them.
7. "Zero blocking calls on critical threads" needs definitions. Name the
   critical threads explicitly. Qt's event loop is not non-blocking everywhere.
8. "Bare-metal" cannot be literal for a Qt/Windows application. The working
   interpretation: no heap allocation on audio/voice callbacks, preallocated
   pools, RAII for every handle, error codes rather than exceptions on the audio
   path, and no blocking calls on the voice thread.

## 7. Open items

- Owner approval of D1 (SFU). Alternative SFUs are acceptable if the owner
  prefers them.
- Owner approval of the server change in D3 (token minting).
- Verify D10 (existing DSCP values) and the D3 relay behaviour in Murmur.
- Decide the installer approach (section 6, item 4).

## 8. Planned phases

0. Decisions (this document, approvals above).
1. Baseline and isolation: record voice baseline; add `src/sidecar/` as a
   separate CMake target behind an option defaulting to OFF; add the WebRTC
   grep gate in CI.
2. IPC layer: named pipe, framed and versioned, bounded ring, non-blocking
   voice path, sidecar death detection and capped reconnect.
3. Signalling and permissions: `PluginDataTransmission` payloads; server token
   minting and a permission check on who may share.
4. Sidecar media: WGC capture, hardware H.264, WebRTC transport, bounded frame
   queue with a buffer pool.
5. Client UI and lifecycle: share state from IPC, crash status, retry cap.
6. Network QoS: AF41 on a separate video socket; verify voice marking.
7. Installer and CI: one installer approach; fixed workflow.
8. Verification: voice under 30% packet loss; sidecar force-kill leaves voice
   running; DSCP inspection in Wireshark; 24-hour soak test tracking handles and
   working set; allocations on the voice hot path; voice p99 latency while video
   runs; ASan/TSan builds; IPC fault injection; game-performance comparison with
   and without the sidecar running.

## 9. How to resume this work

1. Read this document, then `AGENTS.md`, `COMMIT_GUIDELINES.md` and
   `docs/dev/TheMumbleSourceCode.md`.
2. Check the open items in section 7 and ask the owner which are still open.
3. Push, open PRs or create issues only when the owner asks for it.
4. Never introduce WebRTC or capture headers into `src/mumble/`.
