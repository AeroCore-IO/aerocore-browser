# Readability Scaling Principles

Automatic scaling is designed around where and how a display is used. Handheld devices are viewed close up and have high pixel density, so the interface should stay compact. Living-room displays are viewed from a couch, so controls and page content need larger presentation even when the display reports a high resolution. Resolution alone is not a reliable measure of readable size.

## Design assumptions

- Treat the Steam Deck OLED (1280×800, 7.4 inches) as the handheld reference.
- Treat a 4K display at 45.88 inches as the living-room viewing anchor. At 3840×2160 this is approximately 96 DPI, close to half the handheld reference density.
- Use screen class and session context to distinguish handheld use from TV use. Living-room sessions may expose inaccurate monitor-sized EDID data; a sub-32-inch reported diagonal in a TV session is treated as unknown rather than evidence that the user is sitting close to a monitor.
- Use resolution tier to tune readability targets, then use reported diagonal to interpolate between small-screen, couch-anchor, and large-screen targets. Unknown diagonal falls back to the couch anchor.
- Keep three controls independent: application UI scale affects native browser controls and HUD; page zoom affects web content; minimum font size sets a legibility floor for small page text.

## Runtime targets

The current implementation in `src/siteconfigmanager.cpp` uses these living-room targets by resolution tier. Values are ordered as application UI scale, page zoom, and minimum font size.

| Resolution tier | Small-screen floor (32 in) | Couch anchor (45.88 in) | Large-screen target (85 in) |
| --- | --- | --- | --- |
| 720p | 1.55× / 1.46× / 20 px | 1.95× / 1.74× / 22 px | 2.08× / 1.82× / 23 px |
| 1080p | 1.60× / 1.50× / 20 px | 2.00× / 1.78× / 22 px | 2.14× / 1.86× / 23 px |
| 1440p | 1.68× / 1.56× / 20 px | 2.08× / 1.82× / 22 px | 2.21× / 1.90× / 24 px |
| 4K | 1.80× / 1.63× / 21 px | 2.20× / 1.90× / 23 px | 2.35× / 2.00× / 26 px |

Values are interpolated smoothly from 32 inches to 45.88 inches, and from 45.88 inches to 85 inches. The progress curves use small exponents (1.05 and 1.10) to keep the response gradual. Beyond the endpoints, values stay at the corresponding endpoint. For handheld displays, the 7.4-inch baseline is 1.00× / 1.10× / 17 px, interpolating toward 1.12× / 1.18× / 18 px at 11 inches.

## How to choose scaling ratios

Choose the viewing scenarios first, then assign output targets at each scenario. The small-screen floor represents a relatively close viewing distance; the couch anchor represents the intended default TV experience; the large-screen target accounts for a larger physical panel without letting scale grow without bound. Calibrate the three outputs separately against real devices: native UI controls should remain easy to navigate, page content should be readable from the couch, and minimum font size should improve small text without making already-large text excessive.

Resolution tier adjusts the targets because equal physical screen sizes at different resolutions produce different pixel density and content detail. Diagonal selects the position along that tier's curve. This keeps 4K content from being scaled solely according to its pixel count while still allowing it to receive somewhat larger readability targets than 720p content.

When changing the policy, update the runtime targets and interpolation together, then inspect representative handheld, 720p, 1080p, 1440p, and 4K displays at small, anchor, and large diagonals. Check both controller navigation and real web pages; a comfortable native interface does not guarantee comfortable page typography.

## Calculation helpers

Three standalone Python scripts in this directory support calibration: `calculate.py` prints a combined curve and matrix, `calculator.py` prints an alternative tier-independent matrix, and `compare.py` compares physical dimensions and DPI for 45.88-inch and 92-inch 4K displays. Run them with `python3 tools/scale-calculator/<script>.py`. Their formulas are exploratory and do not define runtime behavior; `src/siteconfigmanager.cpp` is the source of truth.
