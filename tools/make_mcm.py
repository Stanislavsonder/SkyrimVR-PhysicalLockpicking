#!/usr/bin/env python3
"""Builds the files that register the MCM (Mod Configuration Menu) without the Creation Kit.

MCM Helper (with SkyUI VR) draws the menu from Data/MCM/Config/VRLockpicking/config.json. To
register it, SkyUI needs a quest with a script extending MCM_ConfigBase and a player alias
running SKI_PlayerLoadGameAlias. This writes:

  VRLockpicking.esp                  - that quest (same layout as SkyUI's ExampleConfigMenu.esp)
  Scripts/VRLockpicking_MCM.pex      - "Scriptname VRLockpicking_MCM extends MCM_ConfigBase"
  Scripts/Source/VRLockpicking_MCM.psc

usage: make_mcm.py <output dir>
"""
import pathlib
import struct
import sys

PLUGIN = "VRLockpicking.esp"
SCRIPT = "VRLockpicking_MCM"
PARENT = "MCM_ConfigBase"
QUEST_EDID = "VRLockpicking_MCMQuest"
DISPLAY_NAME = "VR Lockpicking"
QUEST_ID = 0x01000800  # first form of this plugin (master Skyrim.esm is index 00)
PLAYER_REF = 0x00000014
# Debug buttons' functions, implemented in the SKSE plugin (TestLocks.cpp, DebugCheats.cpp)
NATIVES = [
    "SpawnTestLocks", "RemoveTestLocks", "ResetMarkup",
    "DebugGiveLockpicks", "DebugToggleSkeletonKey",
    "DebugAddLockPerks", "DebugAddLocksmith", "DebugAddUnbreakable", "DebugAddWaxKey", "DebugAddQuickHands", "DebugRemovePerks",
    "DebugSkill10", "DebugSkill25", "DebugSkill50", "DebugSkill75", "DebugSkill100",
]
PSC = (
    f"Scriptname {SCRIPT} extends {PARENT}\n\n"
    "; The menu itself is defined in Data/MCM/Config/VRLockpicking/config.json (MCM Helper).\n\n"
    "; Debug buttons, implemented in the SKSE plugin.\n"
    + "".join(f"Function {n}() global native\n" for n in NATIVES)
)


# --------------------------------------------------------------------------------------------
# ESP (TES4 format, form version 44 = Skyrim SE / VR)
# --------------------------------------------------------------------------------------------
def zstring(s: str) -> bytes:
    return s.encode("cp1252") + b"\0"


def wstring(s: str) -> bytes:  # VMAD strings: uint16 length + chars, no terminator
    b = s.encode("cp1252")
    return struct.pack("<H", len(b)) + b


def sub(tag: str, data: bytes) -> bytes:
    return tag.encode("ascii") + struct.pack("<H", len(data)) + data


def record(tag: str, form_id: int, data: bytes, flags: int = 0) -> bytes:
    return tag.encode("ascii") + struct.pack("<IIIIHH", len(data), flags, form_id, 0, 44, 0) + data


def group(label: str, data: bytes) -> bytes:
    return b"GRUP" + struct.pack("<I4sIII", 24 + len(data), label.encode("ascii"), 0, 0, 0) + data


def vmad() -> bytes:
    out = struct.pack("<hhH", 5, 2, 1)  # version 5, object format 2, one script
    out += wstring(SCRIPT) + b"\0"  # name, flags: local
    out += struct.pack("<H", 1)  # one property
    out += wstring("ModName") + bytes([2, 1]) + wstring(DISPLAY_NAME)  # string, edited
    # quest fragments: unknown(int8) = 2, fragment count = 0, empty script file name
    out += bytes([2]) + struct.pack("<H", 0) + wstring("")
    # one alias (alias 0 = PlayerAlias) carrying SKI_PlayerLoadGameAlias
    out += struct.pack("<H", 1)
    out += struct.pack("<hhI", 0, 0, QUEST_ID)  # object (format 2): unused, alias id, form id
    out += struct.pack("<hhH", 5, 2, 1) + wstring("SKI_PlayerLoadGameAlias") + b"\0" + struct.pack("<H", 0)
    return out


