# Native graphics settings restoration

User requested restoration of the former Xenos settings menu, especially resolution changes. This is part of replacing Xenos, not authorization to re-enable its plugin.

## First implemented slice

F1 anisotropic filtering now uses `edf_native_anisotropic_filtering` (-1 game default, 0 off, 1..5 selecting 1x..16x). The material binding path derives a native sampler cache key including the effective filter choice. Changes can therefore select new sampler objects immediately without clearing or mutating samplers already bound to the context. Default preserves existing behavior. Point, base-only, movie and dedicated UI sampling are preserved. Unsupported overrides preserve game state.

Built `out/build/win-native-clean/edf2027-native-settings.exe`, SHA256 `3AF9B1ED63CCDF988104225720AFDF1A52277D2D2D6516BC81A1B6E7A3DF2908`. All 18 CTest tests passed (12.86 seconds). Added filter-level, reset, invalid-value, point/base-only preservation and D3D11 sampler creation tests. This candidate has not yet been play-tested or checked interactively through F1.

## Remaining requested work

- Verify existing window-size/aspect selection through save/restart and native presentation. Window/output size is not proof of higher internal render resolution.
- Implement independent native render resolution/scaling with consistent target, viewport, scissor, depth, post-processing and UI handling. Do not simply enable `draw_resolution_scale_x`, which is a legacy plugin flag.
- Restore native MSAA controls and post anti-aliasing.
- Restore sharpening/spatial upscaling and dithering through native presentation. Temporal upscaling needs valid motion/depth inputs; DLL presence alone is not capability evidence.
- Decide and implement native background shader compilation behavior with correct resource readiness.
- Verify save/reload, setting changes, multiple resolutions, loading UI and gameplay. Existing VSync/FPS-cap/display controls remain available.

## Resolution initialization implementation and live test

The 1920x1080 video-mode probe using settings.exe (owned PID 44348, run `binding-validation-20260910-064114-6eee5e94`) still allocated a 1280x720 native scene and a 640x360 first reduction. Capture showed gameplay/HUD with 120/120 ammo. Stopped that exact-path owned process after capture. This proves output-size selection alone does not change internal resolution.

Generated `sub_8219E3B8` hardcodes renderer+84 to 1280 and +88 to 720/960 based on wide/display flags, ignoring queried video-mode dimensions. Before it calls `82139A40` at LR `8219E4D4`, r7 points to presentation parameters and r8 points to renderer+8. Added a caller-specific hook that overrides both dimension pairs before device/resource creation. Downstream scene, depth and post allocations reload the renderer fields. Other callers and zero/default options remain unchanged. New restart-only options: `edf_native_render_width` and `edf_native_render_height`, validated to width 640..4095 and height 480..4095 when enabled. F1 is not yet wired to them pending runtime validation.

Candidate `edf2027-native-resolution.exe`, SHA256 `88953CAFBE0224C28B3FEB4B285475A0FD65AD9350ABB30B6F7E0D4EDEC68F43`, builds and passes all 18 existing tests (12.82 seconds). These tests do not independently verify the new initialization hook.

Started owned PID **33500** at 06:43:48 in `binding-validation-20260910-064348-bb2ce427` with movement/fire input, integer post centers, 1920x1080 video mode AND native render size, and AF16. Early logs confirm native scene **1920x1080, 2 samples** and first reduction **960x540**, followed by 480x270, 240x135, 120x67, 60x33 and the independent 16..1 luminance chain. This is actual allocation evidence, not merely a larger window. Runtime remains in progress: loading, intro and movement/fire checks still pending. Revalidate this process before launching another.

Early `menu-check.png` shows the new-game-data prompt, but the title backdrop appears restricted to an upper-left region with black space to the right/bottom. Treat this as a possible UI scaling regression to investigate, not a passed 1080p visual gate. The new dimensions must not be advertised as finished based on allocation logs alone.

## Draw-time UI investigation

PID 33500 completed intro and reached movement/fire: `gameplay-check.png` is actually the mothership intro (no HUD), while `gameplay-fire.png` shows a displaced player, shell casings/projectiles and 005/120 ammunition. HUD elements are smaller and inset at 1080p. Sampled draw/parameter/texture error counters remained zero. Stopped that owned process after capture. This verifies bounded gameplay execution, not resolution fidelity or visible FPS.

