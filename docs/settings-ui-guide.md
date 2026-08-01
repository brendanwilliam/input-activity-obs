# Input Activity settings UI guide

Use this guide when adding, moving, or reviewing an OBS property. Keep existing setting keys and
saved-scene behavior unless an explicit migration accompanies the change.

## Implementation basis

Build the UI with normal nested `obs_properties_add_group()` groups. OBS Studio 31.1.1 does not
provide collapsible property groups, so do not use checkable groups as disclosure controls or
informational text properties as pseudo-headings. Use modified callbacks for dependent controls;
hidden controls must retain valid saved values.

## Canonical hierarchy

1. **Mode** is the first control.
2. **General Settings** follows. Its nested sections are **Input target**, **Color theme**,
   **Layout & sizing**, and **Typography**. **Show advanced settings** follows the sections.
3. **Mode Settings** follows General. Exactly one normal mode group is visible: **Live Keys**, **Mouse Activity**,
   **Input Intensity**, or **Input Statistics**. Hide the other outer groups when Mode changes.
4. Mode sections are:
   - Live Keys: **Content**, **Layout & spacing**, **Typography**, **Colors & appearance**,
     **Behavior & data**.
   - Mouse Activity: **Content**, **Colors & appearance**, **Behavior & data**,
     **Export & actions**.
   - Input Intensity: **Metrics**, **Colors & appearance**, **Layout & spacing**,
     **Behavior & data**.
   - Input Statistics: **Content**, **Colors & appearance**, **Layout & spacing**,
     **Typography**, **Behavior & data**.

**Typography** has one shared set of sizes: **Title**, **Subtitle**, and **Text**. These are the only
font-size controls, including when advanced settings are shown. Their new-source defaults are 22px, 18px,
and 30px respectively.

Primary target controls, shared color theme, primary sizing, Content/Metrics controls stay visible
by default. The persisted shared UI-only **Show advanced settings** control reveals detailed
layout, typography, behavior/data, and export/action controls. It must not alter rendering, input
capture, saved mode settings, or hidden controls' values.

The shared **Alignment** setting controls horizontal indicator direction in every mode: right
alignment anchors bars and box plots on the right and makes them grow left. Section titles are
aligned to the selected edge. Live Keys uses the shared setting for its most-used chart, placing
its key label on the growth edge and count on the opposite edge. Top-key rows start at the chart's
top edge and use the configured fixed spacing. **Invert layout** moves the most-used chart above
live keys and orders its largest bar at the bottom.

## Rules

- Give each setting one owner: shared settings use `activity.*`; mode settings use their mode
  prefix.
- Place a parent toggle or list immediately before its dependent controls and refresh their
  visibility with its modified callback.
- Keep source IDs stable, preserve the Accessibility warning, and never log keystroke contents.
- Check all modes in OBS with advanced settings both off and on at a normal properties-window
  size after changing order or visibility.

For properties that depend on a selected metric or target, use normal `obs_properties_add_group()`
sections and modified callbacks. Hidden controls must retain their valid saved values.
