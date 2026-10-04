# Real-time Windows plugins in buzz-remote, browser-only — Design

Date: 2026-10-04
Status: draft for review. Builds on the offline proof of concept in this
directory; see [performance.md](../../performance.md) for the measurements this
design relies on.

## 1. Goal

Run an unmodified **32-bit Windows VST2/VST3 plugin as a buzz-remote machine in
real time**, entirely in the browser: static hosting, no backend, no native
helper.

Success means:

- a user installs a Windows plugin (`.dll` or `.vst3`) from a local file, adds
  it as a machine, and plays it from patterns and from live input;
- sequenced notes and parameter changes are sample-accurate, and live input has
  bounded latency (target ≤ 30 ms on top of the audio device);
- parameters are buzz globals and controller endpoints, and plugin state
  round-trips through `.bzw`;
- **Dexed 0.9.3 plays dense 8-voice material at 48 kHz with no underruns** on a
  mid-range laptop for 10 minutes.

Out of scope: 64-bit plugins (see option 3 in performance.md), plugin editors in
v1, sidechains and multi-bus plugins, copy-protected plugins.

## 2. Facts this design rests on

Measured in this repository (headless Chromium, Boxedwine single-threaded `jit`
build):

| Fact | Value |
|---|---|
| Emulator boot until the host is ready | ~20 s (with the wineboot fix) |
| First plugin start-up in a session | Dexed `VSTPluginMain`: ~9 s; later loads: ~0.5 s |
| Dexed audio processing | **3.8–4.7× realtime**, from the first render |
| PoC Synth audio processing | 12–20× realtime |
| Emulated vs. native DSP | ~100–200× slower |

From the code:

- **buzz-remote** renders the whole machine graph synchronously inside one
  AudioWorklet `process()`. Machines implement `ProcessMachine.process()` and get
  sample-offset events from `WorkletTimelineScheduler`, which is driven tick by
  tick (`onTickStart`). There is no latency or plugin-delay-compensation concept
  yet. `engine/telemetry/audioRing.ts` already provides a SharedArrayBuffer audio
  ring, and the prometheos host serves COOP/COEP (`require-corp`).
- **Boxedwine** devices are `FsVirtualOpenNode` subclasses registered with
  `Fs::addVirtualFile("/dev/…")` (`source/kernel/devs/`, `source/sdl/startupArgs.cpp`).
  Its browser audio already uses a SharedArrayBuffer PCM ring with an `Int32Array`
  control block feeding an AudioWorklet (`boxedwine-audio-worklet.js`).
- In Boxedwine's **multithreaded** builds (`-pthread -sPROXY_TO_PTHREAD`), guest
  threads run on pthreads (Web Workers), the WebAssembly memory is a
  SharedArrayBuffer, and JavaScript called from a pthread is **proxied to the
  browser main thread** (`boxedwine-multithreaded-audio.js`).
- Wine maps drive `Z:` to `/`, so a Windows program can open a Boxedwine
  device as `Z:\dev\<name>`.

## 3. Brainstorm: options per decision

### 3.1 Where the emulator runs

