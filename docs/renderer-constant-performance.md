# Shader constant upload specialization

September 18, 2026, following the Direct3D 12 framerate unlock.

`ShaderBindings` now dispatches on the reflected component count once per
value, outside the register loop. Comparisons and copies consequently have
compile-time sizes (4, 8, 12, or 16 bytes). The release assembly no longer
calls variable-length `memcmp`/`memcpy` inside full or partial register uploads.
Source bits, reflected padding, partial-update boundaries, and constant-version
tracking retain their existing behavior.

## Measurement

Fresh isolated Mission 1 runs used the same startup script, 1280x720 defaults,
120 FPS cap, framerate unlock, and full hook timings. Both reached the gameplay
scene (`0x82003fa4`). The report window is 199–241 game seconds, allowing for
integer timestamp rounding; each FPS result covers eight complete intervals,
40 seconds total. Hook costs are inclusive and must not be summed.

| Measurement | Before | After |
| --- | ---: | ---: |
| Vertex constant upload, microseconds/call | 0.775 | 0.648 |
| Pixel constant upload, microseconds/call | 0.745 | 0.706 |
| Instance constant patch, microseconds/call | 0.150 | 0.134 |
| Whole native material activation, microseconds/call | 2.946 | 2.715 |
| Game FPS | 63.225 | 64.700 |

This is one instrumented comparison: the roughly 2.3% FPS increase is indicative,
not a repeatability claim or an uninstrumented benchmark. Indexed draw cost was
essentially unchanged. The optimization reduces constant processing cost; it
does not remove the remaining render-helper bottleneck.

Local evidence:

- `out/renderer-performance-baseline-process.json` and
  `out/renderer-performance-after-process.json` identify the stopped runs.
- `out/renderer-performance-{baseline,after}-profile.json` contain the reports.
- `out/renderer-bindings-{before,after}.asm` contain the object disassembly.
- `out/renderer-constant-specialization.bmp` was inspected: gameplay, character,
  scenery and HUD are present. Both runs logged zero errors.

Release build and existing `edf_native_draw_test` passed. The test includes GPU
readback, matrix/array packing, poisoned padding replacement, partial patches,
invalid extents, and exact signed-zero/NaN/infinity preservation. Broad gameplay
testing remains deferred.
