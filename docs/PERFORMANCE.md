# Performance and power (0.3.0 work, branch perf-ui-030)

What the interface and the engine cost while idle and while receiving, what was changed, and how to measure it again.
All numbers are from `bench_ui_power` (tests/bench/bench_ui_power.cpp) on an Apple silicon Mac, built standalone with `-O2` and
run under `nice -n 19` while other builds ran, so the CPU figures are upper bounds; the wake-up counts and allocation counts are exact.

## How to measure

* `bench_ui_power [frames] [tr] [rfcam] [ring]`: headless, no window, no radio. Built with the tests (not on Windows).
* View > Performance overlay (off by default): frame rate, the redraw pace and why (on battery, window covered), frame build time
  (average and worst per second), the process's CPU, its wake-ups per second (macOS; context switches on Linux) and the busiest threads
  by name. Nothing is measured while it is off.
* `DECT2_PERF=1`: the same once every 5 s on standard error, so a `--hidden` run can be measured without looking at the screen.
  `DECT2_FPS=1` (frame statistics) and `DECT2_STATLOG=1` (receiver status) are unchanged.

## What was already good (no change needed)

* The frame loop already slept in `glfwWaitEventsTimeout()` with a per-state pace (60 fps with input or video, 30 receiving,
  15 in the background, 10/4 idle) instead of following the display (the GL main window swaps with vsync, the pop-out without, so a
  frame never waits twice).
* The main waterfall uploads one new row per spectrum frame (`replaceRegion` / `glTexSubImage2D` of 4096x1), not the whole texture.
* Spectrum and receiver telemetry reach the interface by sequence number: no copy when nothing new arrived.
* The decoder pools are bounded (shared pool: hardware threads - 1, at most 8; ATSC 3.0 at most 8; DVB-S2 at most 6; conceal and DTMB at most 4)
  and wait on condition variables. The PLP decoder takes all hardware threads, but only while a frame is being decoded.
* No `glFinish` / `glGetError` per frame; the Metal layer is framebuffer-only.

## Changes and their numbers

### 1. Redraw pace: covered windows and battery (app/ui/frame_pace.h)

The policy moved out of `paceFrame()` into a pure function with a test (tests/app/test_frame_pace.cpp checks every state combination
never redraws faster than before). New inputs: the window is covered (macOS `occlusionState`: other windows over it, another Space,
the screen off or locked) and the computer runs on its battery (IOKit power source; Windows `GetSystemPowerStatus`; Linux
`/sys/class/power_supply`), with View > Power > Low power mode (automatic by default; in 3.0 this one setting also drives dect2/power, see docs/PERFORMANCE-POWER.md).

| window state | before fps | after fps |
|---|---|---|
| idle, focused / background | 10 / 4 | 10 / 4 |
| receiving, focused / background | 30 / 15 | 30 / 15 |
| receiving, covered window | 15 | 10 |
| TV playing, covered window | 60 | 10 |
| dragging, on battery | 60 | 30 |
| receiving, background, on battery | 15 | 10 |
| TV playing, on battery | 60 | 60 (the picture keeps its rate) |
| minimised | 10 | 10 |

A covered window also does no GPU work at all on Metal (`Backend::skipFrame()`): the interface keeps running (decoders, lists,
waterfall), but no drawable is taken and nothing is encoded. When it is uncovered the next frame (at most 100 ms, at once on a click)
draws again.

### 2. Metal: the drawable is taken when the frame is drawn

`MtlBackend::newFrame()` took `nextDrawable` before the interface was built, so a wait for the display came before the input was read
and the drawable was held for the whole build. It is now taken in `endFrame()`, held only while encoding, and a missing drawable drops
the frame instead of encoding into a nil texture.

### 3. App Nap only when idle (app/platform/power_cocoa.mm)

There was no App Nap handling. A covered OnAir with a receiver running could be napped by macOS (timers coalesced, threads throttled)
and drop samples. While any receiver runs the app now holds an `NSActivityUserInitiatedAllowingIdleSystemSleep` activity (the screen
and the computer may still sleep when idle); with no receiver it may nap as before.

### 4. The analysis thread waits for samples instead of polling (core/include/dect2/common/ring.h)

