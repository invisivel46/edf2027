# Building from source

Back to the [README](../README.md).

The build is Windows-only and uses clang, not MSVC's `cl`. The recompiled
game is 81 large generated C++ translation units (about 84 MB of source
under `generated/default/`), so a build needs a lot of memory and time.

## Prerequisites

| Tool | Version used | Notes |
|---|---|---|
| Visual Studio 2026 (version 18) Community | MSVC 14.50 toolset | Workload *Desktop development with C++*, with a Windows 10/11 SDK and the *C++ Clang Compiler for Windows* component. `build.cmd` and `validate-renderer-offline.cmd` expect `C:\Program Files\Microsoft Visual Studio\18\Community`. |
| clang / clang++ and lld-link | 20.1.8 | The copy that ships with Visual Studio (`VC\Tools\Llvm\x64\bin`) is the one used. The presets call `clang` and `clang++` from `PATH`. |
| CMake | 3.25 or newer (4.x used) | Visual Studio's bundled CMake works. |
| Ninja | 1.12 | Visual Studio's bundled Ninja works. |
| Python | 3.x | Configuring with tests on (the default) needs it, and so do the tools under `tools/`. |
| ReXGlue SDK | 0.10.0 | `find_package(rexglue 0.10.0)`. See below. |
| Git | any | |

