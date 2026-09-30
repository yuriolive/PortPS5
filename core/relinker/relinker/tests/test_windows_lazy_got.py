"""Check that --lazy-binding GOT slots are valid DIR64 preferred-VA pointers.

Ported from AnyPS5 2c2fd2cd. With --lazy-binding an unresolved import gets a
lazy stub and the GOT slot is pre-patched to point at it. The PE base
relocation type used for the image is IMAGE_REL_BASED_DIR64, which only adds
(loadBase - ImageBase) to the 8 bytes already stored, so each slot must:

  1. hold the full 8-byte preferred VA (ImageBase + stubRva), not a bare RVA;
  2. have its own base relocation entry (.reloc used to be built before the
     lazy stubs were patched in, so it missed these slots).

Both fail on the old code: the slot held a 4-byte RVA and was not relocated, so
a module loaded away from its preferred base jumped into unmapped memory.
The ELF input is synthetic (built below); no game data is used.
"""

import struct
import subprocess
import sys
import tempfile
from pathlib import Path

IMAGE_BASE = 0x140000000


def build_lazy_import_elf():
    """Synthetic two-segment ELF with one unresolvable PLT import and one RELATIVE slot.

    Layout (file offset == virtual address):
      0x0000-0x0fff  R+X text: entry at 0x200 is `call [rip+GOT]; ret; nop`, rest zero
      0x1000-0x1fff  RW data: dynstr, dynsym, GOT, RELA, JMPREL, dynamic
    Text and data are separate so the linear instruction sweep only ever sees the
    zero-padded text (a single RWX segment makes the sweep run into table bytes).
    """
    image = bytearray(0x2000)
    image[:16] = b"\x7fELF\x02\x01\x01" + bytes(9)
    struct.pack_into("<HHIQQQIHHHHHH", image, 16, 3, 62, 1, 0x200, 64, 0, 0, 64, 56, 3, 64, 0, 0)
    # call [rip+0x1102] -> GOT slot at 0x1308 (rip after the 6-byte call is 0x206).
    image[0x200:0x208] = bytes([0xFF, 0x15, 0x02, 0x11, 0x00, 0x00, 0xC3, 0x90])
    # dynstr: "\0foo\0libc.prx\0" (an import needs a DT_NEEDED library).
    # dynsym: null symbol + undefined FUNC "foo" (name offset 1).
    image[0x1000:0x100E] = b"\x00foo\x00libc.prx\x00"
    struct.pack_into("<IBBHQQ", image, 0x1020 + 24, 1, 0x12, 0, 0, 0, 0)
    # RELA: one R_X86_64_RELATIVE (8) at 0x1310 -> 0x200 (a relocated data pointer).
    struct.pack_into("<QQq", image, 0x1320, 0x1310, 8, 0x200)
    # JMPREL: one R_X86_64_JUMP_SLOT (7) against symbol 1, GOT slot at 0x1308.
    struct.pack_into("<QQq", image, 0x1340, 0x1308, (1 << 32) | 7, 0)
    tags = [
        (5, 0x1000), (10, 14), (1, 5), (6, 0x1020), (11, 24),
        (7, 0x1320), (8, 24), (9, 24),
        (23, 0x1340), (2, 24), (20, 7), (3, 0x1300),
        (0x6100003F, 48),  # DT_SCE_SYMTABSZ: 2 symbols, lets code analysis bound dynsym
        (0, 0),
    ]
    for index, tag in enumerate(tags):
        struct.pack_into("<qQ", image, 0x1400 + index * 16, *tag)
    struct.pack_into("<IIQQQQQQ", image, 64, 1, 5, 0, 0, 0, 0x1000, 0x1000, 0x1000)
    struct.pack_into("<IIQQQQQQ", image, 120, 1, 6, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000)
    struct.pack_into("<IIQQQQQQ", image, 176, 2, 6, 0x1400, 0x1400, 0x1400,
                     len(tags) * 16, len(tags) * 16, 8)
    return image


def parse_pe(pe):
    """Return (size_of_image, sections{name: (rva, vsize, raw_off, raw_size)}, reloc_dir)."""
    pe_off = struct.unpack_from("<I", pe, 0x3C)[0]
    nsec = struct.unpack_from("<H", pe, pe_off + 6)[0]
    opt_size = struct.unpack_from("<H", pe, pe_off + 20)[0]
    opt = pe_off + 24
    size_of_image = struct.unpack_from("<I", pe, opt + 56)[0]
    reloc_rva, reloc_size = struct.unpack_from("<II", pe, opt + 112 + 5 * 8)
    sections = {}
    table = opt + opt_size
    for i in range(nsec):
        name, vsize, rva, rsize, roff = struct.unpack_from("<8sIIII", pe, table + i * 40)
        sections[name.rstrip(b"\0").decode()] = (rva, vsize, roff, rsize)
    return size_of_image, sections, (reloc_rva, reloc_size)


def rva_to_off(sections, rva):
    """Translate an RVA to a file offset using the section table."""
    for va, vsize, roff, rsize in sections.values():
        if va <= rva < va + max(vsize, rsize):
            return roff + rva - va
    raise AssertionError("RVA not in any section: %#x" % rva)


def dir64_targets(pe, sections, reloc_dir):
    """Decode the base relocation directory into a list of DIR64 target RVAs."""
    rva, size = reloc_dir
    assert size > 0, "missing base relocation directory"
    off = rva_to_off(sections, rva)
    end = off + size
    targets = []
    while off < end:
        page, block = struct.unpack_from("<II", pe, off)
        assert block >= 8
        for i in range((block - 8) // 2):
            entry = struct.unpack_from("<H", pe, off + 8 + i * 2)[0]
            if entry >> 12 == 10:  # IMAGE_REL_BASED_DIR64
                targets.append(page + (entry & 0xFFF))
        off += block
    return targets


def main():
    relinker = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="portps5-lazy-got-") as directory:
        work = Path(directory)
        source = work / "input.elf"
        output = work / "output.exe"
        source.write_bytes(build_lazy_import_elf())
        # check=False: the return code is asserted with full output below.
        result = subprocess.run(
            [str(relinker), "--windows", "--lazy-binding", str(source), str(output)],
            capture_output=True, text=True, timeout=20, check=False)
        assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
        pe = output.read_bytes()

        size_of_image, sections, reloc_dir = parse_pe(pe)
        entry_rva, entry_vsize, _, entry_rsize = sections[".entry"]
        entry_lo = IMAGE_BASE + entry_rva
        entry_hi = entry_lo + max(entry_vsize, entry_rsize)

        targets = dir64_targets(pe, sections, reloc_dir)
        values = {t: struct.unpack_from("<Q", pe, rva_to_off(sections, t))[0] for t in targets}
        assert len(set(targets)) == len(targets), ("duplicate DIR64 entries", targets)

        # Every DIR64 slot must hold a preferred VA inside the image.
        for target, value in values.items():
            assert IMAGE_BASE <= value < IMAGE_BASE + size_of_image, (
                "DIR64 slot %#x holds %#x, not a preferred-VA pointer" % (target, value))

        # Exactly one slot (the lazy GOT slot) must point at a lazy stub in .entry.
        stub_slots = [t for t, v in values.items() if entry_lo <= v < entry_hi]
        assert len(stub_slots) == 1, ("expected one relocated lazy GOT slot", values)
        # The RELATIVE slot must still be relocated alongside it.
        assert len(values) == 2, ("expected RELATIVE + lazy GOT relocations", values)
    print("Windows lazy GOT DIR64 test passed")


if __name__ == "__main__":
    main()