`Engine::analysisLoop()` slept 2 ms whenever the ring was empty, about 400 wake-ups a second whatever the radio did. It now sleeps in
`IqRing::waitData()` until about 5 ms of signal is there (the radio's write wakes it; the producer only touches the mutex while the
consumer waits) or the next 33 ms telemetry tick. tests/common/test_ring_wait.cpp covers the timeout, the wake and a lost-wake stress.

| radio pattern | wake-ups/s before | after | consumer CPU before | after |
|---|---|---|---|---|
| stalled radio (no samples) | 399 | 30 | 0.27 % | 0.03 % |
| sound card 48 kHz, 1024 per write | 385 | 71 | 0.35 % | 0.09 % |
| RTL-SDR 2.4 Msps, 16384 per write | 395 | 169 | 0.34 % | 0.18 % |
| HackRF 10 Msps, 131072 per write | 400 | 104 | 0.33 % | 0.08 % |
| SDRplay 2 Msps, 1008 per write | 422 | 209 | 0.24 % | 0.17 % |

The latency of a sample to the decoder stays within about 5 ms (it was within 2 ms plus the USB transfer). The Rust ring (opt-in
`DECT2_RUST_RING`) keeps the old 2 ms sleep.

### 5. tr() without allocations (app/i18n/i18n.cpp)

A translated `"Label##id"` longer than the string's inline buffer (22 characters) built a `std::string` on every call, every frame.
The cache is now keyed by `string_view` into the catalog's own storage.

| language | ns per tr() before | after | heap allocations per 1000 calls before | after |
|---|---|---|---|---|
| English | 1.3 | 1.2 | 0 | 0 |
| German | 23.0 | 16.1 | 111 | 0 |
| Japanese | 21.4 | 17.7 | 111 | 0 |

### 6. RF camera picture blended only when it changed (app/df/rfcam_ui.cpp)

The heat overlay copied the 1080x270 picture, blended it and uploaded the whole texture every frame. It is now redone only when the
picture, the direction result (about 8 Hz) or a setting changed: texture uploads go from 70 MB/s to 9.3 MB/s at 60 fps (the blend
itself is 0.1 ms).

### 7. Message database table clipped (app/panels/msgdb_ui.cpp)

The 300-row page built every row each frame (a time string, an ID, five cells). With single-line rows (the usual case) only the rows
in view are built now (`ImGuiListClipper`); a page with multi-line texts is drawn as before.

### 8. Thread classes by name (core/include/dect2/common/thread_name.h)

`namedThread()` gives savers, clean-up and watchdogs (`onair-results`, `onair-tmsave`, `onair-reap`, `onair-plg-dog`, `onair-scandb`,
`onair-msgdb`, `onair-upd`, `onair-ntp`, `onair-discord`, `onair-health`) the macOS background QoS class (efficiency cores, coalesced
timers; nice 10 on Linux), and uploads nobody waits on (`onair-mapsend*`, `onair-feeds`, `onair-feedtargets`, `onair-psk`,
`onair-packs`, `onair-updl`, `onair-airport`) the utility class (nice 5). The sample path, decoders and players keep what they set
themselves (`setThreadPriority`: user-interactive / user-initiated / utility). The main thread stays user-interactive.

### 9. Big spectrum FFTs and the fast waterfall (3.3; core/src/dsp/spectrum.cpp)

Reported: "SDRconnect 262144 is easy, OnAir 131072 lags". Measured with `bench_specfft [Msps] [seconds]` (tests/bench/bench_specfft.cpp, standalone
`-O3`, Apple silicon, Accelerate FFT) and `bench_fft` (`-DDECT2_PORTABLE` for the portable kernels Windows and Linux run).

* **Where the time goes** (ms per spectrum frame at 10 Msps, two blocks; one core is 33 ms of every 33 ms): 4096 bins 0.03; 16384 0.12;
  65536 0.50; 131072 1.05; 262144 2.3 (of which windows, transforms and power sums 1.5, the dB conversion 0.16, the copies 0.08, the trace and row averages
  0.55). That is 0.1 % to 4.4 % of a core on the Mac, so the transform is not what lags there. The portable FFT (what a PC runs; NEON here,
  AVX2 there) is about three times slower than Accelerate: 262144 points in 1.7 ms against 0.5 ms, still about 5 % of a core at 30 frames a second.
