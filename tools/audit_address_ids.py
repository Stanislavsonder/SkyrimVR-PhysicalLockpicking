#!/usr/bin/env python3
"""List the Address Library IDs a built VRLockpicking.dll resolves, and check them.

Works on the clang-cl cross build (needs its .pdb), using llvm-pdbutil / llvm-objdump:

    audit_address_ids.py <VRLockpicking.dll> <VRLockpicking.pdb> [version-1-4-15-0.csv]

1. Finds REL::IDDB::id2offset in the PDB and the small wrapper that calls it.
2. Collects the immediate ID passed to every call of that wrapper.
3. Fails if an ID is not listed in src/plugin/AddressCheck.h (so it can't be gated), and,
   given an address library CSV, reports which IDs that library lacks.
"""
import pathlib
import re
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def tool(name: str) -> str:
    path = shutil.which(name)
    if not path:
        sys.exit(f"{name} not found on PATH")
    return path


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    dll, pdb = sys.argv[1], sys.argv[2]
    csv = sys.argv[3] if len(sys.argv) > 3 else None

    publics = subprocess.run([tool("llvm-pdbutil"), "dump", "--publics", pdb], capture_output=True, text=True).stdout
    m = re.search(r"id2offset@IDDB@REL@@[^\n]*\n\s*flags = function, addr = (\d+):(\d+)", publics)
    if not m:
        sys.exit("id2offset not found in PDB")
    sections = subprocess.run([tool("llvm-objdump"), "-h", dll], capture_output=True, text=True).stdout
    text_va = int(re.search(r"\.text\s+[0-9a-f]+\s+([0-9a-f]+)", sections).group(1), 16)
    id2offset = text_va + int(m.group(2))

    lines = subprocess.run([tool("llvm-objdump"), "-d", "--no-show-raw-insn", dll], capture_output=True, text=True).stdout.splitlines()

    def callers(target: int):
        needle = f"0x{target:x}"
        return [i for i, l in enumerate(lines) if ("call" in l or "jmp" in l) and needle in l]

    # the wrapper: the function containing the (single) call to id2offset
    sites = callers(id2offset)
    if len(sites) != 1:
        sys.exit(f"expected one id2offset call site, found {len(sites)}")
    i = sites[0]
    while i > 0 and not re.match(r"^\s*1[0-9a-f]+:\s+push", lines[i]) or (i > 0 and "int3" not in lines[i - 1]):
        i -= 1
    wrapper = int(lines[i].split(":")[0], 16)

    ids, unresolved = set(), 0
    for c in callers(wrapper):
        for j in range(c - 1, max(0, c - 15), -1):
            mm = re.search(r"mov[lq]?\s+\$0x([0-9a-f]+),\s*%(?:edx|rdx)", lines[j])
            if mm:
                ids.add(int(mm.group(1), 16))
                break
        else:
            unresolved += 1

    header = (ROOT / "src/plugin/AddressCheck.h").read_text()
    listed = {int(x) for x in re.findall(r"\b(\d{4,6})\b", header.split("namespace Ids")[1])}
    print(f"{len(ids)} IDs used, {unresolved} call sites without an immediate")
    ok = True
    if ids - listed:
        print("NOT gated in AddressCheck.h:", sorted(ids - listed))
        ok = False
    if csv:
        have = {int(l.split(",")[0]) for l in pathlib.Path(csv).read_text().splitlines()[2:] if l.split(",")[0].isdigit()}
        missing = sorted(ids - have)
        print("missing from", csv, ":", missing or "none")
    return 0 if ok and unresolved == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
