# Experimental SVU01 scope reference (DNM)

This is a reference implementation for discussing see-through sniper scopes in
VR. It is not ready to merge or include in a release. The zoom compositor matches
the `s_sniper_SVU01` model; in-game lens transparency was checked with the
Ranger's ターミガンHA1. Other definitions sharing this model remain untested.
For an eventual mod feature, the
request is to investigate applicability to each weapon in the Ranger sniper
rifle category.

An optional, geometry-only MMF01 (KFF-family) profile is included in the lens
patch tool. It has not been tried in-game and is not recognized by the zoom
compositor. See [the model inventory and additional example](sniper_scope_inventory.md)
for the inspected weapon/model mappings and its verification limits.

## Rendering approach

`plugin.cpp` identifies the held model and publishes points on its rear scope
aperture using the weapon transform. `scope_zoom.cpp` projects these points into
each eye's view and draws an ellipse into that eye's output image. Inside it,
the shader samples the existing world image at half the displacement from the
ellipse center, giving an intended 2x screen-space enlargement. The operation
runs after the stereo weapon composite and before the world UI. It uses the
existing eye images and does not render the scene again.

The current composite requires separate source/target textures of matching
dimensions, one sample, and one array slice. It skips apertures with a
degenerate projection, a radius below three pixels, or a projected horizontal
or vertical radius above one quarter of the image dimension. Thus bringing
the eye very close can disable the overlay. These guards are prototype
limits, not an optical eye-relief implementation.

## Build and try locally

Follow `source/BUILD.md` for the toolchain, dependencies, generated cockpit
materials, and placement of the source folders next to the installed game.
The supported game build is 17055427. The normal full build is `build.cmd`,
but it previously stopped at C4189 in `cockpit_heli_tests.inc`. To build only
this reference DLL and its three relevant tests, open **cmd.exe**, change to
`_VRDEV\EDF6VR`, and run:

```bat
call tools\toolchain\msvc-x64-env.cmd
set VSLANG=1033
python tools\prepare_native_cockpit_materials.py
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --target EDF6VR EDF6VRScopeZoomTests EDF6VRDisplayTests EDF6VRWeaponStereoTests
```

Run these commands in the same cmd.exe session: calling the environment batch
file from PowerShell does not initialize the parent PowerShell environment.
CMake and Ninja must be available on PATH, as required by `source/BUILD.md`.
If CMake or Ninja is absent from PATH, install the Python packages with
`python -m pip install cmake ninja`, then use this alternative in the same
cmd.exe session, after the environment and material preparation above:

```bat
for /f "delims=" %N in ('python -c "import ninja; print(ninja.BIN_DIR)"') do set "EDF6VR_NINJA=%N\ninja.exe"
python -m cmake -S . -B build-scope-reference -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_MAKE_PROGRAM="%EDF6VR_NINJA%"
python -m cmake --build build-scope-reference --target EDF6VR EDF6VRScopeZoomTests EDF6VRDisplayTests EDF6VRWeaponStereoTests
```

The `for` command uses `%N` at an interactive prompt; use `%%N` if saving it
in a batch file. A new build directory avoids reusing another generator's
cache. The DLL
is written to `dist/EDF6VR.dll`, and test executables to `build/`. This target
selection does not establish that the full build or test suite passes.
For the Python alternative, the executables are in `build-scope-reference/`;
substitute that directory in the test commands below.

The lens override can be generated separately:

```powershell
python tools\patch_svu01_scope.py "<game directory>" "<scratch directory>\S_SNIPER_SVU01.RAB"
```

The script reads `Root.cpk`, checks the original model's SHA-256 and topology,
and degenerates only the two 80-triangle lens islands. It rebuilds the RAB and
checks that the other entries are unchanged. Use a scratch output directory:
the script does not install the override or manage backups.

With the game closed, back up any existing `Mods/Plugins/EDF6VR.dll` and
`Mods/WEAPON/S_SNIPER_SVU01.RAB`. Copy `dist/EDF6VR.dll` into `Mods/Plugins` and
the generated RAB into `Mods/WEAPON`. Start VR, equip the ターミガンHA1, and try
looking through the scope at a building while moving the head and weapon.
Compare the lens interior with the surrounding view and check both eyes.
Restore the previous DLL and RAB to undo the experiment; if no RAB override
existed before, remove the generated override. The original `Root.cpk` stays
unchanged. An updater can overwrite the experimental DLL.

