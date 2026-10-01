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
The supported game build is 17055427. From `_VRDEV/EDF6VR`, the normal build is
`build.cmd`. The lens override can be generated separately:

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

`EDF6VRScopeZoomTests` uses a D3D WARP gradient image to check the inner sampling
scale and absence of writes outside the aperture. It does not test the actual
weapon pose, binocular alignment, or performance. Relevant test commands are:

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
- The complete build was previously blocked by C4189 (an unused variable
  warning treated as an error) in `cockpit_heli_tests.inc`. This draft does not
  include a fix for that file.

Only source code is provided. Generated RABs, DLLs, captures, and other game
assets are not part of this reference.