def esp() -> bytes:
    quest = b"".join(
        [
            sub("EDID", zstring(QUEST_EDID)),
            sub("VMAD", vmad()),
            sub("FULL", zstring(QUEST_EDID)),
            # flags: start game enabled | run once | (as SkyUI's example), priority 0, type 0
            sub("DNAM", struct.pack("<HBBfI", 0x0111, 0, 0, 0.0, 0)),
            sub("NEXT", b""),
            sub("ANAM", struct.pack("<I", 1)),  # next alias id
            sub("ALST", struct.pack("<I", 0)),
            sub("ALID", zstring("PlayerAlias")),
            sub("FNAM", struct.pack("<I", 0)),
            sub("ALFR", struct.pack("<I", PLAYER_REF)),  # forced reference: the player
            sub("VTCK", struct.pack("<I", 0)),
            sub("ALED", b""),
        ]
    )
    body = group("QUST", record("QUST", QUEST_ID, quest))
    header = b"".join(
        [
            sub("HEDR", struct.pack("<fII", 1.7, 2, 0x801)),  # version, records + groups, next id
            sub("CNAM", zstring("VR Physical Lockpicking")),
            sub("SNAM", zstring("Registers the VR Lockpicking settings menu (needs SkyUI VR and MCM Helper).")),
            sub("MAST", zstring("Skyrim.esm")),
            sub("DATA", struct.pack("<Q", 0)),
            sub("INTV", struct.pack("<I", 1)),
        ]
    )
    # Light (ESL-flagged ESP): it holds one record with an id in the light range, so it needs no full
    # load order slot with Skyrim VR ESL Support. Without that plugin VR ignores the flag and loads it as a
    # regular ESP.
    return record("TES4", 0, header, flags=0x200) + body


# --------------------------------------------------------------------------------------------
# PEX (compiled Papyrus, big endian, version 3.2, game id 1 = Skyrim)
# --------------------------------------------------------------------------------------------
def pex() -> bytes:
    strings = [SCRIPT, "", PARENT, "hidden", "conditional", "None"] + NATIVES
    idx = {s: i for i, s in enumerate(strings)}

    def ws(s: str) -> bytes:
        b = s.encode("cp1252")
        return struct.pack(">H", len(b)) + b

    out = struct.pack(">IBBHQ", 0xFA57C0DE, 3, 2, 1, 0)
    out += ws(f"{SCRIPT}.psc") + ws("VRLockpicking") + ws("VRLockpicking")
    out += struct.pack(">H", len(strings)) + b"".join(ws(s) for s in strings)
    out += bytes([0])  # no debug info
    out += struct.pack(">H", 2)  # user flags, as the compiler always writes them
    out += struct.pack(">HB", idx["hidden"], 0) + struct.pack(">HB", idx["conditional"], 1)
    out += struct.pack(">H", 1)  # one object
    obj = struct.pack(">HHIH", idx[PARENT], idx[""], 0, idx[""])  # parent, docstring, flags, auto state
    obj += struct.pack(">HH", 0, 0)  # no variables, no properties
    obj += struct.pack(">H", 1) + struct.pack(">HH", idx[""], len(NATIVES))  # the empty state
    for name in NATIVES:
        # name; return type, docstring, user flags, flags (global | native), no params / locals / code
        obj += struct.pack(">HHHIBHHH", idx[name], idx["None"], idx[""], 0, 0x03, 0, 0, 0)
    out += struct.pack(">H", idx[SCRIPT]) + struct.pack(">I", 4 + len(obj)) + obj
    return out


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    out = pathlib.Path(sys.argv[1])
    (out / "Scripts" / "Source").mkdir(parents=True, exist_ok=True)
    (out / PLUGIN).write_bytes(esp())
    (out / "Scripts" / f"{SCRIPT}.pex").write_bytes(pex())
    (out / "Scripts" / "Source" / f"{SCRIPT}.psc").write_text(PSC, encoding="utf-8", newline="\r\n")
    print(f"wrote {PLUGIN}, Scripts/{SCRIPT}.pex to {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
