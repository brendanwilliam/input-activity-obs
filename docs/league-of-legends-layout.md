# League of Legends HUD layout

This document defines the configuration-driven overlay contract for issue
[#30](https://github.com/brendanwilliam/input-activity-obs/issues/30). It is
the implementation authority for the **League Safe Area** OBS source, which
shows where an Input Activity visualization can be placed without covering
League of Legends' persistent HUD.

The calibration images and their settings are in
[`references/league-of-legends/hud`](../references/league-of-legends/hud/).

## Scope

The source only avoids persistent HUD. It does not reserve space for temporary
gameplay content such as the shop, expanded scoreboard, quest prompts, death
recap, or floating combat text. It never creates, moves, or otherwise manages
OBS scene items: align its game-frame-sized canvas manually with the League
capture.

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

## Safe-area source

The registered source ID is `input-activity-league-safe-area`; the existing
`input-activity` source ID is unchanged. It starts transparent at `1×1` until
it accepts a selected `game.cfg`, then its dimensions are exactly `Width ×
Height` from that file. In particular, a right-aligned 2560×1440 League window
on a 3440×1440 desktop produces a 2560×1440 source.

Its properties store only the selected `game.cfg` path. The status text and
**Reload now** action do not save game settings. The source watches that file
and its parent directory, polls its modification time and size every 500 ms,
and debounces reloads by 250 ms. Invalid, incomplete, or temporarily missing
writes keep the last valid overlay visible and report the condition in status.
No game configuration contents or input bindings are logged.

## Exclusion regions and calibration

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

All values are normalized to the game frame and were measured from the
committed reference captures. Chat and minimap dimensions interpolate linearly
between their endpoints; their lower-corner anchors do not move.

| Region | Normalized calibrated bounds or endpoints |
| --- | --- |
| Player HUD | `[0.315, 0.755, 0.685, 1.000)` |
| Scoreboard margin | `[0.385, 0.000, 0.615, 0.075)` |
| Toolbar margin | `[0.000, 0.180, 0.052, 0.700)` |
| Team frames, right | `[0.875, 0.165, 0.985, 0.655)`; horizontally mirrored on the left |
| Chat, `0…100` | width `0.180…0.345`, height `0.155…0.355`, lower left |
| Minimap, `0…3` | width `0.140…0.275`, height `0.250…0.485`, lower right; mirror for `FlipMiniMap=1` |

When `FlipMiniMap=1`, the minimap moves to the lower-left and can overlap the
chat exclusion. This is expected: take the union of the two rectangles rather
than selecting one over the other.

The source takes the deterministic union of all exclusions and subtracts it
from the full game frame using normalized half-open rectangles. It renders only
the resulting safe rectangles as translucent green over a transparent canvas;
it does not fill excluded regions, render labels, or change scene positioning.

## Calibration and tests

Before implementation, measure the persistent bounds in the reference images
and add them to a versioned calibration table. Tests should cover:

- chat minimum and maximum;
- minimap minimum and maximum;
- both minimap sides through horizontal mirroring;
- team frames on both sides;
- the union of chat and a left-side minimap; and
- an invalid update retaining the previous valid model; and
- a 2560×1440 game frame embedded in a wider desktop capture.

Use the same normalized values for a 16:9 or ultrawide game frame. New game
patches or materially changed HUD artwork require new captures and a review
of this document before changing the calibration table.
