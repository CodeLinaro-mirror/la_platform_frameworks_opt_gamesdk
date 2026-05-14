# Cube Sample Application

This is a sample Vulkan application demonstrating rendering functionality, display timing control, and frame pacing integration using the Android Frame Pacing Library (Swappy).

## Configuration Options

The application exposes multiple launch parameters to allow dynamically configuring frame pacing behavior without recompiling the code.

### Available Options

| Option | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `display_timing` | Boolean | `true` | Controls whether `VK_GOOGLE_display_timing` presentation adjustments are enabled. |
| `swappy` | Boolean | `false` | Enables the Android Frame Pacing (Swappy) pipeline for frame queuing and presentation. |
| `set_30_fps_limit` | Boolean | `true` | Enforces a target 30 FPS refresh duration multiplier on the render loop. |

---

## Launching on Android

When running on Android, these options can be passed as `Intent` extras when starting `CubeActivity`. Both boolean (`--ez`) and string (`--es`) extra types are fully supported.

### Example usage via ADB:

Launch the application with default frame pacing settings:
```bash
adb shell am start -n com.samples.cube/.CubeActivity
```

Explicitly toggle options (e.g., enable Swappy and disable the 30 FPS limit):
```bash
adb shell am start -n com.samples.cube/.CubeActivity \
  --ez swappy true \
  --ez set_30_fps_limit false \
  --ez display_timing true
```

---

## Launching Natively (Standalone / Desktop)

When built and executed as a standalone native application on supported platforms (Linux, macOS, Windows), options can be provided directly via command-line arguments:

```bash
./cube --swappy --set_30_fps_limit --display_timing
```

Additional diagnostic parameters supported natively include:
- `--validate`: Enables Vulkan validation layers.
- `--use_staging`: Utilizes a staging buffer for texture uploads.
- `--c <framecount>`: Limits execution to a specified number of frames.
