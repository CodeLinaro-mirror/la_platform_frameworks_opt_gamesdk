# SwappyTest Application

An OpenGL ES 2.0 (`SwappyGL`) and Vulkan (`SwappyVk`) frame-pacing benchmark and test application for evaluating Android Game SDK (`Swappy`) pacing algorithms, auto-swap-interval transitions, pipeline modes, buffer-stuffing recovery, and interaction with CPU/GPU dynamic voltage and frequency scaling (DVFS) and scheduler core migration using calibrated synthetic workloads.

---

## Background & Evaluation Goals

SwappyTest drives deterministic CPU and fragment-shader GPU workloads calibrated to hardware
execution rates so frame-pacing behavior can be measured reproducibly across devices and graphics
APIs:

1. **Frame Pacing, Pipeline Mode & Buffer-Stuffing Evaluation:**
   Measures frame delivery stability, deadline misses, swap-interval ladder transitions (`S2`
   ramp-up, `S4` in-budget frame-to-frame variation), explicit frame-rate caps (`S5`/`H7`),
   mid-run `resetFramePacing()` recovery (`S10`), pipeline mode (`on`/`off`/`auto`), and
   buffer-stuffing mitigation (the `pipeline` and `stuff_fix` intent extras).
2. **DVFS Downclocking & 30 FPS Lock-In Reproduction (`S1`, `S7`):**
   - **Downshift to 30 FPS:** When per-frame CPU or GPU workload exceeds the 60 FPS budget (`16.67 ms`, such as a `27 ms` spike), Swappy's auto-swap-interval ladder drops the target rate to 30 FPS (`33.33 ms` budget).
   - **Governor Downclocking & Core Migration:** Because the CPU or GPU has significant idle time in each `33.33 ms` window while Swappy sleeps to pace presentation, DVFS governors scale down clock frequencies and/or the scheduler migrates the render thread from prime/big cores to medium or little cores.
   - **Lock-In at 30 FPS:** When the app's true workload drops back below the 60 FPS budget (e.g. `14 ms -> 4 ms`), the same work takes longer in wall-clock time at the lower clock frequency. If the pacer judges readiness from unscaled wall-clock duration alone, it remains stuck at 30 FPS even though the workload could sustain 60 FPS at higher clocks.
3. **False-Upshift ("Honeytrap") & Bottleneck-Transition Resilience (`H1`–`H10`):**
   Verifies that upshift heuristics and recovery probes do not trigger thrashing or false 60 FPS
   upshifts under sustained heavy loads (`H1`, `S11`), marginal loads around the 60 FPS threshold
   (`H2`), periodic square-wave spikes (`H3`/`S3`), high frame-to-frame variation (`H4`),
   GPU-bound passes (`H6`), idle loads (`H8`), CPU-to-GPU bottleneck flips (`H9`), or external
   big-core frequency boosts (`H10`).

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
| `S4` | In-budget variation: a fixed 30-frame pattern of 6-14 ms CPU loads (`6, 7, 13, 10, 14, 14, 7, 8, 6, 6, 9, 6, 8, 12, 9, 7, 14, 12, 11, 6, 7, 10, 12, 10, 8, 7, 12, 10, 14, 7`), looped for 30 s (1 step). | 30 s |
| `S5`, `H7` | Explicit 30 FPS request with an 8 ms CPU load (1 step). | 10 s |
| `S7` | GPU-bound ramp-down through the `S1` targets as fragment shader time, with a 1 ms CPU load (13 steps). | 39 s |
| `S10` | 27 ms for 3 s, 12 ms for 6 s, 27 ms for 3 s, then 12 ms for 6 s after `SwappyGL_resetFramePacing()` / `SwappyVk_resetFramePacing()` (4 steps). | 18 s |
| `S11` | Constant 27 ms CPU soak (1 step), run only on request. | 600 s |
| `H1` | Constant 27 ms CPU load (1 step). | 36 s |
| `H2` | 18 ms for 24 s, then 14 ms for 24 s (2 steps). | 48 s |
| `H3` | Square wave: 27 ms and 10 ms for 5 s each, four periods, enough for the projected ladder's first three upshift attempts. | 40 s |
| `H4` | A fixed 30-frame pattern of 20-30 ms CPU loads (`26, 25, 27, 26, 30, 20, 24, 20, 29, 26, 25, 27, 25, 22, 25, 23, 28, 27, 27, 26, 23, 22, 20, 21, 29, 21, 26, 28, 24, 27`), looped for 36 s (1 step). | 36 s |
| `H6` | GPU-bound 25 ms fragment shader with a 1 ms CPU load (1 step). | 36 s |
| `H8` | Idle app: 1 ms CPU load (1 step). | 12 s |
| `H9` | Bottleneck flip: 24 ms CPU (0 ms GPU), then 24 ms GPU (1 ms CPU), then 10 ms GPU (1 ms CPU), 10 s each (3 steps). | 30 s |
| `H10` | 22 ms CPU load (six 6 s steps); every 6 s step opens with a 500 ms external big-core boost pulse. | 36 s |

