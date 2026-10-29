#!/usr/bin/env python3
"""List the Address Library IDs a built VRLockpicking.dll resolves, and check them.

Works on the clang-cl cross build and the MSVC build (needs the .pdb), using llvm-pdbutil /
llvm-objdump:

    audit_address_ids.py <VRLockpicking.dll> <VRLockpicking.pdb> [version-1-4-15-0.csv]

1. Finds REL::IDDB::id2offset in the PDB and its call sites.
2. Collects the immediate ID passed to each call, or, in a wrapper, to every call of the wrapper.
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

    image = pathlib.Path(dll).read_bytes()
    pe = int.from_bytes(image[0x3C:0x40], "little")
    count = int.from_bytes(image[pe + 6:pe + 8], "little")
    table = pe + 24 + int.from_bytes(image[pe + 20:pe + 22], "little")
    base = int.from_bytes(image[pe + 48:pe + 56], "little")  # PE32+ ImageBase

    def read_qword(va: int):
        rva = va - base
        for s in range(count):
            h = table + 40 * s
            vaddr, raw_size, raw = (int.from_bytes(image[h + o:h + o + 4], "little") for o in (12, 16, 20))
            if vaddr <= rva < vaddr + raw_size:
                return int.from_bytes(image[raw + rva - vaddr:raw + rva - vaddr + 8], "little")
        return None

    def immediate_id(c: int, a_byPointer: bool = False):
        for j in range(c - 1, max(0, c - 15), -1):
            mm = re.search(r"mov[lq]?\s+\$0x([0-9a-f]+),\s*%(?:edx|rdx)", lines[j])
            if mm:
                return int(mm.group(1), 16)
        if not a_byPointer:
            return None
        # MSVC: the wrapper takes the ID by pointer, a constant in the image or one on the stack
        for j in range(c - 1, max(0, c - 15), -1):
            mm = re.search(r"leaq\s+-?0x[0-9a-f]+\(%rip\),\s*%rcx\s+# 0x([0-9a-f]+)", lines[j])
            if mm:
                return read_qword(int(mm.group(1), 16))
            mm = re.search(r"leaq\s+(-?0x[0-9a-f]+\(%r[sb]p\)),\s*%rcx", lines[j])
            if mm:
                for k in range(j - 1, max(0, j - 15), -1):
                    st = re.search(r"mov[lq]?\s+\$0x([0-9a-f]+),\s*" + re.escape(mm.group(1)), lines[k])
                    if st:
                        return int(st.group(1), 16)
                return None
        return None

    def function_start(c: int) -> int:
        i = c
        while i > 0 and ("int3" in lines[i] or "int3" not in lines[i - 1]):
            i -= 1
        return int(lines[i].split(":")[0], 16)

    # A call to id2offset with the ID as an immediate (MSVC inlines some); any other is in a
    # wrapper (the only one with clang-cl) whose callers pass the ID.
    sites = callers(id2offset)
    if not sites:
        sys.exit("no id2offset call site found")
    ids, unresolved, wrappers = set(), 0, set()
    for c in sites:
        found = immediate_id(c)
        if found is not None:
            ids.add(found)
        else:
            wrappers.add(function_start(c))
    for wrapper in wrappers:
        for c in callers(wrapper):
            found = immediate_id(c, True)
            if found is not None:
                ids.add(found)
            else:
                unresolved += 1
    print(f"{len(sites)} id2offset call site(s), {len(wrappers)} wrapper(s)")

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