**The ReXGlue SDK.** Build and install
[rexglue-sdk](https://github.com/rexglue/rexglue-sdk) at the `v0.10.0` tag,
with FidelityFX turned on (`REXGLUE_ENABLE_FIDELITYFX`, which is off by
default). Run these commands from a Visual Studio x64 developer prompt, in a
folder of your choice (`C:\rex` here):

```cmd
cd C:\rex
git clone --recursive --branch v0.10.0 https://github.com/rexglue/rexglue-sdk.git
cd rexglue-sdk
cmake --preset win-amd64 -DREXGLUE_ENABLE_FIDELITYFX=ON -DCMAKE_INSTALL_PREFIX=C:\rex\sdk_ffx\win-amd64
cmake --build --preset win-amd64-release
cmake --install out\build\win-amd64 --config Release
```

The rest of this section calls `C:\rex\sdk_ffx\win-amd64` "the SDK". It
contains `lib\cmake\rexglue`.

- **The signed FidelityFX DLL.** The FSR path wants AMD's signed
  `amd_fidelityfx_dx12.dll`, the one that includes frame generation, from the
  FidelityFX SDK's `PrebuiltSignedDLL` folder. The SDK build fetches it into
  `rexglue-sdk\out\build\win-amd64\_deps\fidelityfx-src\PrebuiltSignedDLL\`.
- CMake looks for the DLL at `<SDK>\..\..\rexglue-sdk\out\...`. With the
  layout above (`C:\rex\rexglue-sdk` next to `C:\rex\sdk_ffx`), that is where
  it is. If your layout is different, pass
  `-DEDF2027_FFX_SIGNED_DLL_PATH=<path to amd_fidelityfx_dx12.dll>`.
- If CMake does not find the signed DLL, it stages the SDK's own build of the
  DLL instead. That build has the upscaler and no frame generation, and the
  configure log says so.
- `-DEDF2027_FFX_SIGNED_DLL=OFF` always uses the SDK's DLL.
- Either way the DLL is copied next to `edf2027.exe` and into the package.

## Configure and build

`build.cmd` does both steps inside the Visual Studio developer environment:

```cmd
set REXGLUE_SDK=C:\path\to\sdk\win-amd64
set BUILD_JOBS=2
build.cmd                                   & rem win-amd64-release, target edf2027
build.cmd win-amd64-relwithdebinfo edf2027  & rem preset, target
```

- `REXGLUE_SDK` is the SDK's `win-amd64` folder. It defaults to the
  maintainer's local path, so set it.
- `BUILD_JOBS` is the number of parallel compiler jobs, 2 by default. Clang
  needs a lot of memory for each generated file, and it runs out of memory at
  high parallelism. Raise the number only if you have plenty of free RAM. The
  port is built with the default on a 32 GB machine.
- The first build compiles everything and takes a long time. Later builds are
  incremental.

By hand, from an *x64 Native Tools* / developer prompt:

```powershell
cmake --preset win-amd64-release -DCMAKE_PREFIX_PATH=C:\path\to\sdk\win-amd64
cmake --build --preset win-amd64-release --target edf2027 --parallel 2
```

The executable is written to `out\build\<preset>\edf2027.exe`, with its DLLs
and `gamecontrollerdb.txt` next to it. `find_package` caches `rexglue_DIR`, so
delete the build folder's `CMakeCache.txt` before pointing it at another SDK.
To build from an SDK source tree instead of an install, set `-DREXSDK_DIR=<rexglue-sdk checkout>`.

## Presets

| Preset | Build type | Code generation |
|---|---|---|
| `win-amd64-release` | Release, `-O2 -DNDEBUG` | x86-64-v3 for guest and host code (`EDF_GUEST_OPT_PROFILE=v3`, `EDF_HOST_OPT_PROFILE=v3`) and profile-guided optimization with the committed `pgo/edf2027.profdata` (`EDF_PGO=use`, see [pgo.md](pgo.md)). This is the one to ship and to measure. |
| `win-amd64-release-nopgo` | Release, `-O2 -DNDEBUG` | The same without PGO, for A/B measurements. |
| `win-amd64-pgo-train` | Release, `-O2 -DNDEBUG` | The instrumented build (`EDF_PGO=generate`) that `tools/pgo-train.ps1` trains the profile with. |
| `win-amd64-relwithdebinfo` | RelWithDebInfo | Baseline x86-64 with no extra profile. It runs on any x86-64 CPU. |
| `win-amd64-debug` | Debug | Baseline x86-64, unoptimized. Very slow in game. |

The ARM64, Linux and macOS presets in `CMakePresets.json` are placeholders.
They do not build the game.

`cmake/edf_optimization.cmake` holds the optimization knobs. Every one is off
unless set, and the release preset sets `v3` and `EDF_PGO=use`:

- `EDF_GUEST_OPT_PROFILE` and `EDF_HOST_OPT_PROFILE`: `""`, `O3`, `v3` or
  `O3-v3`. The `v3` profiles add `-ffp-contract=off`, so float results do not
  change.
- `EDF_LTO=thin`: ThinLTO, linked by lld.
- `EDF_PGO=generate|use`: clang IR profile-guided optimization, with
  `EDF_PGO_RAW_FILE` and `EDF_PGO_PROFILE`. If the profile is missing, `use`
  prints a warning and builds without PGO. [pgo.md](pgo.md) covers
  training (`tools/pgo-train.ps1`) and when to retrain.

The release preset pins `CMAKE_CXX_FLAGS_RELEASE`, because an empty cached
value once built the whole game at `-O0`. Configuring a Release or
RelWithDebInfo tree whose flags have no `-O` level now fails, unless you pass
`-DEDF2027_ALLOW_UNOPTIMIZED_RELEASE=ON`.

## Older CPUs

The release preset's executable needs x86-64-v3. Its entry point
(`src/cpu_check_entry.cpp`) is compiled for baseline x86-64, and it checks the
CPU before any other code runs. For a build that runs on any x86-64 CPU, clear
both profiles:

```powershell
cmake --preset win-amd64-release -DCMAKE_PREFIX_PATH=<SDK> -DEDF_GUEST_OPT_PROFILE= -DEDF_HOST_OPT_PROFILE=
```

You can also use the `win-amd64-relwithdebinfo` preset. The v3 build was about
11% faster in gameplay in the `-O2` profile run (235 to 261 FPS uncapped).

## The recompiled code (codegen)

The generated C++ under `generated/default/` is tracked in git, so a normal
build does not need the game files. The `edf2017_codegen` target runs
`rexglue codegen edf2017_manifest.toml`. It is part of every build, and it
re-runs only when one of its inputs changes.

The tracked code is emitted by a modified build of the ReXGlue code generator
(options in `edf2017_codegen_fork.toml`; design and measurements in
[codegen-fork.md](codegen-fork.md)). Configure checks that
`EDF_REXGLUE_CODEGEN_EXE` points to a `rexglue.exe` that reports the pinned
version (`EDF_CODEGEN_FORK_VERSION` in `CMakeLists.txt`) and stops otherwise,
so the stock generator can never silently replace the code. That generator
build is not public yet. Until it is, setting the options in
`edf2017_codegen_fork.toml` to `false` builds with the stock SDK tool, which
regenerates the stock (slower) code from your own disc (steps below).

To regenerate the code, for example after changing `edf2017_overrides.toml`:

1. Edit `edf2017_manifest.toml` so that `game_root` and `file_path` point to
   your own extracted disc.
2. Build the target:

   ```powershell
   cmake --build out/build/win-amd64-release --target edf2017_codegen
   ```

Game data (`*.iso`, `*.xex`, extracted folders) must never be committed.

## Tests

Tests are on by default (`BUILD_TESTING`, from CTest). `build.cmd` builds only
`edf2027`. Build the `all` target to get the test executables, then run CTest:

```powershell
cmake --build out/build/win-amd64-release --parallel 2
ctest --test-dir out/build/win-amd64-release --output-on-failure
```

That is 81 tests: C++ unit tests, D3D11/D3D12 WARP render tests, and Python
tests of the tools. From the repository root in PowerShell,
`.\validate-renderer-offline.cmd` runs the renderer regression gate: it
rebuilds the renderer test targets, runs them, and saves a report under
`out\renderer-offline\`. It needs neither the game nor its data. See
[renderer-offline-validation.md](renderer-offline-validation.md).

The in-game test tools are under `tools/`. They need the game data:

- `tools/run-renderer-scenario.ps1` runs a scripted Mission 1 benchmark or
  another scenario from `tools/renderer-scenarios.json`. A seeded save from
  `tools/make-edf-save.py` unlocks later missions.
- `tools/renderer-runtime-gate.py` checks a run's log for FPS, mission and
  errors.
- `tools/compare-renderer-ab-captures.py` compares guest and native frames.

[renderer-internals.md](renderer-internals.md) describes them.

## Packaging

```powershell
cmake --build out/build/win-amd64-release --target package_release
```

This writes `out/package/EDF2027-PC-<version>-<commit>.zip` and a separate
`-symbols.zip` with the PDB. The package contains the executable,
`rexruntime.dll`, `amd_fidelityfx_dx12.dll`, `d3dcompiler_47.dll`, the
app-local Visual C++ runtime DLLs, `gamecontrollerdb.txt`, the README and
`LICENSES/`, and never any game data; the build fails if anything else is in
the folder or something is missing (see [release.md](release.md)). Disc-image extraction is built in
(`src/xdvdfs.h` reads the XDVDFS game partition directly), so the package
needs no external tools. From a developer prompt,
`cmake --build <build-dir> --target audit_native_dependencies` checks that
nothing in the executable's import closure resolves outside its folder.
