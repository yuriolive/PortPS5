"""Check that --to-intel lowers AMD-only instructions inside sce_module guest modules.

Runs the real relinker on a synthetic executable plus a synthetic guest module
for the Linux (ELF) and Windows (PE) outputs. The module's entry point runs
straight-line code containing an immediate-form INSERTQ (a stub site) and a
4-byte register-form EXTRQ (shorter than a jump, so the site is extended over
the following NOP). The test asserts that both sites become `jmp rel32` into a
stub that returns after the whole original site. All bytes are synthetic.
"""

import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from test_linux_load_alignment import fixture as executable_fixture

# INSERTQ xmm3, xmm3, 8, 8 (6 bytes, needs an out-of-line stub) at 0x1000.
INSERTQ_SITE = bytes.fromhex("f2 0f 78 db 08 08")
# EXTRQ xmm1, xmm2 (4 bytes) at 0x1006, followed by a NOP the converter moves.
EXTRQ_SITE = bytes.fromhex("66 0f 79 ca")
CODE = INSERTQ_SITE + EXTRQ_SITE + b"\x90\xc3"
# (offset inside the code, length of the original site as the converter records it)
SITES = ((0, len(INSERTQ_SITE)), (len(INSERTQ_SITE), len(EXTRQ_SITE) + 1))
PLAIN_CODE = b"\x90" * len(CODE)
JMP_SIZE = 5


def guest_fixture(code):
    """Build a synthetic guest ELF whose entry point runs `code` and returns."""
    image = bytearray(0xA00)
    image[:16] = b"\x7fELF\x02\x01\x01" + bytes(9)
    struct.pack_into("<HHIQQQIHHHHHH", image, 16, 3, 62, 1, 0x1000, 64, 0, 0, 64, 56, 3, 64, 0, 0)
    struct.pack_into("<IIQQQQQQ", image, 64, 1, 5, 0x400, 0x1000, 0x1000, 0x100, 0x100, 0x100)
    struct.pack_into("<IIQQQQQQ", image, 120, 1, 6, 0x600, 0x2000, 0x2000, 0x400, 0x400, 0x100)
    tags = [
        (5, 0x2200),
        (10, 1),
        (6, 0x2220),
        (11, 24),
        (4, 0x2240),
        (7, 0x2300),
        (8, 0),
        (9, 24),
        (0, 0),
    ]
    struct.pack_into(
        "<IIQQQQQQ", image, 176, 2, 6, 0x600, 0x2000, 0x2000, len(tags) * 16, len(tags) * 16, 8
    )
    image[0x400:0x500] = b"\x90" * 0x100
    image[0x400 : 0x400 + len(code)] = code
    for index, tag in enumerate(tags):
        struct.pack_into("<qQ", image, 0x600 + index * 16, *tag)
    struct.pack_into("<II", image, 0x840, 1, 1)
    return image


def main_fixture():
    """Shift the shared executable fixture so it does not overlap the guest module."""
    image = executable_fixture()
    struct.pack_into("<Q", image, 24, 0x4000)
    struct.pack_into("<QQ", image, 64 + 16, 0x4000, 0x4000)
    struct.pack_into("<QQ", image, 120 + 16, 0x4600, 0x4600)
    for index in (0, 2, 4):
        (address,) = struct.unpack_from("<Q", image, 0x4600 + index * 16 + 8)
        struct.pack_into("<Q", image, 0x4600 + index * 16 + 8, address + 0x4000)
    return image


def elf_loads(data):
    """Return the PT_LOAD program headers of an ELF file."""
    (offset,) = struct.unpack_from("<Q", data, 32)
    size, count = struct.unpack_from("<HH", data, 54)
    headers = [
        struct.unpack_from("<IIQQQQQQ", data, offset + index * size) for index in range(count)
    ]
    return [header for header in headers if header[0] == 1]


def elf_bytes_at(data, loads, address, size):
    """Read `size` bytes at a virtual address inside an executable ELF segment."""
    for _, flags, offset, mapped, _, file_size, _, _ in loads:
        if flags & 1 and mapped <= address and address + size <= mapped + file_size:
            start = offset + address - mapped
            return data[start : start + size]
    raise AssertionError(f"Executable ELF address is unmapped: {address:#x}")


