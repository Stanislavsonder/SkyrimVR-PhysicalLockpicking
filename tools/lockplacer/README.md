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
   Each model is copied to `C:\VRLP\preview\meshes\...` with a marker where the lock is now: what the INI says, else the mod's guess. Each line says where the model came from; a `missing` line means it was in none of them. The INI is optional. Copies that are already there are made again with their marker where you left it, so you can run it again after adding models or after a new lockplacer. The copies have no collision (NifSkope can fail to save it, and it isn't needed here). Never put the preview folder into `Data`.
4. **Line them up in NifSkope.** Open a copy. The marker is the node `VRLP_Lock`: the lock the mod draws, at its size in the game, with a red keyhole, a green arrow pointing into the door or chest, and a blue tick on top. (Without the lock model in the data it is a white frame the size of the lock's face.) Add the game's archives in NifSkope (Options → Settings → Resources → Archives) to see the textures. Select `VRLP_Lock`, then Transform → Edit, and move and turn it until the keyhole sits where the lock should be. Save. The game draws what is left of the lock on the marker:
   - all of it: the whole lock;
   - delete the node `VRLP_LockBody` under the marker (right click → Block → Remove Branch): only the keyhole and the ring round it, for a small chest or a safe where the whole lock is too big;
   - delete the ring's shapes (`VRLP_LockRing...`) too: only the keyhole, for a model with its own lock face (a Dwemer chest);
   - delete the marker itself: no lock on this model at all, it stays in front of you (a model that is locked but has nothing to pick).

   The marker's red, green and blue guides (`VRLP_LockMarker`) can be deleted too; they are only for lining up.
5. **Save them for the mod:**
   ```
   lockplacer read VRLockpicking_LockableModels.txt C:\VRLP\preview <Data>\SKSE\Plugins\VRLockpicking_Locks.ini
   ```
   (With Mod Organizer 2 the INI the game reads is in its `overwrite` folder.) The mod uses the new places at the next lockpicking.

A door needs only one side marked: the other side is then its mirror through the door. Where the mirror misses (the door's frame is part of the model, or the two sides differ), mark the other side too: copy the `VRLP_Lock` node (right click → Block → Copy Branch, then on the root node Paste Branch) and move the copy to the other side. The first marker in the file is the side marked, the second the other side (`vObjectKeyhole2`, `vObjectInto2`, `vObjectUp2`, `sObjectLock2`). NifSkope renames a pasted copy when it saves (`<root>:53`, `:54` ...); `read` finds it by its shapes, gives it its names back and saves them into the copy. In the game you get the lock of the side you stand on. Delete the copy and run `read` again to go back to the mirror. Coordinates are the model's own, as NifSkope shows them.
