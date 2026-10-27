# lockplacer

Lines up where the lock sits on each door and chest model (**Lock on the door or chest**) outside the game, in [NifSkope](https://github.com/hexabits/nifskope/releases), instead of with the markup tool in VR. It writes the same `VRLockpicking_Locks.ini` keys as the markup tool.

CI builds `lockplacer.exe` (artifact `VRLockpicking-<run>-lockplacer`). To build it yourself:

```sh
git clone https://github.com/ousnius/nifly
cmake -S tools/lockplacer -B build-lockplacer -DNIFLY_DIR=$PWD/nifly -DBUILD_TESTING=OFF
cmake --build build-lockplacer
```

## Steps

1. **The list.** Start the game once with this version of the mod. A few seconds after the main menu it writes `Documents\My Games\Skyrim VR\SKSE\VRLockpicking_LockableModels.txt`: every door and chest model that your game and mods lock somewhere, the most often locked first.
2. **The models.** lockplacer reads them straight from the game's archives: give it your `Data` folder. Add the folders of mods with their own door or chest models (loose meshes or their `.bsa`), or single `.bsa` files, separated by `;`. A folder's loose meshes come first, then its archives, the game's later ones (`Update.bsa`, the DLC) before `Skyrim - Meshes0/1.bsa`, as in the game. Extracted meshes work too: a folder with `meshes\...` in it.
3. **Place the markers:**
   ```
   lockplacer place VRLockpicking_LockableModels.txt C:\VRLP\preview "D:\SteamLibrary\steamapps\common\SkyrimVR\Data;C:\Mods\SomeMod" VRLockpicking_Locks.ini
   ```
   Each model is copied to `C:\VRLP\preview\meshes\...` with a marker where the lock is now: what the INI says, else the mod's guess. Each line says where the model came from; a `missing` line means it was in none of them. The INI is optional. Copies that are already there keep their marker, so you can run it again after adding models. Never put the preview folder into `Data`.
4. **Line them up in NifSkope.** Open a copy. The marker is the node `VRLP_Lock`: a white frame the size of the lock's face, a red keyhole in the middle, a green arrow pointing into the door or chest, and a blue tick on top. Select `VRLP_Lock`, then Transform → Edit, and move and turn it until the keyhole sits where the lock should be. Save.
5. **Save them for the mod:**
   ```
   lockplacer read VRLockpicking_LockableModels.txt C:\VRLP\preview <Data>\SKSE\Plugins\VRLockpicking_Locks.ini
   ```
   (With Mod Organizer 2 the INI the game reads is in its `overwrite` folder.) The mod uses the new places at the next lockpicking.

A door needs only one side: the other side is its mirror through the door. Coordinates are the model's own, as NifSkope shows them.