* **What did lag**: a frame is made of transforms of consecutive samples, so below a sample rate the frame rate was fs / N, not 30:
  131072 bins at 2.4 Msps gave 18 frames a second, 262144 bins 9, and at 1 Msps 7 and 3.7 (a picture that moves a few times a second is the
  lag). Since 3.3, with the sample rate known (`SpectrumAnalyzer::setSampleRate`, set by the engine) a size for which two blocks do not fit in a
  frame period is made from overlapping windows, one every fs / 35 samples: 29 to 30 frames a second at every rate.

  | Msps | bins | frames/s before | after | CPU ms per signal second before / after |
  |---|---|---|---|---|
  | 1 | 131072 | 7.3 | 29.0 | 3.4 / 10.4 |
  | 1 | 262144 | 3.7 | 27.7 | 3.7 / 19.5 |
  | 2.4 | 131072 | 18 | 29.7 | 7.5 / 12.3 |
  | 2.4 | 262144 | 9 | 29.0 | 7.9 / 23.3 |
  | 4 | 262144 | 15 | 29.7 | 12.7 / 23.7 |
  | 10 and 20 | 16384 to 262144 | 30 | 30 | unchanged (the sizes do not overlap) |

  The automatic size (8192 to 65536, bins of about 125 Hz or finer) never needs it: nothing changes for anyone who has not picked a big size by hand.
* **The analysis thread no longer waits for a transform.** `feed()` held the analyser's mutex for the whole call, transforms included, so the
  thread that also runs the decoders (it takes a frame every 33 ms) could wait for a 262144-point transform. The transforms now run on the
  spectrum thread's own buffers and only the sum is added under the mutex (a `gen` counter drops a block that was under way when `resize()` /
  `reset()` came). `tests/dsp/test_spectrum_threads.cpp`, also under ThreadSanitizer, ASan and UBSan.
* **Not changed on purpose**: `fftSplit` (the decoders' transform; its bits are pinned by `test_fft_bits`). A cache-blocked version of its stages was tried
  and measured: no faster on Apple silicon (1.6 ms against 1.8 ms at 262144), so it was left out. The frames, their dB scale and the
  statistics are bit for bit what they were (`test_spectrum_fastrows`). Bins above 262144 are not offered (`kMaxFft`).
* Small sizes do not regress: 4096 to 262144 bins at 10 and 20 Msps, ms per frame within noise of the old analyser (bench_specfft run
  against both).

**Fast waterfall rows** (Waterfall > Fast (Morse / CW), or the "CW / fast" button): a row every 1/50, 1/100, 1/200 or 1/500 s from the engine
(`SpectrumAnalyzer::setFastRows`): its own windowed transforms of about 20 ms of signal (1024 to 65536 points, `fastRowFft`, kept under 8 M points
a second), a row being the mean power of up to four of them spread over its hop. The screen takes whatever rows have arrived since the last frame
(a batch) and writes all of them, so the speed does not depend on the 30 to 60 frames a second; the time scale and the "row from ... ago" readout
use the exact row time. The window draws 60 frames a second while a focused, live fast waterfall is on (30 on battery power-saving).
Cost on top of the ordinary frames (`bench_specfft`): 1 % to 2 % of a core at 2.4 and 10 Msps for 100 to 500 rows a second, 0.1 % at 48 ksps;
the waterfall history is 512 rows, so 100 rows a second show the last 5 seconds.

## Looked at, not changed

* Spectrum computation while the spectrum is not shown: the spectrum also feeds the waterfall, the auto labels, the bandwidth finder,
  the scanners and the band activity, so skipping it would change behaviour; its cost is in `bench_spectrum`.
* Other sleep loops (file playback pacing 1 ms, a few decoders' 1-5 ms idle sleeps): they only run while that source or mode is
  active; the same `waitData()` can replace them one at a time.
* Tables without a clipper: about 130 screens; most lists are capped by their decoder (tens to a few hundred rows). The message
  database was the largest uncapped page.

## Dev flags for screenshots

* `--no-first-run` (or `DECT2_SKIP_FIRSTRUN=1`): no first-start question (public map consent); the tour and setup wizard only start with no arguments, so with `--hidden --mute --shot` nothing covers the UI. Use it for all screenshot work, together with a renamed copy of the binary for isolated settings.
* `DECT2_FAKE_CPU=<percent>`: the status strip takes this as the process CPU (100 = one core), to see the CPU colours and the "Too slow" warning with its Fix button (needs a running receiver and about 4 s).
