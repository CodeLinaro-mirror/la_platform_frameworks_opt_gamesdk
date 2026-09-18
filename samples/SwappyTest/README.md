# SwappyTest Application

An OpenGL ES 2.0 (`SwappyGL`) and Vulkan (`SwappyVk`) frame-pacing benchmark and test application for evaluating Android Game SDK (`Swappy`) pacing algorithms, auto-swap-interval transitions, pipeline modes, buffer-stuffing recovery, and interaction with CPU/GPU dynamic voltage and frequency scaling (DVFS) and scheduler core migration using calibrated synthetic workloads.

---

## Background & Evaluation Goals

SwappyTest drives deterministic and stochastic CPU and fragment-shader GPU workloads calibrated to hardware execution rates so frame-pacing behavior can be measured reproducibly across devices and graphics APIs:

1. **Frame Pacing, Pipeline Mode & Buffer-Stuffing Evaluation:**
   Measures frame delivery stability, deadline misses, swap-interval ladder transitions (`S2` ramp-up, `S4` in-budget jitter), explicit frame-rate caps (`S5`/`H7`), mid-run `resetFramePacing()` recovery (`S10`), pipeline mode (`on`/`off`/`auto`), and buffer-stuffing mitigation (`CUSTOM` step overrides).
2. **DVFS Downclocking & 30 FPS Lock-In Reproduction (`S1`, `S7`):**
   - **Downshift to 30 FPS:** When per-frame CPU or GPU workload exceeds the 60 FPS budget (`16.67 ms`, such as a `27 ms` spike), Swappy's auto-swap-interval ladder drops the target rate to 30 FPS (`33.33 ms` budget).
   - **Governor Downclocking & Core Migration:** Because the CPU or GPU has significant idle time in each `33.33 ms` window while Swappy sleeps to pace presentation, DVFS governors scale down clock frequencies and/or the scheduler migrates the render thread from prime/big cores to medium or little cores.
   - **Lock-In at 30 FPS:** When the app's true workload drops back below the 60 FPS budget (e.g. `14 ms -> 4 ms`), the same work takes longer in wall-clock time at the lower clock frequency. If the pacer judges readiness from unscaled wall-clock duration alone, it remains stuck at 30 FPS even though the workload could sustain 60 FPS at higher clocks.
3. **False-Upshift ("Honeytrap") & Bottleneck-Transition Resilience (`H1`–`H10`):**
   Verifies that upshift heuristics and recovery probes do not trigger thrashing or false 60 FPS upshifts under sustained heavy loads (`H1`, `S11`), marginal loads around the 60 FPS threshold (`H2`), periodic square-wave spikes (`H3`/`S3`), high-variance jitter (`H4`), GPU-bound passes (`H6`), idle loads (`H8`), CPU-to-GPU bottleneck flips (`H9`), or external big-core frequency boosts (`H10`).

---

## Test Phases

### Calibration (Phases 0 & 1)
Scenario steps specify workloads in milliseconds of peak-clock work (for example, `27 ms` or `12 ms`). Because raw loop and shader speeds vary across devices, SoCs, and screen resolutions, SwappyTest calibrates the device before scenarios are run:

- **Three Independent Calibrations (`cpu`, `gles`, `vulkan`)**: Run once via the **Calibrate CPU**, **Calibrate GPU**, or **Calibrate All** buttons (or `-e calibrate ...`). Results are cached on device (`swappy_calibration_{cpu,gles,vulkan}.json` and `calibration_report_{cpu,gles,vulkan}.txt`) and reused across scenario runs as long as the OS build (and surface resolution for GPU) is unchanged.
- **0. Thermal Cooldown Phase**: Waits for the device to cool down if prior runs caused thermal throttling so calibration measures unthrottled hardware performance.
- **1. CPU & GPU Rate Calibration Phase**: Measures how many synthetic CPU loop iterations and offscreen GPU fragment-shader passes correspond to `1 ms` of work at peak clock (plus any fixed per-pass GPU overhead). Scenario runs require a valid cache for the workload types they use (`cpu` for CPU steps, and `gles` or `vulkan` for GPU steps).

