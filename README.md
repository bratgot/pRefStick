# pRefStick

A Nuke NDK plugin that bakes a **frame-invariant position** into vertex colour
(`Cf`), so geometry with no UVs and no `Pref`/`rest` primvar can still drive a
position-reference reprojection. It is the front half of a "stick a texture to
tracked geometry" pipeline; the baked pass is rendered unlit and fed to
[PRefToMotion](https://github.com/masterkeech/PRefToMotion), which turns it into
an ST/UV map.

`pRefStick` is a classic `GeoOp` (subclass of `ModifyGeo`) and appears in the
node menu under **Geometry ▸ pRefStick**.

## Why

To reproject a texture onto moving geometry you need a per-pixel label that is the
*same value for the same surface point on every frame*. With UVs or an exported
`Pref` attribute that label already exists. Without them, there is nothing stable
to match against. `pRefStick` manufactures that label:

- **Rigid geometry** (motion lives in the object matrix): the object-space *local*
  points are already invariant, so it bakes those directly.
- **Point-baked / deforming geometry** (animated vertices): local points move every
  frame, so instead it transfers the positions from a **frozen reference frame**
  onto the live points by index, giving every vertex its source-frame position on
  all frames.

The result, rendered unlit, is a position pass that `PRefToMotion` can match
back to a source frame.

## Requirements

- Nuke 16.x (built against **Nuke 16.1**; should build for 14–16 with the matching
  toolset).
- **Windows:** Visual Studio 2019 (MSVC 19.29) — the toolset Foundry uses for
  Nuke 14–16.1.
- CMake 3.20+.

## Build

### Windows

```bat
cmake -G "Visual Studio 16 2019" -A x64 -DCMAKE_PREFIX_PATH="C:/Program Files/Nuke16.1v1" -B build
cmake --build build --config Release
```

### macOS

```bash
cmake -DCMAKE_PREFIX_PATH="/Applications/Nuke16.1v1/Nuke16.1v1.app/Contents/MacOS" -B build
cmake --build build --config Release
```

The build copies the plugin (`pRefStick.dll` / `.dylib`) into `~/.nuke`
(`%USERPROFILE%\.nuke` on Windows) so Nuke picks it up on next launch. The output
filename must stay `pRefStick` — it has to match the `Op::Description` name.

## Usage

Two geometry inputs:

| input | what to connect |
|-------|-----------------|
| `0` (unlabelled) | the **live** geometry — your `ReadGeo` |
| `1` (`ref`) | the **reference** geometry — `ReadGeo ▸ FrameHold` set to the source frame (optional) |

Knobs:

| knob | purpose |
|------|---------|
| **use reference** | On: transfer input 1's frozen positions onto the live geo by index (deforming geo). Off: bake live local points (rigid geo). |
| **colour attribute** | Vertex attribute to write. `Cf` is what ScanlineRender renders. |
| **offset** | Added to every baked position. Nudge off `0` if a surface point sits at local origin, since PRefToMotion treats `(0,0,0)` as background. A uniform offset does not affect the nearest-neighbour solve. |

A **Help** tab in the properties panel restates this inside Nuke.

### Full pipeline

```
ReadGeo ───────────────► pRefStick (input 0, live)
ReadGeo ► FrameHold ────► pRefStick (input 1, ref)     use reference: ON
(source frame)

pRefStick ► ScanlineRender (classic, live camera, UNLIT) ► PRef pass
          ► PRefToMotion (pref channels = RGB, source frame = FrameHold frame)
          ► STMap ► warps the source-frame texture so it sticks to the surface
```

Three frame numbers must agree: the **FrameHold** frame, the **PRefToMotion source
frame**, and the frame the **texture** is aligned to.

## Notes & limitations

- **Render unlit.** `Cf` passes through the shader; a light or colour shader scales
  it and corrupts the values. No lights, no diffuse — `Cf` must reach pixels 1:1.
- **Classic ScanlineRender only.** A classic `GeoOp` connects to the classic
  `ScanlineRender`, not the USD `ScanlineRender2`. Build the branch from
  **3D ▸ 3D Classic**.
- **Constant topology** for the reference transfer. The transfer is by point index,
  so it is valid for fixed-topology deformation (skin, blendshape, point cache) but
  not for geometry regenerated per frame (fluids, remeshing, procedural). For those,
  export a real `Pref` primvar from the DCC instead.
- **Co-visibility.** A single source frame only covers the surface visible at that
  frame; areas disoccluded later have no source pixel to sample and will smear.
- A position pass reads near-black in the Viewer, but the float data is intact —
  judge correctness by hue-invariance across frames, not by brightness.

## Credit

Created by **Marten Blumen**.

Downstream node [PRefToMotion](https://github.com/masterkeech/PRefToMotion) © 2020
masterkeech (MIT).

## License

© 2026 Marten Blumen. Add your preferred license here (MIT is a common choice for
NDK plugins) and include a matching `LICENSE` file.
