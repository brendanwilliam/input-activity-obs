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
| `ChatScale`, `ShowTeamFramesOnLeft` | Validate the selected `game.cfg`; the annotated target does not reserve a fixed chat or team-frame area. |

Do not read, persist, or log key bindings from `input.ini` as part of this
feature. They do not determine HUD geometry.

## Safe-area source

The registered source ID is `input-activity-league-safe-area`; the existing
`input-activity` source ID is unchanged. It starts transparent at `1×1` until
it accepts a selected `game.cfg`, then its dimensions are exactly `Width ×
Height` from that file. In particular, a right-aligned 2560×1440 League window
on a 3440×1440 desktop produces a 2560×1440 source.

Its properties store only the selected `game.cfg` path. **Auto-detect League
game.cfg** searches standard macOS app-bundle locations and Windows Riot Games
installation locations, including Riot Client metadata for custom-drive
installs, then accepts only a candidate with valid required HUD settings. A
manually selected path remains an override. The
status text and **Reload now** action do not save game settings. The source
watches that file and its parent directory, polls its modification time and
size every 500 ms, and debounces reloads by 250 ms. Invalid, incomplete, or
temporarily missing writes keep the last valid overlay visible and report the
condition in status. No game configuration contents or input bindings are
logged.

## Exclusion regions and calibration

Represent each exclusion as a normalized half-open rectangle
`[left, top, right, bottom)`. The layout engine creates the union of these
rectangles and subtracts it from the full game frame.

| Region | Anchor | Inputs | Rule |
| --- | --- | --- | --- |
| Player HUD | Bottom center | None | A fixed, tightly calibrated rectangle. |
| Minimap | Bottom right by default | `MinimapScale`, `FlipMiniMap` | Interpolate its measured size from `MinimapScale`; mirror horizontally when flipped. |
| Top-left reserve | Top left | None | Fixed reserve from the annotated game-frame capture. |
| Enemy info and death recap | Top right | None | Fixed reserve from the annotated game-frame capture. |

All values are normalized to the game frame and were measured from the
annotated 2560×1440 game-frame capture. Minimap dimensions interpolate
linearly between their endpoints; the lower-corner anchor does not move.

| Region | Normalized calibrated bounds or endpoints |
| --- | --- |
| Player HUD | `[0.277, 0.866, 0.660, 1.000)` |
| Top-left reserve | `[0.000, 0.000, 0.134, 0.107)` |
| Enemy info and death recap | `[0.796, 0.000, 1.000, 0.058)` |
| Minimap, `0…3` | width `0.120…0.208`, height `0.200…0.383`, lower right; mirror for `FlipMiniMap=1` |

When `FlipMiniMap=1`, the minimap moves to the lower-left. The source takes the
union of all exclusions, including any overlap with the fixed reserves.

The source takes the deterministic union of all exclusions and subtracts it
from the full game frame using normalized half-open rectangles. It renders only
the resulting safe rectangles as translucent green over a transparent canvas;
it does not fill excluded regions, render labels, or change scene positioning.

## Calibration and tests

Before implementation, measure the persistent bounds in the reference images
and add them to a versioned calibration table. Tests should cover:

- minimap minimum and maximum;
- both minimap sides through horizontal mirroring;
- the top-left and top-right measured reserves;
- an invalid update retaining the previous valid model; and
- a 2560×1440 game frame embedded in a wider desktop capture.

Use the same normalized values for a 16:9 or ultrawide game frame. New game
patches or materially changed HUD artwork require new captures and a review
of this document before changing the calibration table.
