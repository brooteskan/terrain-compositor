# Ground-contact filtering at the camera horizon

The retained Vue terrain's ground-contact sampler used full camera-ray/ground-plane
intersection to choose texture gradients. This matches flat-ground filtering at
the contact, but its denominator is `position.z - camera.z`. On a hillside at eye
height the projected coordinates have a pole. Their derivatives select very coarse
texture mips, creating a horizontal colour stripe. The previous small-denominator
fallback does not make the surrounding gradients continuous.

Use the first-order continuation about `position.z == groundHeight` instead:

```
eyeHeight = camera.z - groundHeight
footprint = position.xy + (position.xy - camera.xy)
    * (position.z - groundHeight) / eyeHeight
```

At contact its value and first derivative equal the ray/plane projection, preserving
ground/mesh texture-footprint agreement. Away from contact the continuation stays
finite at eye height. The existing fallback handles a camera on the ground plane.
This coordinate is used only for gradients; sampled world coordinates, randomized
patch identities, patch weights, texture sources and material blend weights are
unchanged. The existing orthographic path remains unchanged.

The headless D3D11 fixture tests four 16:9 sizes (720p through 1440p) and 1x/2x/4x
MSAA. Its contact cases compare a flat plane with two intersecting slopes within
two centimetres of contact. Full ray/plane matching throughout a slope is not a
valid requirement because that continuation introduces the horizon pole.

The twelve added cases render a shallow hillside crossing eye height and use a
texture with distinct, uniform mip colours to measure a spurious stripe. With
the old production helper as a negative control, the new check fails on the first
case (stripe excess 0.407548). With the correction, all 24 contact and 12 horizon
cases pass. Contact error is at most 0.000390589 in the mip-coded output; horizon
excess is zero in this fixture, versus up to 0.507611 with full projection. These
numbers describe the diagnostic texture, not actual scene RGB differences.

The TG wrapper is `Gem/Tests/verify_ground_blend_footprint.py`; `--helper` accepts
a captured source helper for the negative control. The test extracts the actual
production function. The shared generic sampler and the TG managed copy carry
the same correction, with the previous managed-file hash accepted for upgrades.
`TestProjectSync.py` passes all four delivery/idempotence/conflict checks.

User captures show the hillside line with blowing sand disabled as well as with
it enabled. The sand ground check affects its own alpha and has no terrain depth
writes. This removes visible sand draws as the cause of that captured line.
After the corrected shaders were processed successfully and the game relaunched,
the user confirmed on 6 November 2026 that the long thin hillside line is gone in
Normal view. No post-correction capture was saved, so this result records the
user's gameplay inspection. Other locations are not certified by that check.
