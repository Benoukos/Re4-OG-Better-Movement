# RE4 OG Aim Walk / TPS

Gameplay plugin for **Resident Evil 4 (2005), Steam Ultimate HD Edition**.

## Controls and settings

- **F1** opens/closes the settings window.
- **F1** opens an interactive settings window with aim/knife speed, camera sensitivity, and a vertical pitch limit slider (default 45° each way), plus movement/camera toggles. While it is open, the cursor is unlocked and Leon's input dispatch is paused; **F1** or **Esc** closes it. Settings save beside the plugin in `RE4OGAimWalk.ini`.
- Outside aim, horizontal and vertical mouse movement orbit the game's original eye/target pair around Leon's follow-camera pivot. Vertical orbit uses the native target height as its center to avoid swinging around floor level. WASD movement is relative to that camera; Leon turns toward the chosen direction and uses the game's native walk/run animation.
- **AIM**/**KNIFE** use the game's original camera pivot, pitch, laser, attack direction, and upper-body pose. On entering aim, Leon snaps once to the TPS camera's horizontal heading. Native mouse input controls aiming with the game's AIM acceleration bypassed; the plugin does not alter vertical aim or pose.
- Aim/knife movement lets Leon walk forward, backward, or strafe relative to his facing while the game is in its weapon-ready state. Movement reads physical WASD so mouse aiming cannot interrupt the direction input; the knife action remains bound through the game's own controls.
- The game's dedicated 180° quick-turn routine is suppressed for Leon, regardless of whether it is triggered by Q, Shift+S, or a remapped binding.

## Current state

The x86 plugin builds successfully. The free camera orbit is enabled by default and can be toggled in F1; it applies only outside AIM/KNIFE. Its vertical pitch is clamped to the configured limit, default ±45°. During AIM/KNIFE, the game's original vertical camera and body pose remain in control. The aim/knife movement currently changes Leon's position without adding a lower-body animation, so the legs may look planted while aiming.

The plugin checks the executable timestamp and only hooks Steam UHD `bio4.exe` timestamp `0x5A6192E5`. It writes `RE4OGAimWalk.log` beside the plugin with hook/load status. Aim-walk default speed is 1800 and the settings slider reaches 3000.

## Build

Requires CMake and an x86 Windows C/C++ toolchain. MinHook is vendored under `third_party/minhook` with its upstream BSD-2-Clause license.

With Visual Studio 2022 and the Windows SDK:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release
```

The build produces the regular `RE4OGAimWalk.asi` for standalone use, plus `re4bettermovement.dll` and a proxy `dinput8.dll` for COOP. The COOP proxy forwards all six DirectInput exports to the original COOP DLL, saved as `RE4CoopDinput8.dll`, then starts the movement plugin directly. This avoids depending on the COOP loader discovering an external ASI.

For the COOP setup, run `install-coop.ps1`. It preserves the COOP DLL as `RE4CoopDinput8.dll` and installs the proxy and movement plugin beside `bio4.exe`. The proxy writes `re4bettermovement-loader.log` so startup and plugin load failures are visible.

To uninstall the installed files safely:

```powershell
.\uninstall.ps1
```


