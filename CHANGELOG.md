# Changelog

## Unreleased

- **Lock on the door or chest** (General page, off by default): the lock sits on the door or chest you pick instead of in front of you, on the side of the door you stand. Doors and chests that the game's plugins never lock keep it in front of you. With it, **Walk while picking** (on by default) lets you walk and turn while you pick; the game is then not paused. Small chests and safes can show only the keyhole instead of the whole lock. A door's other side shows the mirror of the side marked, or its own place when that side is marked too.
- Markup tool: a new first step places the lock on the door or chest (carried with the left hand), saved per model; on a door's other side it marks that side on its own. The number pad fine-tunes every step: moves, turns, and the tools' size.
- **Spawn test locks** now places every chest and door model that your game locks somewhere, a batch at a time (press again for the next batch).

## 1.0.1

- The settings menu and the mod's messages in English, French, German, Italian, Spanish, Polish, Russian, Japanese and Chinese (the game's language).
- Fixed: installing with Mod Organizer 2 could crash it. The archive no longer has an installer; it is laid out as the `Data` folder, so mod managers install it directly.
- `VRLockpicking.esp` is now flagged as light (ESPFE). With Skyrim VR ESL Support it takes no load order slot; without it, it loads as a normal plugin as before.

## 1.0.0

First release.

- Lockpicking by hand in Skyrim VR instead of the flat menu: a lockpick and a tension wrench in your hands, a lock in front of you, the sweet spot found by feel through controller vibration.
- Two control styles: turning your wrist (alternative mode, default) or steering the pick's handle round the keyhole (like the vanilla minigame, mirrored for a pick in the left hand).
- Vanilla rules: lock levels, skill, perks (sweet spot, Locksmith, Unbreakable, Wax Key), the Skeleton Key, breaking picks, XP, the "Locks Picked" statistic, crime, opening the container.
- The game pauses while you pick a lock (optional), you stay in place, and your hands are hidden so only the tools show.
- A lockpick counter over the lock shows how many picks you have left (hidden while picks can't break).
- Lock Variations support: its lock and lockpick models and sounds per lock.
- Settings menu (MCM Helper) with General, Vibration and Debug pages; every setting also in the INI.
- Debug tools: debug view, keyhole target, assist ring, model markup tool, test chests and doors.