Tested a caller-specific override of `82142300` at LR `8242D30C` to keep XUI initialization dimensions 1280x720. Candidate `edf2027-native-resolution-ui.exe`, SHA256 `E8D174E4BDD837A3F9263804552886FE9F6080AFDF3BC5B60B94C5D1BAF1C75C`, passed 18 tests (13.04s). Owned PID 60884, run `binding-validation-20260910-064722-a201df4e`, logged the hook firing but `menu-check.png` still showed the backdrop at upper left. **Removed this ineffective override from source** and stopped the owned process. Do not promote that candidate.

Current candidate `edf2027-native-resolution-trace.exe`, SHA256 `FB4799128E05C7D9EE94FA230363CE6E623D2851E2F27C7EE6FA8B81A7821C4F`, adds bounded first-five XUI draw viewport/projection traces only when native render width is enabled. All 18 tests passed (12.45s).

Owned process **33568**, started 06:49:18, remains running in `binding-validation-20260910-064918-7326fed7` with the same 1080p/native-size/AF16/movement-fire flags. Revalidate it before launching any other game. Early data at 06:49:27 proves XUI viewport, scissor and target are all full 1920x1080. Projection rows are `(2/1920,0,0,-1)` and `(0,-2/1080,0,1)`, while authored local transforms retain positions such as (385,296). Therefore the background is not clipped by a leftover 720p viewport: draw-time projection consumes authored pixel coordinates at output resolution. Next change should address the logical-to-render projection in native XUI bindings, with tests for translation, clipping and default preservation; separately audit custom font/Utility HUD paths. Do not globally change scene/post viewport or global texel-size constants.

## Native XUI projection correction

Stopped owned trace process 33568 after revalidating its exact executable. Implemented `NativeXuiCanvasProjection` in `native_xui_bindings.h`: for enabled native render sizes, map clip X to `sx*X+(sx-1)*W`, and Y to `sy*Y+(1-sy)*W`, with sx=target.width/1280 and sy=target.height/720. Depth/homogeneous rows, local transforms and texture coordinates are unchanged. This corrects the authored top-left canvas in the native constant upload, without guest-memory edits or changing scene/post resources. The upload uses the already resolved binding, not a per-draw string lookup. Default unit scaling retains the existing byte upload path.

Added tests for projection scaling, top-left anchoring, depth/homogeneous preservation, identity/default and invalid dimensions/size. All 18 tests pass (12.16 seconds). Existing GPU XUI tests continue to cover normal/reversed depth and UV behavior; higher-resolution clipping and scaled GPU pixel tests remain to be extended.

Built `edf2027-native-canvas.exe`, SHA256 `4EC00B16B5BF14AFE59E0A0AF791BF79E8E56C583217D3FCAC16D868FB3466D3`. Owned PID **1212** started at 06:55:51, run `binding-validation-20260910-065551-1b1082d6`, same 1080p/native-size/AF16/movement-fire flags, is still running. Revalidate before another launch. `menu-check.png` now shows the title background and logo centered and filling the 1080p client rather than occupying its upper-left 720p region. The custom Press START frame/text remain relatively small, consistent with their separate Utility/font path. Loading, gameplay after this change, custom HUD/menu scaling, partial scissor handling and other aspect ratios are not yet verified. F1 resolution control remains intentionally unwired until these are handled.

## Custom UI candidate

Stopped owned PID 1212 after exact-path verification and `gameplay-fire.png` capture: gameplay, ants/projectiles, 100/120 ammo and the still-small/inset HUD. No claim of completed HUD scaling for that candidate.

Added `native_canvas_constants.h` with a host-owned XY float-register scaling helper preserving Z/W and input bytes. Native font bindings can scale Offset/Scale only, preserving atlas/tint/channel inputs. Utility `VS_2D` / `VS_2DTex` with the audited source fingerprint scales `_g_DX2DScale` and `_g_DX2DOffset` at native parameter upload, not guest memory. These are center-anchored clip-space corrections, unlike XUI's top-left correction. Default scale retains input bytes; native higher-resolution paths need visual validation. The Utility selection currently checks names during upload; move this classification into immutable shader metadata as part of finishing native ownership/per-draw cleanup.