## Validation and open questions

On 2026-10-01, a new `build-scope-reference` directory was configured using
the Python CMake/Ninja alternative above (VS 2022 x64, RelWithDebInfo).
The DLL and all three selected test targets built successfully. Scope zoom,
display (19 projections), and weapon stereo tests each reported zero failures;
the weapon stereo test was given the installed build 17055427 `EDF.dll`.
The checked tool versions were Python 3.11.9, CMake 4.4.3, and Ninja
`1.13.2.git.kitware.jobserver-pipe-1`, with MSVC 19.37.32824.0.

Lens transparency has been confirmed in-game, and the logging counters confirm
that the composite pass executes. The visible scale and aperture alignment
still need verification; the counters alone do not establish that the intended
2x view is correctly displayed in the headset.

`EDF6VRScopeZoomTests` uses a D3D WARP red/green gradient image to check the
inner 2x sampling scale in both axes and preservation of existing pixels
outside the aperture. Its sample locations and tolerances distinguish 2x from
1x; a verification copy with the shader changed to 1x must fail. The test uses
UNORM images and does not test the actual weapon pose, binocular alignment,
color-space combinations, or performance. Relevant test commands are:

```powershell
.\build\EDF6VRScopeZoomTests.exe
.\build\EDF6VRDisplayTests.exe
.\build\EDF6VRWeaponStereoTests.exe ..\..\EDF.dll
```

The installed-data lens patch checks are separate from the C++ tests:

```powershell
python tests\lens_patch_tests.py "<game directory>"
```

They require the original `Root.cpk` from the supported game build. They run
the CLI's default SVU01 profile and optional MMF01 profile in a temporary
directory, check the expected RAB SHA-256, exact changed triangle counts
(160/42), unchanged other model bytes and RAB entries, and rejection of
modified/already-patched MDB input. Both profiles passed on 2026-10-01.
They do not install files or establish in-game appearance.

### Suggested headset checks

Keep native game zoom off for the first comparison. Use the same mission and
a distant building with recognizable edges. Record the active OpenXR runtime,
resolution settings, weapon, and which eye is captured.

| Check | Observation to record |
|---|---|
| Move the weapon into/out of view while keeping the head stationary | Compare the same building edge at a fixed camera pose, inside versus outside the aperture; the intended image displacement from the aperture center is 2x. |
| View with each eye, then both eyes | Whether the scene is enlarged in both eyes and can be fused comfortably. |
| Move the head sideways and change the eye-to-scope distance | Whether the projected aperture stays inside the housing; note when the size guards disable the overlay. |
| Fire, reload, and switch weapon | Whether the effect follows the held scope and disappears for an unrelated model. |
| Enable native game zoom separately | Its existing full-view zoom remains active; distinguish it from the local 2x effect. |
| Compare a stable scene with the stock DLL | Record frame/GPU times under matching settings; no performance result is claimed by the synthetic tests. |

`SCOPEMODEL` identifies a matching held resource. `SCOPEZOOM published` counts
pose publications and `composited` counts submitted composite calls, normally
for each eye. They do not measure visible magnification, image quality, or
GPU completion. A nonzero count alone is not a headset validation result.

Known limitations:

- Fixed resource offsets (`model + 0xA0`, resource name at `+0x78`), an exact
  model name, and hard-coded aperture coordinates are specific to this model
  and game build.
- The ellipse is not depth-tested against the scope housing. Physical lens
  occlusion, eye relief, and optical parallax are not modeled.
- The 2x effect resamples each existing eye image and cannot add source detail.
  It also changes the apparent disparity relative to each projected aperture;
  natural binocular alignment must be checked in the headset. It is not a
  separate zoom camera.
- Native game zoom still affects the whole view; there is no configuration
  toggle or automatic installation/restore workflow.
- Other weapons, left-handed use, dual wielding, runtime differences, and the
  performance impact have not been validated.
- A synthetic typed-sRGB input/output test changed the center byte from 128
  to 55 because of the current color conversion. This is accepted for this
  draft after the current in-game view was reported to look acceptable;
  correction of the color-space handling remains open.
- The complete build was previously blocked by C4189 (an unused variable
  warning treated as an error) in `cockpit_heli_tests.inc`. This draft does not
  include a fix for that file.

Only source code is provided. Generated RABs, DLLs, captures, and other game
assets are not part of this reference.
