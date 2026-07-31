# League of Legends HUD references

These lossless, full-desktop captures are calibration references for issue
[#30](https://github.com/brendanwilliam/input-activity-obs/issues/30), not
runtime assets. Do not resize, crop, or optimize the originals: their pixel
coordinates are the source of truth for deriving and validating layout-safe
regions. The implementation contract is in
[`docs/league-of-legends-layout.md`](../../../docs/league-of-legends-layout.md).

All captures use a 3440x1440 desktop canvas.

| File | League game region in desktop coordinates | HUD scale |
| --- | --- | --- |
| `ultrawide-3440x1440-hud-min.png` | `x=0, y=0, width=3440, height=1440` | Minimum |
| `ultrawide-3440x1440-hud-max.png` | `x=0, y=0, width=3440, height=1440` | Maximum |
| `right-aligned-16x9-2560x1440-hud-min.png` | `x=880, y=0, width=2560, height=1440` | Minimum |
| `right-aligned-16x9-2560x1440-hud-max.png` | `x=880, y=0, width=2560, height=1440` | Maximum |

The 16:9 references intentionally retain the desktop content to the left of
the game. This represents the intended placement convention: a 2560x1440
League window occupies the right-hand portion of a 3440x1440 display. Layout
calculations must use the game-region coordinates above, then normalize into
that region rather than treating the desktop canvas as the game frame.

The labels "minimum" and "maximum" record the HUD scale state supplied with
the capture. They are useful for visual comparison, but `GlobalScale` is not
an input to the persistent-HUD exclusion model.

## Isolated calibration captures

The captures in `calibration/` each change one geometry-relevant setting from
the same right-aligned 2560x1440 League game region. Their paired `.cfg`
files remain local because they include unrelated player key bindings; the
table records every value relevant to layout.

| File | Changed setting | Relevant settings |
| --- | --- | --- |
| `calibration/chat-scale-100.png` | Maximum chat size | `ChatScale=100`, `MinimapScale=1.62`, team frames right |
| `calibration/chat-scale-0.png` | Minimum chat size | `ChatScale=0`, `MinimapScale=1.62`, team frames right |
| `calibration/minimap-scale-3.png` | Maximum minimap size | `MinimapScale=3.00`, `ChatScale=27`, team frames right |
| `calibration/minimap-scale-0.png` | Minimum minimap size | `MinimapScale=0.00`, `ChatScale=27`, team frames right |
| `calibration/team-frames-left.png` | Team-frame side | `ShowTeamFramesOnLeft=1`, `MinimapScale=1.23`, `ChatScale=27` |

All five captures use `Width=2560`, `Height=1440`, `WindowMode=2`,
`MinimapMoveSelf=1`, and `FlipMiniMap=0`. `FlipMiniMap=1` is implemented by
horizontally mirroring the measured minimap rectangle; it does not require a
separate geometry profile.
