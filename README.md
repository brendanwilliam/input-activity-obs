# Input Activity for OBS

[![Build macOS plugin](https://github.com/brendanwilliam/input-activity-obs/actions/workflows/build-project.yaml/badge.svg)](https://github.com/brendanwilliam/input-activity-obs/actions/workflows/build-project.yaml)
[![Static security](https://github.com/brendanwilliam/input-activity-obs/actions/workflows/security.yaml/badge.svg)](https://github.com/brendanwilliam/input-activity-obs/actions/workflows/security.yaml)
[![Release macOS plugin](https://github.com/brendanwilliam/input-activity-obs/actions/workflows/release.yaml/badge.svg)](https://github.com/brendanwilliam/input-activity-obs/actions/workflows/release.yaml)

Input Activity is a macOS-only OBS plugin that visualizes keyboard and mouse activity captured from this computer.
It provides four independent source types:

- **Live Keys** (`input-activity-live-keys`) — recently pressed keys with configurable release fading.
- **Input Intensity** (`input-activity-input-intensity`) — rolling keyboard, click, action, key, button, and mouse-velocity metrics.
- **Mouse Activity** (`input-activity-mouse-activity`) — button state, cursor trail, display heatmap, and coordinates.
- **Input Statistics** (`input-activity-statistics`) — KPM, CPM, APM, session and lap totals, distance, and shared Reset/Lap hotkeys.

Every source can be scoped to all input, a display, a running application, or a running window. Sources paused by an
unavailable target retain their current state until that target returns or their target is changed.

## Install

### Stable releases

Download the signed `.pkg` from the [GitHub Releases](https://github.com/brendanwilliam/input-activity-obs/releases) page and open it. The installer places the plugin in your user OBS plugins directory. Releases target macOS 26.5 or later and contain a universal Apple Silicon/Intel bundle.

Before opening an asset, compare its SHA-256 value with the accompanying `SHA256SUMS.txt`. The package is signed with the project's Developer ID and notarized by Apple; verify it with:

```sh
pkgutil --check-signature input-activity.pkg
spctl --assess --type install --verbose input-activity.pkg
```

### Release candidates and development builds

Release candidates are marked **Pre-release** on GitHub Releases and are intended for testing. Development CI artifacts are unsigned and short-lived; unpack one and place `input-activity.plugin` in:

`~/Library/Application Support/obs-studio/plugins/`

Restart OBS after installing or replacing the bundle.

| Channel | macOS | Signing | Intended use |
| --- | --- | --- | --- |
| Stable | 26.5+ | Developer ID signed and notarized | General use |
| Release candidate | 26.5+ | Developer ID signed and notarized | Release validation |
| CI artifact | 26.5+ | Unsigned | Development only |

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

## Contributing

Changes flow from `feature/<title>`, `fix/<title>`, or `chore/<title>` branches into the protected `main` branch through pull requests. See [CONTRIBUTING.md](CONTRIBUTING.md) for the full workflow and [AGENTS.md](AGENTS.md) for repository automation guidance.

## License and attribution

Input Activity is GPL-2.0-only. Its activity visualizers and local capture implementation are derived from [Input Overlay](https://github.com/univrsal/input-overlay), with applicable source notices retained. The build and packaging structure is based on the [OBS Plugin Template](https://github.com/obsproject/obs-plugintemplate).
