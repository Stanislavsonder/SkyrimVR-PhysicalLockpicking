# VR Physical Lockpicking (Skyrim VR)

An SKSE VR plugin that replaces Skyrim VR's flat lockpicking menu with lockpicking by hand. When you activate a locked door or chest, a lock appears in front of you. You put a lockpick into the keyhole with one hand and the tension wrench with the other, search for the sweet spot by feel, and turn the lock like a key. Everything you need to know comes through controller vibration.

The game's own rules stay in place:

- Lock levels, your Lockpicking skill and the lockpicking perks set the size of the sweet spot and how long a pick lasts. With Locksmith, the sweet spot is close to where your pick first goes in, as the vanilla menu starts the pick close to it. Unbreakable makes picks unbreakable, and so does carrying the Skeleton Key, which you then hold in your hand instead of a pick.
- A broken pick uses up a lockpick and gives the usual skill XP. An opened lock gives the vanilla XP and counts in the "Locks Picked" statistic.
- Wax Key, crime when you are seen picking an owned lock, and opening the container afterwards all work as in vanilla.
- Locks that need a key, and having no lockpicks, behave exactly as in vanilla.

Works with any controllers SteamVR supports (Meta Quest 2 / 3 / Pro, Valve Index, HTC Vive and others) with their default bindings. Tested together with HIGGS, PLANCK and VRIK.

## Requirements

