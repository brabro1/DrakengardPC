"""Minimal read-only ISO9660 reader for PS2 DVD images.

Opens the image strictly read-only. Used to list the disc, read SYSTEM.CNF and
extract individual files (e.g. the boot ELF) into the user's work directory.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import BinaryIO, Iterator

SECTOR = 2048


@dataclass
class Entry:
    path: str
    lba: int
    size: int
    is_dir: bool


class Iso9660:
    def __init__(self, path: str):
        self.path = path
        self.f: BinaryIO = open(path, "rb")
        pvd = self.read_sectors(16, 1)
        if pvd[0] != 1 or pvd[1:6] != b"CD001":
            raise ValueError("not an ISO9660 image (no primary volume descriptor at sector 16)")
        self.system_id = pvd[8:40].decode("ascii", "replace").strip()
        self.volume_id = pvd[40:72].decode("ascii", "replace").strip()
        self.volume_blocks = struct.unpack_from("<I", pvd, 80)[0]
        self.block_size = struct.unpack_from("<H", pvd, 128)[0]
        self.publisher = pvd[318:446].decode("ascii", "replace").strip()
        self.preparer = pvd[446:574].decode("ascii", "replace").strip()
        self.application = pvd[574:702].decode("ascii", "replace").strip()
        self.creation_date = pvd[813:830].decode("ascii", "replace")
        root = pvd[156:156 + 34]
        self.root_lba = struct.unpack_from("<I", root, 2)[0]
        self.root_size = struct.unpack_from("<I", root, 10)[0]

    def close(self) -> None:
        self.f.close()

    def read_sectors(self, lba: int, count: int) -> bytes:
        self.f.seek(lba * SECTOR)
        return self.f.read(count * SECTOR)

    def _dir_records(self, lba: int, size: int) -> Iterator[tuple[str, int, int, bool]]:
        data = self.read_sectors(lba, (size + SECTOR - 1) // SECTOR)
        off = 0
        while off < size:
            rec_len = data[off]
            if rec_len == 0:
                # Records never straddle sectors; skip to the next sector.
                off = (off // SECTOR + 1) * SECTOR
                continue
            ext_lba = struct.unpack_from("<I", data, off + 2)[0]
            ext_size = struct.unpack_from("<I", data, off + 10)[0]
            flags = data[off + 25]
            name_len = data[off + 32]
            name = data[off + 33:off + 33 + name_len]
            off += rec_len
            if name in (b"\x00", b"\x01"):
                continue
            yield name.decode("ascii", "replace"), ext_lba, ext_size, bool(flags & 2)

    def walk(self, lba: int | None = None, size: int | None = None, prefix: str = "") -> Iterator[Entry]:
        if lba is None:
            lba, size = self.root_lba, self.root_size
        for name, ext_lba, ext_size, is_dir in self._dir_records(lba, size):
            clean = name.split(";")[0]
            path = f"{prefix}/{clean}"
            yield Entry(path, ext_lba, ext_size, is_dir)
            if is_dir:
                yield from self.walk(ext_lba, ext_size, path)

    def find(self, path: str) -> Entry | None:
        want = path.upper().lstrip("/")
        for e in self.walk():
            if e.path.upper().lstrip("/") == want:
                return e
        return None

    def read_file(self, entry: Entry) -> bytes:
        self.f.seek(entry.lba * SECTOR)
        return self.f.read(entry.size)
