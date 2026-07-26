# Input Activity for OBS

Input Activity is a macOS-only OBS plugin that visualizes keyboard and mouse activity captured from this computer.
It provides four independent source types:

- **Live Keys** (`input-activity-live-keys`) — recently pressed keys with configurable release fading.
- **Input Intensity** (`input-activity-input-intensity`) — rolling keyboard, click, action, key, button, and mouse-velocity metrics.
- **Mouse Activity** (`input-activity-mouse-activity`) — button state, cursor trail, display heatmap, and coordinates.
- **Input Statistics** (`input-activity-statistics`) — KPM, CPM, APM, totals, distance, and a reset hotkey.

## Install

Download an unsigned test artifact from CI, unpack it, and place `input-activity.plugin` in:

`~/Library/Application Support/obs-studio/plugins/`

Restart OBS after installing or replacing the bundle.

## Accessibility permission

macOS requires permission before OBS can receive global keyboard and mouse events. In **System Settings → Privacy & Security → Accessibility**, enable OBS, then restart OBS. If permission is missing, the plugin stays loaded and writes an actionable warning to the OBS log.

## Coexistence

Input Activity intentionally uses new source IDs, so it can be installed alongside `input-overlay`. Existing Input Overlay scenes are not migrated; add Input Activity sources fresh.

## Build

Requirements: Xcode, CMake 3.28+, and an internet connection for the OBS build dependencies on the first configure.

```sh
cmake --preset macos
cmake --build --preset macos
cmake --install build_macos --config RelWithDebInfo
```

The install step uses the template's default OBS plugin directory. To run formatting checks locally, use the repository formatter workflow tools or `clang-format` and `gersemi` compatible with the CI versions.

## License and attribution

Input Activity is GPL-2.0-only. Its activity visualizers and local capture implementation are derived from [Input Overlay](https://github.com/univrsal/input-overlay), with applicable source notices retained. The build and packaging structure is based on the [OBS Plugin Template](https://github.com/obsproject/obs-plugintemplate).
