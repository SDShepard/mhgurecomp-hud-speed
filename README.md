# HUD Speed — Forge plugin, version 1.0

One standalone ARM32 Forge plugin for MHGU 1.4.0 and MHGURecomp 0.10.1. It combines HUD animation speed, fractional nameplate timing and elapsed-time buff icon cycling. No custom runtime imports, game code, game data or assets are included. No upstream compatibility PR is required for the tested public release.

## Install

Close the game. Extract the precompiled ZIP beside `mhgurecomp.exe`. The installed file must be:

```
mods/hud-speed/romfs/nativeNX/plugins/hud-speed.nro
```

When upgrading from 0.1.0, move the old `mods/hud-timing` folder outside `mods` before extracting this release, so only one HUD Speed plugin is loaded. Remove any separate `buff-timing` plugin first. Move its entire mod folder outside `mods` for backup. Do not combine with the older custom runtime's built-in HUD speed hooks: that installation needs the native layer removed first. A stock public0.10.1 installation does not need executable or DLL replacement.

Press **Insert** to open Forge, then open the **HUD Speed** plugin panel. Use its single **HUD animation speed** slider (0–200%). Our integrated build also exposes Forge plugin panels through PC settings. This plugin does not add original-game menu rows to a stock public runtime.

**Settings are session-only in version 1.0 and start at100%.** The previous native `.ui-speed` file is not used. Persistent plugin preferences remain future work.

To uninstall, close the game and remove only `mods/hud-speed`. No save file or original game data is modified.

## Behavior

- Identified HUD resources follow the slider, including sharpness and Hunter Art animations. Unknown resources keep original timing.
- Menus, quest acceptance, top-screen notifications and the separate quest-popup resource keep their original speed.
- At100%, eligible buff icons cycle every700ms. At0%, cycling pauses but expired icons still update; gameplay buff durations are unchanged.
- Nameplate fractional progress is retained at high frame rates.
- Changing the slider accounts for pending buff time at the previous speed before applying the new speed.

0% pauses animation-dependent visual transitions too: some Hunter Art visual states can wait until animation resumes. Actual gauge quantities are read independently, but not every graphical state is visually qualified. Shared HUD/menu blink effects retain normal timing. Buff intervals over250ms are treated as suspension; this can pause cycling during sustained hitching. The buff clock has32slots; pathological capacity/reuse cases remain documented limitations.

The plugin validates original code before installing hooks. Unsupported or already modified targets are refused and logged. Later-loaded plugins that alter the same functions can still conflict; arbitrary mod combinations are not certified.

## Validation and status

Tested offline with the actual NRO loader, relocations, public Forge hook implementation, dispatcher/interpreter and captured original selector code. Tests cover resource classification, normalizer/register boundaries, nameplate timing, buff expiry, speed changes, malformed resources and modified-code refusal. Test data and original game-code extracts are not distributed.

This is a **user-test package**, not a claim of completed live visual validation. No automatic game launch or gameplay test was performed. In particular, please compare village quest acceptance at0%,100% and200%: the transition should retain its original speed while the HUD responds to the slider.

## Build

Requires Python3, Clang and LLD with ARM32 support. On the tested Windows toolchain:

```
python build.py --cc C:/path/to/clang.exe --output build
```

Output: `build/hud-speed/romfs/nativeNX/plugins/hud-speed.nro`. The builder verifies the complete import allowlist; only the public Forge/ImGui functions are permitted. No game dump is needed to build the plugin. Independent byte-identical rebuilds are expected with the same compiler/linker version.

The hooks validate MHGU1.4.0 entry fingerprints and use the original unmodified scheduler. The ARM assembly adapter preserves the GUI caller's integer/VFP state while returning the expected normalized delta. Public Horizon QueryMemory validates resource reads. No host-specific bridge is used.
