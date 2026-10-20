# SwappyTest CUSTOM Scenarios

A CUSTOM run plays a scenario described in JSON and passed to the activity at
launch, with no rebuild. [`ScenarioParser`](app/src/main/cpp/ScenarioParser.h)
reads the spec with [`json11`](../../third_party/json11/) and turns it into
[`ScenarioBuilder`](app/src/main/cpp/ScenarioBuilder.h) calls, so a spec can
express everything a built-in scenario can.

```bash
adb shell "am start -W -n com.samples.swappytest/.SwappyTestActivity \
  -e api vulkan -e custom '{\"cpu\": 4, \"gpu\": 6, \"duration\": 5.0}'"
```

---

## Concepts

- A **scenario** is a title, a description and a list of **steps**. Each step
  is reported as a separate phase of the run.
- A step is an **action**: an object that changes settings or loads and plays
  frames. `actions` is a list of nested actions played in order, and `repeat`
  plays them several times or for a duration.
- **Loads persist.** `cpu` and `gpu` stay in effect for all following frames,
  across steps, until changed.
- **Swappy settings** (`swap_interval`, `auto_swap_interval`,
  `auto_pipeline_mode`, `buffer_stuffing_fix_wait`) apply with the first frame
  of a step, so they must come before that frame and outside `repeat` blocks.
- **Order inside one object.** JSON keys are unordered, so the fields of one
  object always run in this order: `name` (starts the step), Swappy settings,
  `cpu`/`gpu`, `reset_pacing`/`boost`, `frames`, `duration`, then `repeat` with
  its `actions`. Use `actions` when you need a different order.
- Step names are optional. Steps are numbered (`1. warmup`, `2.`, ...); a spec
  with a single unnamed step has no step names.

---

## Spec Shapes

| Shape | Example |
| :--- | :--- |
| Scenario object | `{"title": "MINE", "swap_interval": "60fps", "steps": [ ... ]}` |
| Array of steps | `[{"cpu": 5, "frames": 60}, {"cpu": 15, "frames": 60}]` |
| Single step | `{"cpu": 8, "duration": 5.0}` |
| Stream of objects | `{"cpu": 6, "frames": 1} {"cpu": 12, "frames": 1}` |

In a scenario object, every field except `title`, `description` and `steps`
is applied before the first step.

A stream is a sequence of objects separated by spaces or newlines (JSON Lines),
without an outer array. If its first object plays no frames (no `frames`,
`duration`, `repeat`, `actions` or `name`, nor their aliases), it is the header:
a scenario object without `steps`. Otherwise every object is a step.

Comments are `// ...` and `/* ... */`.

```json
// Header: title and settings for the whole run.
{"title": "STREAM_TEST", "swap_interval": "60fps"}

{"name": "idle", "cpu": 0, "duration": 1.0}
{"name": "spike", "cpu": 25, "gpu": 10, "frames": 60}
{"name": "cooldown", "cpu": 0, "gpu": 0, "frames": "500ms"} /* back to idle */
```

---

## Field Reference

Durations are numbers in the field's default unit or strings with a unit:
`"500ns"`, `"16.6ms"`, `"2s"`. `swap_interval` also accepts `"60fps"`.

### Scenario

| Field | Alias | Value | Meaning |
| :--- | :--- | :--- | :--- |
| `title` | `id`, `scenario` | string | Scenario title. Default `"CUSTOM"`. |
| `description` | | string | Scenario description. Default `"CUSTOM (<n> steps): <spec>"`. |
| `steps` | | array | The steps, in order. |

### Actions