### Scenarios

Each catalog scenario is built in C++ by its own function in `app/src/main/cpp/scenarios/`
(`S1.cpp` ... `H10.cpp`, declared in `Scenarios.h`); `scenarios/ScenarioCatalog.cpp` lists them in
UI order. A function chains `ScenarioBuilder` commands (`app/src/main/cpp/ScenarioBuilder.h`) into a
`Scenario` that `ScenarioEngine` (`app/src/main/cpp/ScenarioEngine.h`) plays one frame at a time:

```cpp
using namespace std::chrono_literals;
ScenarioBuilder scenario("S10", "S10: resetFramePacing() Recovery ...");
scenario.gpu(0ms);  // prelude: applies from the first step on
scenario.step("heavy CPU 27ms").cpu(27ms).frames_for(3s);
scenario.step("CPU 12ms after resetFramePacing")
        .reset_frame_pacing()
        .cpu(12ms)
        .frames_for(6s);
return scenario.build();
```

- `ScenarioBuilder(title, description)`: The scenario title and description.
- `step(name)`: Opens a step, prefixes `name` with its 1-based step number (`"<N>. <name>"`,
  e.g. `"1. heavy CPU 27ms"`), closing the previous step if one is open, and emits the
  `SwappyTest::Step` trace marker. A step lasts until the next `step()` or the end of the scenario
  and is as long as the frames it plays; there is no step-level duration, and a step must play at
  least one frame. Frames outside any step open an unnamed step (leaving `step_names` empty when
  `step()` is not used).
