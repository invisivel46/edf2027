# Releasing EDF2027

This page covers how the release zip is built, what goes in it, and where a player's
logs and crash dumps end up. It also covers what the game does when something fails.

## Building the package

From a VS developer prompt, or through `build.cmd`, which sets one up:

```
build.cmd win-amd64-release package_release
```

`package_release` builds `edf2027` and runs `tools/package-release.cmake`, which writes the
following to `out/package/` (`EDF2027_PACKAGE_ROOT`):

| Output | What it is |
|---|---|
| `EDF2027-PC-<version>-<commit>/` | the staged folder |
| `EDF2027-PC-<version>-<commit>.zip` | the release zip; it holds that folder |
| `EDF2027-PC-<version>-<commit>-symbols.zip` | `edf2027.pdb`, for crash dumps from this exact build |

If tracked files differ from the commit, the name ends in `-dirty` and CMake prints a
warning. Build releases from a clean checkout. The codegen stamp that every configure
rewrites doesn't count as a change.

The script fails the package if:

- an expected file is missing;
- anything else is in the folder: a PDB, a test exe, a stale DLL, a log, a `.toml`, or
  game data (`.xex`, `.iso`, `.sgo`);
- a required license is missing;
- the staged folder fails `tools/audit-native-dependencies.cmake`. The audit checks that
  every non-system DLL import resolves inside the folder, that there's no eager D3D11
  import, and that there's no Xenos DLL.

### Contents (0.3.0-beta, about 19 MB zipped, 59 MB unpacked)

