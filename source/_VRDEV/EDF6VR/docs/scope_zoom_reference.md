# Experimental SVU01 scope reference (DNM)

This is a reference implementation for discussing see-through sniper scopes in
VR. It is not ready to merge or include in a release. The prototype targets the
Ranger's ターミガンHA1 (`s_sniper_SVU01`) only. For an eventual mod feature, the
request is to investigate applicability to each weapon in the Ranger sniper
rifle category.

## Rendering approach

`plugin.cpp` identifies the held model and publishes points on its rear scope
aperture using the weapon transform. `scope_zoom.cpp` projects these points into
each eye's view and draws an ellipse into that eye's output image. Inside it,
the shader samples the existing world image at half the displacement from the
ellipse center, giving an intended 2x screen-space enlargement. The operation
runs after the stereo weapon composite and before the world UI. It uses the
existing eye images and does not render the scene again.

## Build and try locally

Follow `source/BUILD.md` for the toolchain, dependencies, generated cockpit
materials, and placement of the source folders next to the installed game.
The supported game build is 17055427. The normal full build is `build.cmd`,
but it previously stopped at C4189 in `cockpit_heli_tests.inc`. To build only
this reference DLL and its three relevant tests, open **cmd.exe**, change to
`_VRDEV\EDF6VR`, and run:

```bat
call tools\toolchain\msvc-x64-env.cmd
python tools\prepare_native_cockpit_materials.py
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --target EDF6VR EDF6VRScopeZoomTests EDF6VRDisplayTests EDF6VRWeaponStereoTests
```

Run these commands in the same cmd.exe session: calling the environment batch
file from PowerShell does not initialize the parent PowerShell environment.
CMake and Ninja must be available on PATH, as required by `source/BUILD.md`.
If using Python-installed CMake, `python -m cmake` can replace `cmake`; Ninja
must still be discoverable (or supplied with `-DCMAKE_MAKE_PROGRAM`). The DLL
is written to `dist/EDF6VR.dll`, and test executables to `build/`. This target
selection does not establish that the full build or test suite passes.

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

Known limitations:

- Fixed resource offsets (`model + 0xA0`, resource name at `+0x78`), an exact
  model name, and hard-coded aperture coordinates are specific to this model
  and game build.
- The ellipse is not depth-tested against the scope housing. Physical lens
  occlusion, eye relief, and optical parallax are not modeled.
- The 2x effect is a resampling of an existing image, with its existing detail
  and stereoscopic disparity. It is not a separate zoom camera.
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