Candidate `edf2027-native-hud-canvas.exe`, SHA256 `BFBEFD34C1961728843E9D93CC3CD4CBBB086BA276A9A944969F6F39ED8D7B7E`. All 18 tests passed (13.05 seconds), including new XY/Z/W/default/invalid-size helper checks. These are not full scaled-font GPU or clipping coverage.

Owned PID **60740** started at 06:59:54, same flags, run `binding-validation-20260910-065954-f1f6602f`, remains running. Early `menu-check.png` confirms the custom Press START frame is enlarged, but text is absent at that instant (could be blink or clipping; not a text pass). `menu-check-2.png` is the opening movie, not the menu: subtitles appear near the bottom with apparent clipping/fading. **Do not claim custom UI fidelity yet.** Next inspect font viewport/scissor and Offset/Scale live, distinguish intentional fade from clipping, then verify gameplay HUD alignment. Partial scissor mapping has not been updated by this change. The title background correction remains intact; scene is still 1920x1080/2 samples. Revalidate PID 60740 before another launch.

## Confirmed clipping mismatch and correction

PID 60740 was stopped after exact-path verification and `gameplay-check.png`. The capture shows the HUD returned to normal proportional size and edge placement at 1080p, with readable AF14/120/120 and 200/200 health. This is a gameplay-entry check, not a firing check for this build.

Built/ran diagnostic `edf2027-native-font-trace.exe`, owned PID 47716, run `binding-validation-20260910-070232-db3cf7ac`. Font trace proved a full 1920x1080 viewport with scissor enabled and rectangle `(784,668)-(854,680)` while font Offset/Scale were being enlarged around the viewport center. The corresponding corrected rectangle is `(696,732)-(801,750)`. This mismatch clips scaled glyphs independently of blinking/fades. Stopped the exact-path owned diagnostic after collecting these values.

Implemented `ScaleNativeCanvasScissor`: transforms clipping coordinates using the same anchor/scale as geometry, clips to viewport bounds, outward-rounds edges and preserves empty clips. XUI uses a top-left anchor; custom Utility/font use the viewport center. Default 1x is unchanged. Added tests for the observed font rectangle, default preservation, full-frame coverage and empty-clip preservation. All 18 tests passed (14.35 seconds); scaled GPU clipping/pixel regression coverage and non-16:9 behavior still need broader verification.

Current candidate `edf2027-native-canvas-clip.exe`, SHA256 `1932714FBB1A69E129131F398DD35422D1B5A0790B9633949F285F2BD67CADB0`. Owned PID **63076**, started 07:04:43, is running in `binding-validation-20260910-070443-e6750d47` with the same 1080p/native-size/AF16/movement-fire flags. Revalidate before any new launch. Runtime text/menu/loading/gameplay checks for this correction are pending.

First runtime font trace confirms the corrected scissor `(696,732)-(801,750)` is actually used. `menu-check.png` was captured after the script had already entered the opening movie; it does not verify the menu. The bottom movie text remains soft/partly faded in that capture, so do not claim a subtitle fix from the font-scissor correction alone (movie-rendered text may belong to a separate path).

## F1 native render-resolution selector

Stopped owned PID 63076 after `gameplay-check.png`, which shows readable 1080p HUD text and correctly proportioned HUD at mission entry, 120/120 ammo. A different, non-owned process 47588 (canvas-clip.exe, started 07:06:32) caused the diagnostic launcher to refuse a new run. Left it untouched. It subsequently disappeared on a read-only process check.

Added `tools/native-menu-only-input.txt` to avoid advancing into a mission during menu checks. Added F1 native render resolution selection: original, 720p, 900p, 1080p, 1440p, 2160p and custom. Marked experimental; this does not certify all listed resolutions. Window size remains independent. Invalid paired dimensions disable Save/Save and restart. `ValidNativeRenderMode` tests cover original, valid bounds, partial-zero pairs and invalid bounds.

