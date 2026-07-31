# League of Legends HUD layout

This document defines the configuration-driven placement contract for issue
[#30](https://github.com/brendanwilliam/input-activity-obs/issues/30). It is
the implementation authority for a managed OBS layout that keeps Input
Activity visualizations out of League of Legends' persistent HUD.

The calibration images and their settings are in
[`references/league-of-legends/hud`](../references/league-of-legends/hud/).

## Scope

The layout engine only avoids persistent HUD. It does not reserve space for
temporary gameplay content such as the shop, expanded scoreboard, quest
prompts, death recap, or floating combat text.

`GlobalScale` is intentionally excluded. It does not move or resize the
persistent UI regions used for placement.

## Coordinate spaces

All layout calculations use the League game frame, never the whole desktop or
an OBS canvas. Convert every source rectangle into normalized game-frame
coordinates before calculating placement:

```text
normalized_x = (desktop_x - game_left) / game_width
normalized_y = (desktop_y - game_top) / game_height
```

For the supplied 16:9 calibration captures, the 2560x1440 game frame is
right-aligned within the 3440x1440 desktop at `(880, 0)`. The left 880 pixels
are not part of the game frame and must not influence any exclusion or OBS
scene-item transform.

The engine must obtain the game-frame bounds from the capture/window selected
by the user. It must not assume a right-aligned window in general.

## Inputs

Read only these fields from the `[General]` and `[HUD]` sections of
`game.cfg`:

| Field | Use |
| --- | --- |
| `Width`, `Height`, `WindowMode` | Identify the configured game frame and select calibration data. |
| `MinimapScale` | Size the lower-corner minimap exclusion. |
| `FlipMiniMap` | Mirror the minimap exclusion from right to left. |
| `ChatScale` | Size the lower-left chat exclusion. |
| `ShowTeamFramesOnLeft` | Place the team-frame exclusion on the left; otherwise place it on the right. |

Do not read, persist, or log key bindings from `input.ini` as part of this
feature. They do not determine HUD geometry.

## Exclusion regions

Represent each exclusion as a normalized half-open rectangle
`[left, top, right, bottom)`. The layout engine creates the union of these
rectangles and subtracts it from the full game frame.

| Region | Anchor | Inputs | Rule |
| --- | --- | --- | --- |
| Player HUD | Bottom center | None | A fixed, calibrated rectangle. Do not scale it with `GlobalScale`. |
| Minimap | Bottom right by default | `MinimapScale`, `FlipMiniMap` | Interpolate its calibrated size from `MinimapScale`; mirror horizontally when flipped. Reserve associated persistent minimap controls as part of the same rectangle. |
| Chat | Bottom left | `ChatScale` | Interpolate its calibrated size from `ChatScale`. Use the expanded-chat envelope from the references. |
| Team frames | Left or right | `ShowTeamFramesOnLeft` | Use the calibrated vertical frame strip, mirrored to the selected side. |
| Scoreboard margin | Top center | None | A fixed conservative margin for the persistent score display. |
| Toolbar margin | Left edge | None | A fixed conservative margin for the persistent left toolbar. |

When `FlipMiniMap=1`, the minimap moves to the lower-left and can overlap the
chat exclusion. This is expected: take the union of the two rectangles rather
than selecting one over the other.

## Placement and refresh

1. Build the exclusions from the current configuration.
2. Subtract them from the game frame and rank the remaining rectangles by
   whether they can contain the requested source size, then by usable area.
3. Place only Input Activity scene items explicitly marked as managed by this
   layout feature. Never move an unrelated user item.
4. Store a fingerprint of all layout inputs and the game-frame bounds.
5. When the fingerprint changes, recompute the proposal. Present an Apply
   Layout action by default; an eventual automatic refresh may update only
   unchanged managed items.

The source must retain a manual placement escape hatch. A generated layout is
an initial placement and a refreshable recommendation, not an ownership claim
over the user's scene.

## Calibration and tests

Before implementation, measure the persistent bounds in the reference images
and add them to a versioned calibration table. Tests should cover:

- chat minimum and maximum;
- minimap minimum and maximum;
- both minimap sides through horizontal mirroring;
- team frames on both sides;
- the union of chat and a left-side minimap; and
- a 16:9 game frame embedded in a wider desktop capture.

Use the same normalized values for a 16:9 or ultrawide game frame. New game
patches or materially changed HUD artwork require new captures and a review
of this document before changing the calibration table.
