# Layouts: IABLib renderer configurations for real rooms

`layouts/` makes IABLib renderer configurations (`.cfg`, the format of `configurations/`) from the measurements of a
room. It holds:

- `make_cinema_cfg.py`: the generator of `configurations/IAB_cinema_11x6.5.array.vbap.cfg`.
- `cubeangles.cpp`: a tool that gives the angles IABLib's own transform gives a place in the IAB unit cube. The
  generator calls it.
- `CMakeLists.txt`: builds `cubeangles` (added with `add_subdirectory` after IABLib by the top `CMakeLists.txt`).

## Usage

```
cmake --build <build> --target cubeangles
python3 -I layouts/make_cinema_cfg.py configurations/IAB_cinema_11x6.5.array.vbap.cfg <build>/layouts/cubeangles
```

The output is the same every run: the committed `.cfg` is what the generator gives.

## The cinema room

11 m from the screen to the back wall, 6.5 m wide, ceiling at 3.80 m. Coordinates in metres: x from the left wall,
y from the screen, z from the floor. 36 outputs:

| Speakers | Place |
|---|---|
| `L` `C` `R` | screen, z 2.00; L and R 1 m from the side walls, C in the middle |
| `LFE` | output 3, the `w` line |
| `LSS1`..`LSS7`, `RSS1`..`RSS7` | side walls, z 2.40, y 2 m to 11 m every 1.5 m |
| `LTS1`..`LTS7`, `RTS1`..`RTS7` | ceiling, z 3.80, x at 1/3 and 2/3 of the width, y 2 m to 11 m every 1.5 m |
| `LRS1` `LRS2` `RRS2` `RRS1` | back wall, z 2.40, centred in 4 equal parts of the width |

## Angles: IABLib's transform, not a seat

IABLib does not pan an object by the angle seen from a seat. It takes the object's place in the unit cube (x left to
right, y front to back, z floor to ceiling, each 0 to 1), turns it into an azimuth and an elevation with its
theatrical "shoebox" model (`IABTransform::TransformIABToSphericalVBAP`: a front corner at -30 and 30 degrees, the
side wall at -90 and 90, a back corner at -150 and 150, the top of the room at 90 degrees of elevation), and pans by
VBAP among the `.cfg` speakers by their `theta` and `phi`.

So the generator gives each speaker the angles of its own place in the cube: x / 6.5, y / 11, z 0 for the screen and
wall layer and 1 for the ceiling, through `cubeangles`. An object at a speaker's place then plays on that speaker
alone. Angles measured from a seat (tried first, from 2/3 of the length) do not match the transform: an object in a
front corner of the screen played on `LSS1`, 2 m down the side wall, instead of on `L`.

The screen, the side walls and the back wall share the elevation-0 layer (cube z 0), as in IABLib's own cinema
`.cfg` files: the cube puts z 0 at elevation 0, so the speakers that cover it must be there.

## Bed channels: the whole zone

A bed channel whose URI belongs to a physical speaker plays on that speaker (`L`, `C`, `R`, `LFE`). The surround and
top-surround bed channels play on their whole zone, as in a cinema, through virtual speakers (channel `*`) that carry
the channel's URI and a downmix map (`m` line) to the zone:

| Bed channel | Virtual speaker | Zone |
|---|---|---|
| Lss / Rss | `VLSS` / `VRSS` | the 7 speakers of that side wall |
| Lrs / Rrs | `VLRS` / `VRRS` | the 2 back-wall speakers of that side |
| Ls / Rs (5.1) | `VLS` / `VRS` | the side wall and the 2 back-wall speakers of that side (9) |
| Lts / Rts | `VLTS` / `VRTS` | the ceiling line of that side (7) |

IABLib looks for a physical speaker with the channel's URI first and then for a virtual one
(`IABRenderer::RenderIABChannel`), and normalises each map to unit power (`RenderSpeaker::normalizeDownMixValues`,
the L2 norm): every map coefficient is written as 1.0, so each of n speakers gets 1 / sqrt(n). The virtual speakers
are in no VBAP patch, so objects never pan to them.

## VBAP patches

The `p` lines are the convex hull of the directions of the physical speakers (LFE out), without the floor face (the
triangles whose three speakers are all at elevation 0: they would close the hull under the listener). Where four
speakers lie on one plane (a quad of the two ceiling lines, by symmetry) both triangulations are listed, as IABLib
asks ("quads are no longer supported - use explicit overlapping triangles instead"). 61 patches.

## Verification

Rendered with IABLib's renderer (`IABRendererMT`) on the generated file, a 0.5 sine for three frames:

- Every bed channel and every object position tried renders at unit power (1.004 of the input's energy, the same as
  IABLib's own `IAB_c7.1.4.vbap.cfg`).
- Bed `L`, `C`, `LFE`: on their speaker alone. Lss: the 7 left-wall speakers at -8.5 dB each. Lrs: the 2 left
  back-wall speakers at -3.0 dB. Ls: those 9 at -9.5 dB. Lts and Rts: their ceiling line at -8.5 dB.
- Objects at the places of `L`, `LSS4`, `LTS4`, `LRS1`, `RTS1` and `RSS3`: on that speaker alone (0.0 dB).
- Object at the front-left corner of the floor (0, 0, 0): `L` -0.7 dB and `LSS1` -8.2 dB; centre of the screen: `C`
  alone; back-left corner: `LSS7` (the last side speaker is at the back wall); top centre: the four ceiling speakers
  around it.

## Another room

Copy `make_cinema_cfg.py` and change the measurements (`W`, `LEN`, `LAYER`, `CEIL`, the speaker list and the zones of
the virtual speakers). The rules above hold for any room: angles through `cubeangles`, wall speakers at cube z 0,
ceiling at cube z 1, bed zones as virtual speakers with maps, patches from the hull without the floor face.
