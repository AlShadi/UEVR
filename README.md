# UEVR ![build](https://github.com/AlShadi/UEVR/actions/workflows/dev-release.yml/badge.svg)

Universal Unreal Engine VR Mod (4/5)

> **Note:** This repository is a fork of [praydog/UEVR](https://github.com/praydog/UEVR) with multiple stability, memory-safety, concurrency, Unreal Engine 5.5+ compatibility, scripting/plugin API, and frontend injector fixes applied.

## Fork Improvements & Fixes

- **Unreal Engine & Stereo Hooks**
  - Extended `UEVersion` detection and `IXRTrackingSystemHook` vtable dispatch to support **UE 5.5 – 5.7**, with string version overrides taking priority over disk metadata.
  - Fixed a stack buffer overflow in `FFakeStereoRenderingHook::calculate_stereo_projection_matrix` where an inverted double-precision check wrote a 128-byte `Matrix4x4d` into a 64-byte UE4 `Matrix4x4f`.
  - Fixed `RenderTargetPoolHook::find_free_element_hook_ue5` dropping its 5th parameter (`a5`), which shifted stack arguments in UE5 depth/render-target pooling.
  - Fixed `IXRTrackingSystemHook` overwriting `ResetPosition_index()` instead of hooking `ResetOrientation_index()`.
  - Fixed missing `break;` fallthrough in `UObjectHook::process_event_hook` that corrupted `ArrayProperty` buffers as `StrProperty`, throttled async class sorting, and fixed `SDKDumper` bitfield `BoolProperty` getter precedence.
- **D3D11 / D3D12 Rendering & VR Runtimes**
  - Fixed D3D12 `CommandContext::wait()` resetting in-flight `ID3D12CommandAllocator`s on `WAIT_TIMEOUT` (`DXGI_ERROR_DEVICE_REMOVED`), added missing mutex synchronization in `copy_region_stereo`, and fixed `fence_event` handle leaks.
  - Fixed per-frame COM reference leaks on `m_device` in `D3D11Hook::present` and `D3D12Hook::present`, `ID3D11DeviceContext` in `Framework::initialize`, and `ID3D11ClassInstance` in `D3D11Component`.
  - Fixed unmatched `ImGui::TreePop()` and `ImGui::EndGroup()` stack underflows in `VR::on_draw_sidebar_entry`, iterator invalidation in the OpenXR bindings UI, `NaN` thumbstick normalization UB on centered XInput sticks, and eye-transform bounds/mutex locking.
- **Core Framework, Threads & Plugin / Lua APIs**
  - Fixed `WindowFilter` worker thread exiting on its first empty tick (`return` vs `continue`), synchronized job queue access, and replaced blocking `GetWindowTextA` with `SendMessageTimeoutA(..., SMTO_ABORTIFHUNG)`.
  - Fixed a self-deadlock in `PluginLoader::attempt_unload_plugins()` when plugins call `remove_callback()` during `uevr_plugin_shutdown()`.
  - Fixed swapped `duration` and `amplitude` parameters in C++ `uevr::API::VR::trigger_haptic_vibration`, a 4-byte stack out-of-bounds read and quaternion component scrambling in Lua `MotionControllerState::set_rotation_offset`, `FMalloc` support for persistent Lua `FString` property assignments, and subdirectory preservation in `fs` bindings.
  - Replaced detached thread in `DInputHook` with `std::jthread`, wired `std::stop_token` into `XInputHook`, guarded `UEVRSharedMemory` against null dereferences, and pre-loaded `dbghelp.dll` in `ExceptionHandler`.
- **Frontend Injector (.NET 10) & Build / Test Suite**
  - Upgraded the WPF frontend to **.NET 10**, integrated `frontend` as a Git submodule, and added automated C++ (`uevr-tests`) and C# (`UEVR.Tests`) unit test suites plus VS Code build/publish tasks.
  - Fixed 64-bit `SIZE_T` (`nuint`) P/Invoke stack corruption in `WriteProcessMemory` / `VirtualAllocEx`, prevented freeing remote DLL path memory on `WaitForSingleObject` timeout, added recursive nested-ZIP DLL warning inspection and Zip-Slip protection in `GameConfig`, fixed `--attach=` auto-shutdown, and eliminated `Process` handle leaks and per-keystroke config I/O storms.

## Supported Engine Versions

4.8 - 5.7

## Links

- [Download (Stable release)](https://github.com/praydog/UEVR/releases)
- [Download (Nightly release)](https://github.com/praydog/UEVR-nightly/releases/latest)
- [Documentation](https://praydog.github.io/uevr-docs)
- [Flat2VR Discord](https://flat2vr.com)

## Features

- Full 6DOF support out of the box (HMD movement)
- Full stereoscopic 3D out of the box
- Native UE4/UE5 stereo rendering system
- Frontend GUI for easy process injection
- Supports OpenVR and OpenXR runtimes
- 3 rendering modes: Native Stereo, Synchronized Sequential, and Alternating/AFR
- Automatic handling of most in-game UI so it is projected into 3D space
- Optional 3DOF motion controls out of the box in many games, essentially emulating a semi-native VR experience
- Optional roomscale movement in many games, moving the player character itself in 3D space along with the headset
- User-authored UI-based system for adding motion controls and first person to games that don't support them
- In-game menu with shortcuts for adjusting settings
- Access to various CVars for fixing broken shaders/effects/performance issues
- Optional depth buffer integration for improved latency on some headsets
- Per-game configurations
- [C++ Plugin system](https://praydog.github.io/uevr-docs/plugins/getting_started.html) and [Blueprint support](https://praydog.github.io/uevr-docs/plugins/blueprint.html) for modders to add additional features like motion controls

## Getting Started

Before launching, ensure you have installed the .NET 10.0 Desktop Runtime (or SDK). It should tell you where to install it upon first open, but if not, you can [download it from here](https://dotnet.microsoft.com/en-us/download/dotnet/10.0). Most people should click x64 under .NET Desktop Runtime, next to Windows.

Download the latest release from the [Releases page](https://github.com/praydog/UEVR/releases)

1. Launch UEVRInjector.exe
2. Launch the target game
3. Locate the game in the process dropdown list
4. Select your desired runtime (OpenVR/OpenXR)
5. Toggle existing VR plugin nullification (if necessary)
6. Configure pre-injection settings
7. Inject

## To-dos before injection

1. Disable HDR (it will still work without it, but the game will be darker than usual if it is)
2. Start as administrator if the game is not visible in the list
3. Pass `-nohmd` to the game's command line and/or delete VR plugins from the game directory if the game contains any existing VR plugins
4. Disable any overlays that may conflict and cause crashes (Rivatuner, ASUS software, Razer software, Overwolf, etc...)
5. Disable graphical options in-game that may cause crashes or severe issues like DLSS Frame Generation
6. Consider disabling `Hardware Accelerated GPU Scheduling` in your Windows `Graphics settings`

## In-Game Menu

Press the **Insert** key or **L3+R3** on an XInput based controller to access the in-game menu, which opens by default at startup. With the menu open, hold **RT** for various shortcuts:

- RT + Left Stick: Move the camera left/right/forward/back
- RT + Right Stick: Move the camera up/down
- RT + B: Reset camera offset
- RT + Y: Recenter view
- RT + X: Reset standing origin

## Quick overview of rendering methods

### Native Stereo

When it works, it looks the best, performs the best (usually). Can cause crashes or graphical bugs if the game does not play well with it.

Temporal effects like TAA are fully intact. DLSS/FSR2 usually work completely fine with no ghosting in this mode.

Fully synchronized eye rendering. Works with the majority of games. Uses the actual stereo rendering pipeline in the Unreal Engine to achieve a stereoscopic image.

### Synchronized Sequential

A form of AFR. Can fix many rendering bugs that are introduced with Native Stereo. Renders two frames **sequentially** in a **synchronized** fashion on the same engine tick.

Fully synchronized eye rendering. Game world does not advance time between frames.

Looks normal but temporal effects like TAA will have ghosting/doubling effect. Motion blur will need to be turned off.

This is the first alternative option that should be used if Native Stereo is not working as expected or you are encountering graphical bugs.

**Skip Draw** skips the viewport draw on the next engine tick. Usually works the best but sometimes particle effects may not play at the correct speed.

**Skip Tick** skips the next engine tick entirely. Usually buggy but does fix particle effects and sometimes brings higher performance.

### AFR

Alternated Frame Rendering. Renders each eye on separate frames in an alternating fashion, with the game world advancing time in between frames. Causes eye desyncs and usually nausea along with it.

Not synchronized. Generally should not be used unless the other two are unusable in some way.