- Skyrim VR 1.4.15
- [SKSEVR](https://skse.silverlock.org/)
- [VR Address Library for SKSEVR](https://www.nexusmods.com/skyrimspecialedition/mods/58101)
- Optional, for the settings menu: SkyUI VR and MCM Helper. Without them everything works the same and the settings are in the INI file.

## Installation

Install the archive with Mod Organizer 2 or Vortex. There is nothing to choose. By hand: copy the contents of its `core` folder into `Skyrim VR/Data`.

The mod adds `VRLockpicking.esp`, which only holds the settings menu's quest. Keep it enabled.

## How to pick a lock

1. Activate a locked door or chest. A lock appears in front of you and the game pauses until you are done. You stay where you are while lockpicking.
2. You hold the **lockpick** in your right hand and the **tension wrench** in your left (the other way round with **Pick in left hand**). Your hands themselves are hidden.
3. Put a tool into the keyhole by moving its tip into the keyhole's centre. You feel a click when it goes in. Once in, it stays in even if your hand drifts a little; pull it right out to take it out. Nothing else happens until both tools are in.
4. **Lockpick:** put it in with its handle pointing up, then sweep the handle clockwise down the right side of the keyhole, from straight up to straight down, to search. While you search you feel pulses: slow far from the sweet spot, faster near it, fastest in it.
5. **Tension wrench:** put it in with the back of your hand to the left, then turn it like a key until the back of your hand points up.
6. How far the lock turns tells you how close you are: all the way in the sweet spot (it opens), part of the way near it, only a wiggle far from it.
7. If you keep turning a lock that has stopped, the pick strains (a fast rattle, the lock shakes) and then breaks. Take the broken pick out and put it in again for a new one, and turn the tension wrench back to its start.
8. Under the lock you see how many lockpicks you have left. It is hidden while your picks can't break.
9. **B / Y** gives up, and so does walking away from the lock.

**Alternative mode** (settings, General): instead of steering the pick's handle round the keyhole, you search by turning your wrist, and the tension wrench turns from any start.

**Pick in left hand** mirrors everything: the pick searches along the left side, the tension wrench goes in with the back of your hand to the right, and the lock turns the other way.

### Lock Variations

Locks from Lock Variations, or any mod with `Data\*_LID.ini` files, get their own lock and lockpick models and sounds, as in the flat menu.

## Settings

With SkyUI VR and MCM Helper: journal → System → Mod Configuration → **VR Lockpicking**. There are three pages:

- **General:** on / off, alternative mode, pick in left hand, pause on lockpicking, opening containers, and difficulty (sweet spot size, pick strength).
- **Vibration:** overall strength, and how often and how strongly it pulses far from, near and in the sweet spot, and while a pick is about to break.
- **Debug:** debug log and view, the keyhole target, the assist ring (dots on the lock showing where your pick is, and optionally the sweet spot on easy locks), the model markup tool, crime detection, test chests and doors, and buttons to set up a test: lockpicks, the Skeleton Key, the lockpicking perks, and your Lockpicking skill.

Every setting, with comments, is also in `Data/SKSE/Plugins/VRLockpicking.ini`. Values set in the menu are saved to `Data/MCM/Settings/VRLockpicking.ini` and win over it.

### Pause on lockpicking

On by default: the game is paused while you pick a lock, the way menus pause it. Turn it off to let the game go on around you.

### Lining up models (markup tool)

The vanilla lock, lockpick and tension wrench come lined up. For a lock or tool from another mod, turn on the **Model markup tool** (Debug page) and pick a lock:

1. **Lock:** put the pick's tip on the keyhole's centre and press the left trigger, then on the keyhole's top, then press once more to save.
2. **Pick**, then 3. **tension wrench** (A / X moves on): hold the left trigger. The model stays still while you move your hand to where you want to hold it. Let go and it is saved.

Everything is saved per model in `Data/SKSE/Plugins/VRLockpicking_Locks.ini` (with Mod Organizer 2, in its `overwrite` folder). **Reset model markup** (Debug page) forgets all of it.

## Troubleshooting

- Turn on **Debug log**. The log is `Documents/My Games/Skyrim VR/SKSE/VRLockpicking.log`.
- If your VR Address Library is older than the mod expects, the mod only uses the game functions your library has. Optional features it can't use are turned off (the log says which). If an essential one is missing, you get the vanilla menu and a message asking you to update the [library](https://github.com/alandtse/skyrim_vr_address_library/releases).

## Building

The game-independent part (`src/core`) is plain C++ with unit tests, including a simulated player with shaky hands that has to open every lock level:

```sh
cmake -S . -B build-tests -DVRLP_BUILD_PLUGIN=OFF
cmake --build build-tests
ctest --test-dir build-tests
```

The plugin builds on Windows with MSVC and vcpkg, as in `.github/workflows/build.yml`:

```sh
git submodule update --init --recursive
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
cmake --build build --target VRLockpicking
python tools/package.py build/VRLockpicking.dll out/VRLockpicking.zip
```

It also cross-compiles on Linux with clang-cl and an `xwin` sysroot (`extern/CommonLibVR/cmake/toolchain-linux-clangcl.cmake`). Pushing a `v*` tag builds the archive and attaches it to a GitHub release.

### Layout

| Path | What |
|---|---|
| `src/core` | The lockpicking itself: lock and tool geometry, the simulation, vibration, settings, model fitting, Lock Variations files. No game code. |
| `src/plugin` | The SKSE plugin: intercepting the vanilla menu, reading the hands, drawing the models, pausing, perks, crime, the settings menu. |
| `tests` | Unit tests for `src/core`. |
| `package` | What goes into the archive: the INI, the settings menu, the FOMOD, the mod's own models. |
| `tools` | Packaging, the settings menu's ESP and script, `nifgen` (the mod's own models), an address-library audit. |
| `extern/CommonLibVR` | CommonLibSSE-NG with Skyrim VR support (submodule). |

## Credits

- [CommonLibSSE-NG](https://github.com/alandtse/CommonLibVR) (alandtse's fork with Skyrim VR support) and the [VR Address Library](https://github.com/alandtse/skyrim_vr_address_library).
- HIGGS by FlyingParticle, for the hand-space conventions and the frame hook spot this mod chains after.
- Lock Variations, whose `_LID.ini` format this mod reads.

## License

GPL-3.0 (see `LICENSE`), as required by CommonLibSSE-NG, which this plugin links against.