### Scenario Execution — `S1` 30 FPS Lock-In Overview (Phases 2 & 3)
Once calibrated, running a scenario steps through its target workload profile (see [Scenario Catalog](#scenario-catalog)). The default scenario **`S1`** (`39 s`) illustrates how a two-phase workload profile reproduces and measures 30 FPS lock-in:

### 2. Spike Phase (3.0s)
- The render thread runs a calibrated `27 ms` CPU workload each frame between frame start and swap/present, exceeding the `16.67 ms` 60 FPS budget.
- Swappy's auto-swap-interval ladder downshifts presentation to **30 FPS**.

### 3. Ramp-Down & 60 FPS Evaluation Phase (36.0s)
- Holds $18\text{ ms}$ for **3.0s** ($3.0\text{s}$ induction window): too heavy for a 60 FPS frame at any clock, so Swappy stays at 30 FPS while the governor clocks down.
- Evaluates $14\text{ ms} \to 4\text{ ms}$ at **3.0s per step** ($33.0\text{s}$ evaluation zone, $1,980$ ideal frames). The app emits ATrace / Perfetto markers (`SwappyTest::Run`, `SwappyTest::Settings`, `SwappyTest::Calibration`, `SwappyTest::Step`, `SwappyTest::RunEnd`) and logs a single `SCENARIO_RESULT` summary line on `SwappyTest` when the run completes.

---

## Scenario Catalog

Durations count the scenario steps only; the cooldown and calibration run beforehand as their own action (see [Test Phases](#test-phases)).

| ID | Load profile | Duration |
|---|---|---|
| `S1` | CPU ramp-down: 27 ms, 18 ms, then 14 ms to 4 ms in 1 ms steps, 3 s each (13 steps). | 39 s |
| `S2` | CPU ramp-up through the targets around the 16.7 ms budget: 12, 14, 15, 16, 17, 18, 20 and 27 ms, 3 s each (8 steps). | 24 s |
| `S3` | Alias of `H3`, run only on request. | 40 s |
| `S4` | In-budget jitter: a uniform random 6-14 ms CPU load per frame (six 5 s steps). | 30 s |
| `S5`, `H7` | Explicit 30 FPS request with an 8 ms CPU load (two 5 s steps). | 10 s |
| `S7` | GPU-bound ramp-down through the `S1` targets as fragment shader time, with a 1 ms CPU load (13 steps). | 39 s |
| `S10` | 27 ms for 3 s, 12 ms for 6 s, 27 ms for 3 s, then 12 ms for 6 s after `SwappyGL_resetFramePacing()` / `SwappyVk_resetFramePacing()` (4 steps). | 18 s |
| `S11` | Constant 27 ms CPU soak (twenty 30 s steps), run only on request. | 600 s |
| `H1` | Constant 27 ms CPU load (six 6 s steps). | 36 s |
| `H2` | 18 ms for 24 s (four 6 s steps), then 14 ms for 24 s (four 6 s steps). | 48 s |
| `H3` | Square wave: 27 ms and 10 ms for 5 s each, four periods, enough for the projected ladder's first three upshift attempts. | 40 s |
| `H4` | A uniform random 20-30 ms CPU load per frame (six 6 s steps). | 36 s |
| `H6` | GPU-bound 25 ms fragment shader with a 1 ms CPU load (six 6 s steps). | 36 s |
| `H8` | Idle app: 1 ms CPU load (two 6 s steps). | 12 s |
| `H9` | Bottleneck flip: 24 ms CPU (0 ms GPU), then 24 ms GPU (1 ms CPU), then 10 ms GPU (1 ms CPU), 10 s (two 5 s steps) each. | 30 s |
| `H10` | 22 ms CPU load (six 6 s steps); every 6 s step opens with a 500 ms external big-core boost pulse. | 36 s |
| `CUSTOM` | User-defined steps (up to 32) configured via the `steps` / `custom_steps` intent extra or the UI (an empty spec yields one idle `5s:cpu=0,gpu=0` step). | custom |

Custom step strings (`steps` or `custom_steps`) are semicolon- or pipe-separated (`;` or `|`), with an optional `<dur>[s]:` prefix (default `5s`) followed by comma-separated `key=value` pairs:
- `dur`, `d`, `duration`: Step duration in seconds (`float > 0`, default `5.0`).
- `cpu`, `cpu_ms`: Target CPU workload in ms (`int >= 0`, default `0`).
- `gpu`, `gpu_ms`: Target GPU fragment-shader workload in ms (`int >= 0`, default `0`).
- `jit`, `jitter`: Per-frame uniform random CPU workload range `<min>-<max>` in ms (`0 <= min <= max`).
- `pipe`, `pipeline`, `auto_pipeline`: Per-step pipeline override (`on`, `off`, or `auto`).
- `auto_si`, `auto_swap_interval`: Per-step auto-swap-interval override (`on`/`true`/`1` or `off`/`false`/`0`).
- `stuff_fix`, `buffer_stuffing_fix`: Per-step `setBufferStuffingFixWait` frame count (`int >= 0`).
- `fps`: Explicit target frame rate for the step (`int > 0`; values `<= 30` mark the scenario as explicit 30 FPS).
- `reset`: Calls `resetFramePacing()` at step start (`on`/`true`/`1`).
- `boost`: Fires a 500 ms external big-core CPU boost pulse at step start (`on`/`true`/`1`).

Example: `3s:cpu=0,gpu=0; 5s:cpu=8,gpu=0,pipe=off; 5s:cpu=4,gpu=12,pipe=on,stuff_fix=2`.

---

## Building and Running

### Build via Gradle
```bash
cd samples/SwappyTest && ./gradlew assembleDebug
```

### Install and Launch via ADB
```bash
adb install -r samples/SwappyTest/app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n com.samples.swappytest/.SwappyTestActivity
```

### Logcat Telemetry
```bash
adb logcat -s SwappyTest
```

### Intent Extras
Boolean extras accept `--ez key true`, `-e key true`, `1`/`0`, or `on`/`off`; integer and float extras also accept string values (`-e key <val>`).

| Extra | Default | Effect |
|---|---|---|
| `scenario` | `S1` | Scenario ID (`S1`-`S5`, `S7`, `S10`, `S11`, `H1`-`H4`, `H6`-`H10`, or `CUSTOM`; alias `SCENARIO`). Its presence starts the run on launch and exits the app when the scenario completes. |
| `api` | `gles` | Rendering backend (`gles` / `gl` / `opengl` / `0` or `vulkan` / `vk` / `1`; aliases `render_api`, `backend`). |
| `pacing` | `classic` | Pacing selection: `classic` Swappy pacing, or `no_swappy` (`noswappy`, `none`, `3`; or integer extra `pacing_strategy`) to present without Swappy as an unpaced baseline. |
| `calibrate` | unset | Runs calibrations instead of a scenario, replacing their caches and reports: a comma-separated list of `cpu`, `gles`, `vulkan`, `gpu` (the GPU calibration of `api`) and `all`. `true` (or `--ez calibrate true`) calibrates `cpu` and the GPU of `api`, as before. GPU calibrations of both APIs run one after the other, switching the render API. `recalibrate`, `calibration` and `force_calibration` are aliases. |
| `swappy` | `true` | `false` presents without Swappy (`eglSwapBuffers` on GLES, direct `vkQueuePresentKHR` on Vulkan) with no pacing. |
| `set_30_fps_limit` | `false` | Sets a 30 FPS swap interval floor. Auto swap interval stays on, so Swappy's ladder still runs and must not pace faster (implied by S5 and H7). |
| `pipeline`, `auto_pipeline` | `auto` | Swappy pipeline mode override (`auto` / `true` / `1`, `on`, or `off` / `false` / `0`). |
| `auto_swap_interval`, `auto_si` | `true` | Enables or disables Swappy's auto-swap-interval ladder (`setAutoSwapInterval`). |
| `stuff_fix`, `buffer_stuffing_fix` | unset | Overrides `setBufferStuffingFixWait` (`>= 0` frames). |
| `fence_timeout_ms` | unset | Overrides Swappy's fence timeout in milliseconds (`setFenceTimeoutNS`, `>= 0`). |
| `steps`, `custom_steps` | unset | Custom step specification string for `CUSTOM` (implies `CUSTOM` and auto-starts when `scenario` is omitted); single-step shorthand extras `duration` (default `5.0`), `cpu_ms` (default `0`), and `gpu_ms` (default `0`) are also accepted. |
| `overlay`, `show_overlay` | `!auto_exit` | Enables or disables the on-screen telemetry HUD overlay (`true` unless `auto_exit` is set). |
| `run_idx`, `run_iteration` | `1` | Run iteration index (`>= 1`) recorded in the `SwappyTest::Run` and `SwappyTest::Settings` trace markers. |
| `manual` | `false` | Loads the scenario but waits for the Start button, and shows the results screen instead of exiting. |
| `auto_exit` | `false` | Exits after the run even when launched without a `scenario`. |

```bash
adb shell am start -n com.samples.swappytest/.SwappyTestActivity -e scenario S1 -e pacing classic
```
