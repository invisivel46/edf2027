# Contributing

Keep game files, SDK installations, build output, logs, and release packages out
of the repository. In particular, never commit an Xbox 360 disc image,
`default.xex`, or extracted game data.

Generated C++ under `generated/default/` is tracked because it is part of the
recompilation source tree. The build stamp and partition map are also tracked
so normal builds are reproducible; transient dependency bookkeeping is ignored.

Before submitting changes, configure and build with an installed ReXGlue SDK:

```powershell
cmake --preset win-amd64-release -DCMAKE_PREFIX_PATH=C:\path\to\rexglue-sdk
cmake --build --preset win-amd64-release
```

Run the dependency-free unit suite (no game data, display, or GPU required):

```powershell
build.cmd win-amd64-release edf2027_tests
out\build\win-amd64-release\edf2027_tests.exe
```

The suite covers XEX header validation, persisted-config filtering, scripted
input parsing and transitions, frame statistics/pacing calculations,
graphics-setting mappings, and XDVDFS disc-image parsing (against a synthetic
in-memory image — no disc image needed).