| File | Why |
|---|---|
| `edf2027.exe` | the game |
| `rexruntime.dll` | ReXGlue runtime (SDK `bin/`). SDL3 is linked into it statically, so there's no SDL3 DLL. |
| `amd_fidelityfx_dx12.dll` | FSR. This is AMD's signed build with frame generation (`EDF2027_FFX_SIGNED_DLL`). **rexruntime.dll imports it directly**, so the game won't start without it, even with FSR off (see [Known limitations](#known-limitations)). |
| `d3dcompiler_47.dll` | HLSL compiler, from the Windows SDK (`Redist\D3D\x64`, `EDF2027_D3DCOMPILER_DLL`). The renderer compiles the game's shaders at run time with `D3DCompile`. |
| `msvcp140.dll`, `msvcp140_atomic_wait.dll`, `vcruntime140.dll`, `vcruntime140_1.dll` | Visual C++ runtime, deployed app-local from `VC\Redist\MSVC\<ver>\x64\Microsoft.VC14x.CRT` (`EDF2027_VC_RUNTIME_DIR`). Players don't need to install the VC++ Redistributable. |
| `gamecontrollerdb.txt` | SDL controller mappings |
| `README.md`, `LICENSE`, `LICENSES/` | Includes the FidelityFX, SDL3, ReXGlue, imgui, Inter, Font Awesome and gamecontrollerdb licenses, plus `Microsoft-redistributables.txt` for the Microsoft DLLs above. |

The game loads the DLLs beside the exe first. None of them is a KnownDLL, and the
startup report logs the path each one loaded from. A machine therefore gets the packaged
`d3dcompiler_47` even though Windows ships its own. The same goes for the packaged VC++
runtime, even if an older Redistributable is installed. An older runtime than the build's
toolset can crash in `std::mutex`, so the packaged copy avoids that.

**Wine and Proton.** Depending on the prefix, Wine may use its builtin `d3dcompiler_47`,
`msvcp140` and `vcruntime140` instead of the packaged DLLs. This hasn't been tested under
Proton yet. If shaders render wrongly under Proton, or the log
shows `d3dcompiler_47.dll: C:\windows\system32\...`, set this launch option:

```
WINEDLLOVERRIDES="d3dcompiler_47=n,b;msvcp140=n,b;vcruntime140=n,b;vcruntime140_1=n,b" %command%
```

## Version

The version is `EDF2027_VERSION` near the top of `CMakeLists.txt`, and nothing else sets
it. `tools/write-version.cmake` runs on every build and writes the version, the short
commit and the dirty flag to `edf_version_info.h`, which only `src/version.cpp` includes.
The full form looks like `0.3.0-beta+abc1234` (with `.dirty` when tracked files differ
from the commit). It appears in:

- the first line of every log (`==== EDF2027 0.3.0-beta+abc1234 ====`);
- the F1 menu, under the section list;
- every crash report;
- the package name.

To release a new version, change `EDF2027_VERSION` and commit, then build the package from
the clean checkout.

## Logs

- **Where:** `logs\edf2027_NNN.log` next to `edf2027.exe`, one file per run, with `NNN`
  going up by one each run. If the game's folder can't be written to (for example an
  install under Program Files), the logs go to `%APPDATA%\edf2027\logs`. If that fails
  too, they go to `%TEMP%\edf2027\logs`. `--log_file=<path>` overrides all of these. The
  log names its own path in the startup report.
- **Size:** at the default `log_level=info`, a session writes about 0.3 to 0.5 MB per minute.
  The file rotates at 5 MB (`log_max_file_size_mb`) into `edf2027_NNN.1.log`, `.2.log`
  and so on, and each run keeps at most 20 (`log_max_files`). The game keeps the newest
  10 runs (`edf::diag::kKeptRuns`). Each run's logs, dumps and crash notes go together,
  and anything older is deleted when the game starts. Other files in the folder are left
  alone.
- **Startup report:** the top of every log records:
  - version and exe path;
  - Windows edition and build, with the Wine version and host OS when running under Wine;
  - CPU, core count and RAM;
  - every GPU, with vendor and device IDs, VRAM and driver version;
  - every display, with resolution and refresh rate;
  - the user data, config and log paths;
  - the renderer, display, frame-rate and FSR settings in use;
  - the path and version of `rexruntime`, `d3dcompiler_47` and `amd_fidelityfx_dx12`;
  - the FidelityFX providers.

## Crash dumps

`src/diagnostics.cpp` sets these up in `Edf2017App::Create`. The ReXGlue SDK has no crash
handler of its own; its vectored handler only serves guest memory.

- **What triggers them:** unhandled SEH exceptions (access violations, stack overflows,
  illegal instructions, and C++ exceptions that escape the main thread), `std::terminate`,
  `abort()`, pure virtual calls and CRT invalid-parameter reports.
- **What gets written:** a minidump, `logs\edf2027_NNN_crash.dmp` (stacks, threads,
  modules and the memory the stacks point at, about 0.5 MB). The dump is written on a
  helper thread, so a stack overflow can still write it. It comes with
  `edf2027_NNN_crash.txt`, which gives the exception, `module+offset`, the thread, and the
  `what()` of a C++ exception. The same summary goes into the log as a `[critical]` line.
- **What the player sees:** the game window is minimized, and a message box names the
  dump and the log and offers to open the folder. The process then exits without a
  Windows Error Reporting dialog.
- **Symbolizing:** unzip the matching `-symbols.zip` next to the dump and open the dump in
  Visual Studio or WinDbg. For a quick look:
  `llvm-symbolizer --obj=edf2027.exe --relative-address 0x<offset>`.
  - Host code has line tables (`-gline-tables-only`).
  - Recompiled guest code has function names only (`sub_XXXXXXXX`).
  - `rexruntime.dll` has no PDB.
  - The PDB is built with `EDF2027_RELEASE_PDB` (default ON). It doesn't change the
    generated code, and `/PDBALTPATH` keeps build paths out of the exe.
- **Testing the path:** `--edf_crash_test=1` (access violation), `2` (uncaught exception
  on a thread) or `3` (`abort()`) crashes on purpose once the game is up.

## When something fails

| Failure | What the player gets |
|---|---|
| No D3D12 device, or no feature level 11_0 | The device is created on the first frame. The presenting host catches the error and shows "the renderer stopped" with the error, driver advice and the log path, then the game closes. If window or renderer setup throws earlier, the player gets "EDF2027 could not start" with the same details. Both also write `edf2027_NNN_error.dmp`. |
| GPU removed, hung or reset (TDR) | The host's present or its fence wait (10 s deadline, with the device-removed reason) throws. The player gets the same "renderer stopped" box, and the game closes instead of freezing on its last frame. |
| Game failed to boot (runtime setup, XEX load) | "EDF2027 could not start" with the game folder and the log path. Some SDK checks show their own box first. |
| `amd_fidelityfx_dx12.dll` missing | Windows refuses to start the game: "The code execution cannot proceed because amd_fidelityfx_dx12.dll was not found." See [Known limitations](#known-limitations). |
| FFX DLL present but broken, or FSR can't initialize | FSR stays off and the scene is resolved without it. The log says why, and F1 > Graphics shows "FSR is unavailable, so it is off: <reason>" under the upscaling row. |
| Shader cache folder can't be written | The cache moves to `%LOCALAPPDATA%\edf2027\native_cache`, which the log names. If that fails too, every run compiles its shaders again; that's slower, not broken. |
| Log folder can't be written | The log moves to `%APPDATA%\edf2027\logs` (see [Logs](#logs)). |
| Saved controller database path is gone (the game folder moved) | The game looks for `gamecontrollerdb.txt` beside the exe again. |

## Known limitations

- **amd_fidelityfx_dx12.dll must ship.** `rexruntime.dll` (prebuilt SDK) imports four FFX
  entry points directly, for the SDK presenter that native mode never creates. The fix is
  on the SDK side: link `rexruntime.dll` with `/DELAYLOAD:amd_fidelityfx_dx12.dll`, since
  those calls are never made in native mode. After that, a missing DLL would only disable
  FSR, which `native_ffx.cpp` already handles. Until then, `package-release.cmake` refuses
  to build a package without the DLL.
- The exe and DLLs are **not code-signed**. SmartScreen will warn on first run.
- A release build uses the `win-amd64-release` preset, which targets **x86-64-v3 (AVX2)**.
  On an older CPU the game shows a message box and exits (`src/cpu_check_entry.cpp`).

## Release checklist

1. Commit on a clean tree, with `EDF2027_VERSION` set.
2. Run `build.cmd win-amd64-release package_release`. Check that there's no `-dirty` in
   the name and that the audit line reads `Native PE import audit passed`.
3. Unzip into a new folder and run it with `--user_data_root=<empty folder>`. The
   first-run setup should appear. Pick the game folder and reach the title screen.
4. Check that the log's startup report shows the packaged `d3dcompiler_47.dll` path and
   the version.
5. Upload the zip. Keep the `-symbols.zip`: attach it to the release, or at least keep it
   privately. A crash dump from this build can only be symbolized with it.