Renderer/canvas code now captures the render-size pair once at initialization. Saving F1 settings cannot change draw scaling mid-run while resources still have the previous size. Changes apply on restart. Native anisotropic filtering remains live.

Built `edf2027-native-resolution-menu.exe`, SHA256 `F8DC62523FD957BA583DCFB266C5946B10DE58AB1B47438DE1EED06092D7F4B1`. All 18 tests passed (12.10 seconds). Interactive F1 rendering, save/reload/restart and all advertised modes remain to be verified. Experimental availability is not full resolution restoration completion. A fresh owned menu-only diagnostic has been launched after the non-owned process exited; see following runtime entry.

Owned menu-only PID 28960 (`binding-validation-20260910-070953-d14bcd66`) was captured and stopped after exact-path verification. `menu-check.png` shows the player-count selection screen with readable Single Play/Cooperative Play text and matching enlarged frames over the full-size title backdrop. The input script stopped at player-count selection, not at the title menu. This is positive 1080p text/clipping evidence, not interactive F1/save/restart verification.

User reports missing loading screen on **canvas-clip.exe**. The loading presentation code and metadata lock removals remain, but source presence does not disprove a runtime regression. Started reproduction on that exact existing candidate, owned PID **5616**, at 07:11:34, run `binding-validation-20260910-071134-672ea650`, movement/fire input, integer centers, LoadingTrace and LoadTimings, default 720p (no NativeRenderSize). This isolates the build from optional resolution overrides. Currently running; revalidate before starting anything else. Prior size-dependent changes are disabled in this test, except the unchanged native loading ownership work. Need both load durations and image captures during actual loading intervals.

### Default-size reproduction result

The exact canvas-clip binary at default 720p showed the loading screen: `loading-0.png` at 07:12:40.650 contains the Grenade Launcher tip and Now Loading animation. First LoadMap completed at 07:12:40.597 in **3130.3438 ms**; second completed at 07:13:27.781 in **3062.6433 ms**. Thus the prior ~3.2s map-loading improvement remains in this matched diagnostic. These are inclusive script LoadMap durations, not total menu-to-gameplay times. No error-level log matches were found in this run at inspection.

Second-transition capture started too late (07:13:38); `second-loading-0.png` shows gameplay with readable HUD/120 ammo, NOT the second loading screen. Do not treat that as a loading-screen pass. Stopped owned PID 5616 after exact-path verification. The user's missing-screen report remains unresolved, not disproven by one successful first-transition capture. Next reproduce with 1080p/native-size enabled on the same executable, preserving trace/timing flags.

Active owned 1080p reproduction: PID **45436**, started 07:14:33, `binding-validation-20260910-071433-006bd463`. Same canvas-clip binary, movement/fire, integer centers, LoadingTrace+LoadTimings, RenderWidth1920/RenderHeight1080/NativeRenderSize/AF16. First loading interval expected around 60..70 seconds after launch; second approximately 108..118 seconds based on the immediately preceding run. Capture in those intervals, not after logs report load completion. Revalidate live process before further action.

### Native 1080p reproduction result and settings-matched follow-up

PID 45436 first loading screen is visible: `first-loading-1.png` shows the fade-in at 07:15:37, and `first-loading-4.png` shows the full readable Grenade Launcher tip and Now Loading at 07:15:40.527. LoadMap durations were **3114.0769 ms** and **3096.0611 ms**. Second capture again started after the load completed (07:16:36 versus completion07:16:27); do not count it as second-loading visual verification. No error-level matches at inspection. Stopped only this owned process after exact-path check.

Read the user's Roaming/edf2027 config without editing it: window/video mode1920x1080, windowed, native AF16, no native render-size override, no integer-post-centers override, game data under `%APPDATA%/edf2027/game`. Legacy present_effect=fsr is saved but not a native upscaler implementation. The diagnostics used another game-data root and heavy load timing logging; do not assume these runs exactly reproduce the user's state.

