# Changelog

All notable changes to this project are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- A League Safe Area OBS source that reads `game.cfg` and renders placement-safe regions around persistent League HUD elements.
- A unified `Input Activity` OBS source with Live Keys, Mouse Activity, Input Intensity, and Input Statistics modes. Its shared appearance settings include a transparent-by-default background color.
- Separate Live Keys sizing for alphanumeric and special-key labels.
- A configurable Live Keys most-used bar chart alongside the live key row.
- Repository governance, release automation, and contributor guidance.
- Input targeting by display, application, or window.
- Shared Reset and Input Statistics Lap hotkeys, with optional lap metrics.
- Live Keys most-used mode, count and key font sizing, and configurable element spacing.
- Configurable metric rows and element spacing for Input Intensity.
- Configurable element spacing for Input Statistics.
- Mouse Activity heatmap controls for hexbin size and opacity, plus hotkey-driven PNG or SVG exports.
- Titled Input Intensity indicators with per-indicator keyboard scopes for all keys, letters, numbers, or a custom key list.
- Mouse Activity display toggles for the title, hexbin chart, live mouse trail, distance, and coordinates, with configurable pixel, metric, or imperial distance units.
- Mouse Activity custom tracking rectangles using inclusive desktop-coordinate corners.

### Changed

- Input capture now queues raw events immediately and routes them on OBS video ticks, so adding source instances does not multiply capture work and the macOS input callback avoids application, window, display, and model processing.
- Live Keys now has clearer alignment and inversion behavior, consistent special-key indicators, and improved most-used key bars. Input Intensity box plots have more legible current-value markers, and Mouse Activity defaults to 720×560.
- Preference panels now use clear characteristic headings, retire unused defaults with legacy fallbacks, and hide Mouse Activity and Live Keys options until their parent feature is enabled.
- Rendering now falls back to Silom Regular when no explicit font is saved; target selectors adapt to the selected input target; Mouse Activity click labels use compact text-measured bounds; and Live Keys uses compact canonical key labels.
- New sources default to 20px padding, 10px element spacing, and a row-oriented Live Keys layout.
- Mouse Activity now places distance above the chart and coordinates below it, with shared left/right alignment and automatic metric or imperial distance scaling. Input Statistics no longer presents mouse distance totals.
- Legacy individual OBS source registrations have been removed; create and configure `Input Activity` for every mode.
- Build, formatting, and security automation now validate pull requests and pushes to `main`.

## [1.0.0] - 2026-07-26

### Added

- Initial Input Activity macOS OBS plugin release.