| Option | Verdict |
|---|---|
| Single-threaded build on the main thread (today's PoC) | **Offline only.** React and UI work would starve it and cause underruns; keep it for bouncing/freezing. |
| **Multithreaded JIT build in a hidden same-origin iframe; guest threads on pthreads** | **Chosen.** Emulation leaves the main thread, and one guest thread per plugin instance can run in parallel. Its single-thread speed against the `jit` build is unmeasured (Phase 0, task 1). |
| Whole emulator inside the AudioWorklet | Rejected: `process()` must return within one render quantum; the emulator can't be pre-empted, fetch files or start threads there. |

### 3.2 Transport between the guest and buzz-remote

| Option | Verdict |
|---|---|
| Files and the mailbox (today's PoC) | Rejected for real time: polling, Boxedwine's directory cache, filesystem overhead. |
| Windows audio (`waveOut`/DirectSound) through Boxedwine's audio ring | Rejected: output only (no MIDI or parameters in), one mixed stream, resampled (shell default 11,025 Hz), no per-instance routing. |
| TCP/WebSocket through Boxedwine's networking | Rejected: protocol stacks on the hot path, unbounded jitter. |
| **New device `/dev/vstbridge` with its rings in shared WebAssembly memory** | **Chosen.** The guest uses plain `ReadFile`/`WriteFile`; the emulator thread and the AudioWorklet exchange blocks with `Atomics` on the same memory: no copies through JavaScript, no main thread. This is yabridge's shared-memory audio path in emulator form. |
| A custom syscall (`int 0x80` with an unused number, called from Windows code) | Fallback if Wine mishandles a character device; less debuggable. |

### 3.3 Latency strategy

| Option | Verdict |
|---|---|
| Fixed output latency `L` for everything | **v1.** Simple and robust. |
| **Render ahead for sequenced material**: the scheduler hands this machine its events `L` samples early, so its output lands on time | **v2.** Pattern playback sounds latency-free; only live input and knob moves pay `L`. |
| Graph-wide plugin delay compensation | Later; buzz has no PDC concept. |
| **Freeze**: render a heavy plugin's track ahead, in the background, at any speed, and play the cached audio | **v3, the safety net** for plugins that can't keep up in real time (a DAW "freeze track" for emulated plugins). Re-render the affected span when patterns change. |

### 3.4 Process model

| Option | Verdict |
|---|---|
| One `vsthost` process per plugin instance (yabridge's default) | Rejected: each Wine process costs start-up time and memory. |
| **One `vsthost` process, one guest thread per instance** (yabridge "plugin groups") | **Chosen.** One Wine boot; the MT build maps guest threads to pthreads, so instances run in parallel. |

### 3.5 Block size

The emulator renders blocks of `B` frames. Bigger blocks amortize per-block
overhead (device call, wake-up, JIT dispatch) but raise latency. Default
`B = 256`, configurable 128–1024; `L` defaults to `4B` (1,024 frames ≈ 21 ms
at 48 kHz) and is raised automatically after underruns.

### 3.6 Start-up cost

- Boot the emulator lazily: on the first Windows machine, or when a project
  that contains one is opened, with visible progress.
- Persist the Wine prefix in IndexedDB (`storage=idb`) so first-run work happens
  once per browser, not once per visit.
- Ship a slimmer Wine filesystem (Boxedwine documents building one for the
  web); 158 MB today.
- Keep plugins loaded across graph rebuilds; the instance's lifetime belongs to
  the emulator host, not to the worklet's machine object.
- Warm up after load: render a short note burst to silence so first-time JIT
  translation of the note path doesn't hit live playback.
- Idea to research: snapshot the booted emulator's memory and restore it (as v86
  does) for near-instant start.

## 4. Architecture

```text
main thread (buzz-remote UI)                        AudioWorklet (buzz engine)
  WinVstHost ──────── creates ───────────┐           WinVstMachine (ProcessMachine)
   │ boots hidden iframe (Boxedwine MT)   │            │ per block: write request,
   │ sends LOAD / DESCRIBE / STATE        │            │ Atomics.notify, read output
   │ watchdog, restart                    ▼            │ L frames behind
   │                     ┌──── shared WebAssembly memory (SharedArrayBuffer) ────┐
   │                     │  vstbridge region: header + one channel per instance  │
   │                     │  request ring (events, transport) │ audio-out │ audio-in │
   │                     └──────────────────▲─────────────────────────────────────┘
   ▼                                        │ read/write via /dev/vstbridge
Boxedwine MT (pthreads)  ── Wine ── vsthost.exe --bridge
                                         └─ one guest thread per plugin instance:
                                            ReadFile(request) → plugin process → WriteFile(audio)
```

## 5. Components

### 5.1 Boxedwine: `/dev/vstbridge` (patch `0003-vstbridge-device.patch`)

- `source/kernel/devs/devvstbridge.cpp`, registered in `startupArgs.cpp` next
  to `/dev/null`. One open file handle is bound to one channel.
- **Region** allocated once at start-up in WebAssembly memory (before any
  growth); an exported `_vstbridge_region()` returns its address and size.
  JavaScript re-acquires `HEAPU8.buffer` after memory growth; offsets stay
  valid.
- **Channel layout** (little-endian, 64-byte aligned, versioned header):
  - control words (`Int32`): state, sample rate, block frames, `requestSeq`,
    `responseSeq`, underruns, last process time (µs), fault code;
  - request ring: fixed-size records, one per block: `blockIndex`, frame count,
    transport (playing, tempo, beat position), then up to N events
    `{offset, type, a, b, value}` (note on/off, CC, pitch bend, parameter, program);
  - audio-out ring and audio-in ring: planar float32, power-of-two capacity.
- **Guest calls**:
  - `ioctl(ATTACH, channel)` binds the handle; Wine's `DeviceIoControl` or a
    first `WriteFile` command carries it (Phase 0 decides which works through Wine).
  - `ReadFile` blocks until `requestSeq` advances, then returns the request
    record. Blocking uses the kernel's condition mechanism; in the MT build the
    emulator thread parks with `emscripten_futex_wait` on the `requestSeq` word,
    so the worklet's `Atomics.notify` wakes it **with no main-thread hop**.
  - `WriteFile` copies one block of output into audio-out and advances `responseSeq`.
- Single-threaded build: the same device, non-blocking (polling); used for
  offline rendering only.

### 5.2 `vsthost --bridge`

- Opens `Z:\dev\vstbridge` (control channel 0) and serves commands from the
  main thread: `LOAD(channel, path, rate, block)`, `DESCRIBE`, `GET_STATE`,
  `SET_STATE`, `UNLOAD`, `PING`.
- `LOAD` starts one guest thread per instance. That thread opens its channel,
  then loops: `ReadFile` request → apply events at their offsets → `process` B
  frames → `WriteFile` output. It is a strict request/response loop with no
  timing of its own.
- Per-instance thread setup: FTZ/DAZ in MXCSR and x87 control word, so
  denormals can't stall the emulated FPU; `SetThreadPriority(TIME_CRITICAL)`.
- `DESCRIBE` returns the JSON the offline host already produces (name, vendor,
  kind, parameters with display text), used once at install time.
- State: VST2 `effGetChunk`/`effSetChunk` (or the parameter list when the
  plugin has no chunks); VST3 `IComponent::getState`/`setState` and the
  controller's `setComponentState`, through an in-memory `IBStream`.

### 5.3 buzz-remote

- **Format** `winvst` next to `webvst` and `webclap` in `src/engine/plugins`:
  the archive holds the DLL or `.vst3` bundle plus a host-generated
  `winvst.json` (from `DESCRIBE`, frozen at install like WebCLAP descriptors).
  Class id: `winvst:<sha256-of-binary>:<name>`.
- **`WinVstHost`** (main thread, `src/engine/winvst/`): owns the hidden iframe
  with the MT Boxedwine build, boot progress, channel allocation and the
  control channel; posts the region's SharedArrayBuffer plus channel offsets to
  the worklet; runs the watchdog.
- **`WinVstMachine`** (worklet, `ProcessMachine`):
  - **v1**: each block, encode this block's events into the next request,
    `Atomics.notify`, and read the output written `L` frames earlier. On a
    shortfall, output silence, count an underrun and report it through
    telemetry. No allocation in `process()`.
  - **v2**: `WorkletTimelineScheduler` gains a per-machine `lookaheadSamples`:
    it delivers this machine's tick and row events `L` samples early, computed
    from the compiled schedule and the current tempo. Seek, loop and tempo
    changes flush the ring and re-sync. A tempo change inside the lookahead
    window is accepted as a small error at first.
- Parameters as `word` globals (`endpointKey = winvst:<index>`), the same
  encoding as the WebCLAP backend; plugin-side changes come back on a reply ring.
- Persistence: `prometheos.winvst/1` project extension, with `Machine.data` =
  plugin state, mirroring `prometheos.webclap/1`.
- UI: generic parameter window. **Editors later, almost for free:** Boxedwine
  already draws Wine windows to its canvas and forwards mouse input, so
  `effEditOpen`/`IPlugView::attached` on a `vsthost` window can show the real
  plugin GUI in a buzz editor window that hosts the emulator canvas.

## 6. Real-time budget

- **Throughput:** each instance's DSP must finish a block in under `B / rate`
  with headroom. Dexed uses ~25 % of one emulator thread at 44.1 kHz in the ST
  build. With one guest thread per instance, instances are bounded per thread,
  not in total, up to the number of cores.
- **Per-block overhead target:** < 0.3 ms (device calls, ring copy, futex
  wake-up). Phase 0 measures it.
- **Jitter sources:** first-time JIT translation of new code paths (addressed
  by the warm-up), Wine's background threads competing in Boxedwine's
  scheduler, and memory-growth stalls (prevented by allocating up front).
  Proxied JavaScript is designed out of the hot path.
- **Fallbacks when the budget is exceeded:** raise `L`; switch the machine to
  freeze mode (§3.3); show the measured realtime factor in the machine's info
  so users can choose.

## 7. Risks and open questions

| Risk | Plan |
|---|---|
| Boxedwine's MT JIT build stability with Wine and JUCE threads (only the ST build was tested here) | Phase 0 gate: Dexed loads and renders in the MT build. |
| Boxedwine's README lists "multi-threaded build also stutters with sound" as a known issue | The cause is not documented. If it is the `waveOut` path (proxied through the main thread), `/dev/vstbridge` bypasses it; if it is guest scheduling, it hits the bridge too. Phase 0 measures block jitter directly. |
| Wine opening `Z:\dev\vstbridge` as a character device; blocking `ReadFile`; `DeviceIoControl` reaching the device's `ioctl` | Phase 0 test program; fallback is the custom syscall. |
| `emscripten_futex_wait` from the emulator thread on a word that the worklet notifies | Phase 0 micro-benchmark of wake-up latency. |
| Memory growth replacing `HEAPU8.buffer` | Allocate the region at start-up; consumers re-acquire views on growth. |
| Underruns from JIT spikes and scheduling | Warm-up render, adaptive `L`, freeze fallback, telemetry. |
| 158 MB first download | IndexedDB prefix plus a slim filesystem (Phase 3). |
| Licences | Users supply their own binaries; Boxedwine GPL-2.0+, Wine LGPL, host GPL-3.0. |

## 8. Testing

- **Layout:** one shared description of the region, checked from C++
  (`offsetof`) and TypeScript, like the CLAP ABI golden test in buzz-remote.
- **Protocol:** ring wrap-around, sequence numbers, event encoding,
  underrun/recovery, all in TypeScript unit tests against a fake guest.
- **Correctness:** the real-time output, shifted by `L`, must be
  **sample-identical** to the offline render of the same events (the existing
  `vsthost` one-shot mode is the reference). This catches lost, late or
  misplaced events.
- **Endurance (headless Chromium):** Dexed, 10 minutes of sequenced 8-voice
  material, zero underruns at the default `L`; record block-time percentiles.
- **buzz-remote:** install, describe, `.bzw` round trip, controller routing,
  graph rebuild without plugin reload.

## 9. Phases

0. **Spike and go/no-go** (this repository): MT build plus `/dev/vstbridge`
   plus `vsthost --bridge`, one channel, a test page with an AudioWorklet and an
   on-screen keyboard playing Dexed live. Measure the realtime factor in MT,
   wake-up latency, block jitter and underruns at `L` = 512/1,024/2,048.
1. **buzz-remote machine with fixed latency:** format, install/describe,
   parameters, state, `.bzw`, telemetry.
2. **Render-ahead** for sequenced playback; **freeze** mode.
3. **Scale and polish:** parallel instances, plugin editors through the
   Boxedwine canvas, IndexedDB prefix, slim filesystem, start-up snapshot
   research.
