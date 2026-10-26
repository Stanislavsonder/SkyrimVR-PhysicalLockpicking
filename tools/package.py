#!/usr/bin/env python3
"""Assemble the FOMOD-installable mod archive.

usage: package.py <VRLockpicking.dll> <output.zip> [VRLockpicking.pdb]
"""
import pathlib
import sys
import zipfile

import make_mcm

ROOT = pathlib.Path(__file__).resolve().parent.parent
PKG = ROOT / "package"


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    dll = pathlib.Path(sys.argv[1])
    out = pathlib.Path(sys.argv[2])
    pdb = pathlib.Path(sys.argv[3]) if len(sys.argv) > 3 else None
    ini = (PKG / "VRLockpicking.ini").read_text(encoding="utf-8")

    plugins = "SKSE/Plugins/"
    out.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(PKG / "fomod" / "info.xml", "fomod/info.xml")
        z.write(PKG / "fomod" / "ModuleConfig.xml", "fomod/ModuleConfig.xml")
        z.write(dll, "core/" + plugins + "VRLockpicking.dll")
        if pdb and pdb.exists():
            z.write(pdb, "core/" + plugins + "VRLockpicking.pdb")
        z.writestr("core/" + plugins + "VRLockpicking.ini", ini)
        z.write(ROOT / "README.md", "core/" + plugins + "VRLockpicking-README.md")
        z.write(ROOT / "LICENSE", "core/" + plugins + "VRLockpicking-LICENSE.txt")
        # Settings menu (MCM): needs SkyUI VR + MCM Helper; harmless without them.
        z.writestr("core/" + make_mcm.PLUGIN, make_mcm.esp())
        z.writestr(f"core/Scripts/{make_mcm.SCRIPT}.pex", make_mcm.pex())
        z.writestr(f"core/Scripts/Source/{make_mcm.SCRIPT}.psc", make_mcm.PSC.replace("\n", "\r\n"))
        z.write(PKG / "mcm" / "config.json", "core/MCM/Config/VRLockpicking/config.json")
        z.write(PKG / "mcm" / "settings.ini", "core/MCM/Config/VRLockpicking/settings.ini")
        # Settings menu and message texts per game language. Kept as UTF-8 in the repo; the game
        # reads UTF-16 LE with a byte order mark and CRLF line ends.
        for f in sorted((PKG / "translations").glob("VRLockpicking_*.txt")):
            text = f.read_text(encoding="utf-8").replace("\r\n", "\n").replace("\n", "\r\n")
            z.writestr("core/Interface/Translations/" + f.name, "﻿".encode("utf-16-le") + text.encode("utf-16-le"))
        # The mod's own models (debug blocks, keyhole target, assist ring, pick counter), made by tools/nifgen.
        for f in sorted((PKG / "assets").rglob("*")):
            if f.is_file():
                z.write(f, "core/" + f.relative_to(PKG / "assets").as_posix())
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