| Field | Alias | Value | Meaning |
| :--- | :--- | :--- | :--- |
| `name` | `step` | string | Starts a new step with this name. |
| `cpu` | `cpu_ms` | duration, default ms | Calibrated CPU busy-wait per frame (`>= 0`). |
| `gpu` | `gpu_ms` | duration, default ms | Calibrated GPU fragment load per frame (`>= 0`). |
| `frames` | `f` | integer or duration string | Plays that many frames (`>= 1`), or frames for that long (`"2s"`). A fractional number is an error; use `duration`. |
| `duration` | `duration_s` | duration, default s | Plays frames for that long. |
| `reset_pacing` | | bool | Calls `resetFramePacing()` before the next frame. |
| `boost` | `boost_ms` | duration, default ms | External CPU boost pulse before the next frame (`>= 1 ms`). |
| `repeat` | | count, duration string, or object | Repeats `actions`; see [Repeats](#repeats). |
| `actions` | `ops` | array of actions | Plays nested actions in order. |
| `swap_interval` | | duration, default ns | Target swap interval: `"60fps"`, `"16.666ms"`, `16666666`. |
| `auto_swap_interval` | | bool or `"on"`/`"off"` | Swappy auto swap interval. |
| `auto_pipeline_mode` | | `"on"`, `"off"`, `"auto"` or bool | Swappy pipeline mode; `true` is `"auto"`, `false` is `"off"`. |
| `buffer_stuffing_fix_wait` | | integer `>= 0` | Frames to wait for the buffer stuffing fix. |

Giving a field and its alias in the same object (e.g. `cpu` and `cpu_ms`) is
an error, as is an unknown field.

---

## Repeats

`repeat` plays the object's `actions`:

- a number repeats them that many times: `{"repeat": 5, "actions": [...]}`;
- a duration string repeats them until the time is up, counted from the
  block's first frame: `{"repeat": "5s", "actions": [...]}`;
- the object form is equivalent: `{"repeat": {"count": 5, "actions": [...]}}`
  or `{"repeat": {"duration": "5s", "actions": [...]}}`.

A repeat body must play at least one frame. Repeats are actions, so they can be
mixed with other actions and nested:

```json
{"name": "bursts", "actions": [
  {"boost": 500, "cpu": 20, "frames": 10},
  {"repeat": "5s", "actions": [
    {"cpu": 16, "gpu": 20, "frames": 2},
    {"repeat": 3, "actions": [{"cpu": 4, "gpu": 4, "frames": 1}]}
  ]},
  {"cpu": 2, "frames": 1}
]}
```

---

## Example

```json
{
  "title": "ALL_FEATURES",
  "description": "Every CUSTOM scenario feature",
  // Applied before the first step.
  "auto_pipeline_mode": "off",
  "auto_swap_interval": false,
  "swap_interval": "60fps",
  "gpu": 2,
  "steps": [
    {"name": "warmup", "cpu": 0, "duration": 2.0},
    {
      "name": "spike",
      "swap_interval": "30fps",
      "buffer_stuffing_fix_wait": 1,
      "reset_pacing": true,
      "boost": 250,
      "cpu": 27,
      "frames": "3s"
    },
    {
      "name": "alternating",
      "actions": [
        {"repeat": 5, "actions": [{"cpu": 6, "frames": 1}, {"cpu": 12, "frames": 1}]},
        {"repeat": "2s", "actions": [{"gpu": 8, "frames": 1}]}
      ]
    },
    {"cpu": 0, "gpu": 0, "frames": "500ms"}
  ]
}
```

---

## Running

Pass the spec with `-e custom`. `adb shell` joins its arguments into one
command line for the device shell, so quote the whole `am start` command and
the spec inside it. For anything longer than one line, keep the spec in a file
and read it on the device, which keeps its newlines, quotes and comments:

```bash
adb push my_scenario.json /data/local/tmp/
adb shell 'am start -W -n com.samples.swappytest/.SwappyTestActivity \
  -e api vulkan -e custom "$(cat /data/local/tmp/my_scenario.json)"'
```

---

## Errors

A spec that cannot run fails the run with
`bad_custom_scenario:<where>: <message>`. `<where>` is `L<line>` for a JSON
syntax error and otherwise the JSON path of the offending object: `header`,
`steps[2]` or `steps[2].actions[1]` (`steps[i]` counts steps, not a stream
header). A spec without steps fails with `empty_custom_scenario`.
