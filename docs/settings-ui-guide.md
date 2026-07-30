# Input Activity settings UI guide

Use this guide when adding, moving, or reviewing an OBS property. It defines a stable,
user-facing hierarchy for every Input Activity source mode while preserving existing setting keys
and saved-scene behavior.

## OBS implementation basis

The source properties UI is built with the [`obs_properties_t` API](https://docs.obsproject.com/reference-core-objects#properties-obs-properties-t).
Use `obs_properties_add_group()` for source-mode sections, modified callbacks to refresh dependent
controls, and `obs_property_set_visible()` only when a hidden control's saved value remains valid.
The repository builds against OBS Studio 31.1.1, so verify new API usage against that version's
headers and build artifacts.

## Canonical order

1. **Mode** — the unified source's mode selector is always first.
2. **Input target** — all input, display, application, or window; show only the selected target's
   value control.
3. **Layout & sizing** — width, height where supported, padding, alignment, ordering, and spacing.
4. **Typography** — font family, shared font size, section titles, and mode-specific text sizes.
5. **Colors & appearance** — text/background, theme, pressed-state, button, border, and heatmap
   colors.
6. **Content** — what the source displays or tracks: enabled metrics, labels, maps, and chart
   choices.
7. **Behavior & data** — durations, rolling windows, units, DPI, key scope, tracking-region
   details, and other processing options.
8. **Export & actions** — export destination/format and destructive or reset actions, last.

Skip headings that contain no applicable controls. Keep a mode-specific section's controls in this
order after the shared controls; do not duplicate shared target, layout, typography, or appearance
settings inside a mode section.

## Rules for individual controls

- Keep existing setting keys and defaults unless an explicit migration accompanies the change.
- Give each setting one owner: shared settings belong to `activity.*`; mode settings belong to their
  mode prefix.
- Put a parent toggle/list immediately before its dependent controls and refresh visibility through
  its modified callback. Hidden controls retain their values.
- Use concise labels that describe the visible outcome. Keep related controls adjacent (for example,
  title toggle, title text, then title size).
- Put controls that affect source dimensions near the top so they do not require scrolling past
  mode-specific details.
- Check every mode in OBS at a normal properties-window size after changing order or visibility.

## Source-mode application

| Mode | Content | Behavior & data | Export & actions |
| --- | --- | --- | --- |
| Live Keys | visible-key count, live/chart titles, chart choices | fade duration/curve | none |
| Mouse Activity | labels, live cursor, heatmap, clicks, distance | trail, gradient/map, region, units, DPI | export and clear heatmap |
| Input Intensity | enabled metric rows, titles, metric selectors | rolling window, layout, key scope/list, velocity units, DPI | none |
| Input Statistics | enabled statistic categories and lap metrics | alignment and spacing | shared reset/lap hotkeys remain outside the properties panel |
