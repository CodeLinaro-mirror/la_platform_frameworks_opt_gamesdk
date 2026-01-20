# TuningForkMonitor

TuningForkMonitor is a companion application for the Android Performance Tuner (also known as
Tuning Fork) within the Android Game SDK. Its primary role is to assist developers in verifying that
their games or applications are correctly sending performance telemetry data during local
development.

Instead of uploading data to Google Play servers, this monitor app acts as a local endpoint,
allowing real-time inspection of performance metrics directly on the development device.

## How to Build and Install

First, clone the Android Games SDK:
```bash
git clone https://android.googlesource.com/platform/frameworks/opt/gamesdk
```
There are two ways to build and install the TuningForkMonitor app:

**1. Using Android Studio**:
- Open gamesdk/games-performance-tuner/tools/TuningForMonitor in Android Studio.
- Allow Android Studio to sync the project and resolve dependencies.
- With a connected Android device (either physical or virtual), click "Run 'app'", or press Shift+F10.

**2. Using Gradle (Command Line)**
- Navigate to the gamesdk/games-performance-tuner/tools/TuningForkMonitor directory in your terminal.
- With a connected Android device (either physical or virtual), run the Gradle build command:
  ```bash
  ./gradlew installDebug
  ```
  (or the appropriate gradle wrapper command if not in a UNIX environment)
- Open the app on your device.