- Setters persist until changed, including across steps. A scenario starts with `cpu(0ms) gpu(0ms)`
  and, unless the run's options (intent extras, UI) set them otherwise, Swappy's stock settings:
  pipeline auto, auto swap interval on, stuffing fix 0 and no explicit FPS (Swappy's auto rate).
  - `cpu(load)`, `gpu(load)`: The calibrated CPU and GPU fragment-shader workload per frame
    (`std::chrono::nanoseconds`, `>= 0ns`).
  - `set_auto_pipeline_mode(mode)`, `set_swap_interval_ns(swap_ns)` (an explicit swap interval in
    nanoseconds via `setSwapIntervalNS`), `set_auto_swap_interval(enabled)`,
    `set_buffer_stuffing_fix_wait(n)` (`setBufferStuffingFixWait`): Swappy settings. They are
    applied at step start, so they must come before the first frame of their step (or in the
    prelude) and outside repeat blocks. The `SwappyTest::Step` marker reports the
    settings actually in effect at the step's start, i.e. the run's options (the `pipeline` /
    `stuff_fix` intent extras or the UI choice, the run's swap interval) with the scenario's
    settings applied so far (`pipeline=off|on|auto`, `fps=0` for the auto rate, `auto_swap=0|1`,
    `stuff_fix=N`).
- `reset_frame_pacing()`, `boost(duration)`: One-shot actions that fire with the next frame,
  wherever they appear: `resetFramePacing()` and an external big-core CPU boost pulse of `duration`
  (`std::chrono::milliseconds`, `> 0ms`).
- `frames(n)`, `frames_for(duration)`: Plays `n` frames (`n >= 1`, default 1) or frames for a
  duration (`> 0`) with the current `cpu` / `gpu`.
- `repeat(n)` / `repeat_for(duration)` ... `end_repeat()`, or `repeat(n, body)` /
  `repeat_for(duration, body)` with a lambda body: Repeats its body `n` times (`n >= 1`) or for a
  duration (`> 0`); blocks nest, and a body must play at least one frame. A time block's clock
  starts at its first frame and is checked before every frame: once it has expired the block ends
  at once, even mid-iteration (a frame that already started completes), together with any count
  block or frame count nested in it. Count blocks always finish whole iterations.

Each command sets what the following frames play (`gpu(10ms).frames(9)` then `gpu(24ms).frames()`);
nothing restores `cpu` or `gpu` afterwards. An invalid command (a zero or negative count or
duration, an unmatched or missing end, a step or repeat body without frames, a Swappy setting after
the first frame of its step or inside a repeat block, more than 32 steps) is a programmer error
that `build()` asserts on.

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
Boolean extras accept `--ez key true`, `-e key true`, `1`/`0`, or `on`/`off`; integer extras also
accept string values (`-e key <val>`).

| Extra | Default | Effect |
|---|---|---|
| `scenario` | `S1` | Scenario ID (`S1`-`S5`, `S7`, `S10`, `S11`, `H1`-`H4`, `H6`-`H10`; alias `SCENARIO`). Its presence starts the run on launch and exits the app when the scenario completes. |
| `api` | `gles` | Rendering backend (`gles` / `gl` / `opengl` / `0` or `vulkan` / `vk` / `1`; aliases `render_api`, `backend`). |
| `pacing` | `classic` | Pacing selection: `classic` Swappy pacing, or `no_swappy` (`noswappy`, `none`, `3`; or integer extra `pacing_strategy`) to present without Swappy as an unpaced baseline. |
| `calibrate` | unset | Runs calibrations instead of a scenario, replacing their caches and reports: a comma-separated list of `cpu`, `gles`, `vulkan`, `gpu` (the GPU calibration of `api`) and `all`. `true` (or `--ez calibrate true`) calibrates `cpu` and the GPU of `api`, as before. GPU calibrations of both APIs run one after the other, switching the render API. `recalibrate`, `calibration` and `force_calibration` are aliases. |
| `swappy` | `true` | `false` presents without Swappy (`eglSwapBuffers` on GLES, direct `vkQueuePresentKHR` on Vulkan) with no pacing. |
| `set_30_fps_limit` | `false` | Sets a 30 FPS swap interval floor. Auto swap interval stays on, so Swappy's ladder still runs and must not pace faster (implied by S5 and H7). |
| `pipeline`, `auto_pipeline` | `auto` | Swappy pipeline mode override (`auto` / `true` / `1`, `on`, or `off` / `false` / `0`). |
| `auto_swap_interval`, `auto_si` | `true` | Enables or disables Swappy's auto-swap-interval ladder (`setAutoSwapInterval`). |
| `stuff_fix`, `buffer_stuffing_fix` | unset | Overrides `setBufferStuffingFixWait` (`>= 0` frames). |
| `fence_timeout_ms` | unset | Overrides Swappy's fence timeout in milliseconds (`setFenceTimeoutNS`, `>= 0`). |
| `overlay`, `show_overlay` | `!auto_exit` | Enables or disables the on-screen telemetry HUD overlay (`true` unless `auto_exit` is set). |
| `run_idx`, `run_iteration` | `1` | Run iteration index (`>= 1`) recorded in the `SwappyTest::Run` and `SwappyTest::Settings` trace markers. |
| `auto_exit` | `false` | Exits after the run even when launched without a `scenario`. |

```bash
adb shell am start -n com.samples.swappytest/.SwappyTestActivity -e scenario S1 -e pacing classic
```
