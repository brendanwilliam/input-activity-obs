# League of Legends HUD references

These lossless, full-desktop captures are calibration references for issue
[#30](https://github.com/brendanwilliam/input-activity-obs/issues/30), not
runtime assets. Do not resize, crop, or optimize the originals: their pixel
coordinates are the source of truth for deriving and validating layout-safe
regions.

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
the capture. A future calibration pass should record the corresponding exact
`game.cfg` values alongside derived exclusion rectangles.