Started owned PID **51740**, 07:17:41, exact canvas-clip executable, run `binding-validation-20260910-071741-dabde457`, with the user's configured game-data folder (read-only), video1920x1080, default internal size, AF16, no integer centers, no LoadingTrace/LoadTimings. Host timing diagnostics remain enabled by the launcher, so this is closer but not completely identical to an ordinary user launch. Fresh isolated user/save directory preserves the user's files.

An owned capture command is running for PID51740: captures at elapsed50..85 and100..135 seconds, exits after140 seconds, verifying exact process path each iteration. Poll its exec session rather than starting another capture loop. This ensures both windows are sampled without model/tool round-trip timing gaps. Captures/logs are in the run directory above. Missing loading screen remains unresolved pending this evidence.

## Previous diagnostic closed

## Native AA selector (latest)

Added `edf_native_msaa`: 0 preserves the game's scene sample request, 1 disables scene MSAA, 2 forces 2x, 4 forces 4x. F1 now exposes Game default / Off / 2x MSAA / 4x MSAA outside the disabled legacy controls. Save persists the native flag; scene initialization pins it for the renderer lifetime, so changing the saved choice cannot alter subsequent allocations until restart. Color and depth use the same selected count. UI/post targets remain single-sampled. Unsupported device/format combinations retain explicit allocation errors rather than silently substituting a different quality level. FXAA and other legacy post-AA controls remain disabled.

Built `out/build/win-native-clean/edf2027-native-aa-controls.exe`, SHA256 `0A0C8A3F679F3CF0C10BE6B0648CD16DD2983F328F99B424A30482CBCD5BD035`. All 18 CTest tests passed (14.39s). New tests cover all guest-mode/override mappings and invalid values; existing native texture GPU tests cover 2x/4x storage, sample averaging, and depth rejection. Runtime owned PID44540, run `binding-validation-20260910-072853-377578b9`, confirms 1280x720 samples=4 at 07:28:55.489. `aa-check.png` shows readable new-game-data prompt over the title background, not gameplay. Stopped only owned44540 after path validation. Off runtime check was refused by the launch guard: non-owned PID43304 was running the new executable (start07:29:11), and was left untouched. Full-mission 4x fidelity/performance and interactive F1 save/restart are not yet verified.

Previous owned canvas-clip PID51740 was stopped after exact-path verification. Its completed capture set includes `loading-8.png`, a readable Assault Rifle tip and Now Loading; `loading-35.png` is the mothership intro, not a second loading verification. This corroborates the first loading screen with the user's configured asset root; second-transition visual verification remains incomplete. No user config/save files were edited.

Utility canvas classification caching was subsequently completed (see below). Live geometry-source comparisons remain and must not be described as removed.

### Cached native canvas classification

`RegisteredShader::ParameterPlan` now owns a binding token and `canvas_xy` classification per parameter. Source fingerprint, vertex entry name and parameter name are checked only when building that shader/material binding plan. `UploadParameters` consumes the flag with the existing native-resolution and 16-byte checks. Reverse vertex range plans still carry the original generation-validated binding tokens. This does not change guest constant reads, geometry-source comparisons, or shader/parameter lifetime rules.

Built `edf2027-native-canvas-metadata.exe`, SHA256 `845141EE3C5B0BDD0081EF3F2B3D776A789DBA10056598200ADA7334158B53C7`. All 18 tests passed (12.44s); classifier tests cover both Utility vertex entries, both XY constants, unrelated entries/parameters/sources, and vertex versus pixel groups. Existing scaling tests retain XY transformation and byte-preserved Z/W checks. No FPS benefit is claimed. Runtime visual verification is pending: non-owned games were running during this work (latest observed PID53380, `edf2027.exe`), and were left untouched. This candidate also includes the native AA selector from the preceding build.

Owned surface-ownership process 44900 remained live at the start of this work. Its log records second LoadMap at 3028.2104 ms. The final captured window shows the mission-failed screen, city/ants and 100/120 ammo; it is not a firing or successful mission-completion verification. Stopped only this exact-path owned diagnostic after capture. Logs and image remain in `out/native-bridge-run/binding-validation-20260910-063144-6bcf9a62`. Detailed model-stage analysis remains pending; no further load-speedup claim is made here.