def pe_sections(data):
    """Return (name, rva, raw_size, raw_offset, flags) for each PE section."""
    (header,) = struct.unpack_from("<I", data, 0x3C)
    (count,) = struct.unpack_from("<H", data, header + 6)
    (optional_size,) = struct.unpack_from("<H", data, header + 20)
    offset = header + 24 + optional_size
    sections = []
    for index in range(count):
        current = offset + index * 40
        name = data[current : current + 8].split(b"\0", 1)[0]
        _, rva, raw_size, raw_offset = struct.unpack_from("<IIII", data, current + 8)
        (flags,) = struct.unpack_from("<I", data, current + 36)
        sections.append((name, rva, raw_size, raw_offset, flags))
    return sections


def pe_bytes_at(data, sections, address, size):
    """Read `size` bytes at an RVA inside an executable PE section."""
    for _, rva, raw_size, raw_offset, flags in sections:
        if flags & 0x20000000 and rva <= address and address + size <= rva + raw_size:
            start = raw_offset + address - rva
            return data[start : start + size]
    raise AssertionError(f"Executable PE RVA is unmapped: {address:#x}")


def check_trampoline(read, site_address, length):
    """Assert the site is `jmp stub` plus NOP padding and the stub returns after the site."""
    patched = read(site_address, length)
    assert patched[0] == 0xE9, patched.hex()
    assert patched[JMP_SIZE:] == b"\x90" * (length - JMP_SIZE), patched.hex()
    stub = site_address + JMP_SIZE + struct.unpack_from("<i", patched, 1)[0]
    # Stub bodies are 16-byte aligned (their constants use aligned m128 operands).
    assert stub % 16 == 0, hex(stub)
    # Scan the stub for its return branch: the first E9 whose target is the continuation.
    body = read(stub, 0x100)
    expected = site_address + length
    for offset in range(len(body) - JMP_SIZE):
        if body[offset] != 0xE9:
            continue
        target = stub + offset + JMP_SIZE + struct.unpack_from("<i", body, offset + 1)[0]
        if target == expected:
            return
    raise AssertionError(f"stub at {stub:#x} never returns to {expected:#x}")


def run_case(work, windows, has_stub, relinker):
    """Relink one executable plus guest module and verify the module output."""
    case = work / ("windows" if windows else "linux") / ("stub" if has_stub else "plain")
    # The relinker writes converted modules to <output dir>/sce_module, which must
    # not be the source module directory, so input and output live in sibling folders.
    module_dir = case / "in" / "sce_module"
    module_dir.mkdir(parents=True)
    (case / "out").mkdir()
    source = case / "in" / "input.elf"
    output = case / "out" / ("output.exe" if windows else "output.elf")
    source.write_bytes(main_fixture())
    (module_dir / "sample.prx").write_bytes(guest_fixture(CODE if has_stub else PLAIN_CODE))
    arguments = [str(relinker), "--to-intel"]
    if windows:
        arguments.append("--windows")
    # check=False: the return code is asserted below with the full output.
    result = subprocess.run(
        [*arguments, str(source), str(output)],
        capture_output=True,
        text=True,
        timeout=30,
        check=False,
    )
    assert result.returncode == 0, (windows, has_stub, result.stdout, result.stderr)
    produced = sorted((case / "out" / "sce_module").glob("*.guest.prx"))
    assert len(produced) == 1, produced
    data = produced[0].read_bytes()
    if windows:
        assert data[:2] == b"MZ", (windows, has_stub)
        sections = pe_sections(data)
        code = next(section for section in sections if section[0] == b".elf0")
        bases = [code[1] + offset for offset, _ in SITES]

        def read(address, size):
            return pe_bytes_at(data, sections, address, size)

        assert any(section[0] == b".amdstub" for section in sections) == has_stub
    else:
        assert data[:4] == b"\x7fELF", (windows, has_stub)
        loads = elf_loads(data)
        bases = [0x1000 + offset for offset, _ in SITES]

        def read(address, size):
            return elf_bytes_at(data, loads, address, size)

    for base, (_, length) in zip(bases, SITES, strict=True):
        if has_stub:
            check_trampoline(read, base, length)
        else:
            assert read(base, length) == b"\x90" * length


def main():
    """Run the four (OS, stub-or-plain) cases against the relinker binary."""
    relinker = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="portps5-guest-intel-") as directory:
        for windows in (False, True):
            for has_stub in (False, True):
                run_case(Path(directory), windows, has_stub, relinker)
    print("Guest Intel trampoline integration tests passed")


if __name__ == "__main__":
    main()
