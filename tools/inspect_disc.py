"""Inspect a Drakengard PS2 disc image and extract the boot ELF.

Usage: python -I tools/inspect_disc.py <image.iso> <work_dir>

Writes into <work_dir>:
  disc_listing.txt   every file with LBA and size
  SYSTEM.CNF         copy of the boot configuration
  <boot elf name>    copy of the boot executable
  disc_report.json   machine-readable summary
The source image is opened read-only and never modified.
"""
from __future__ import annotations

import hashlib
import json
import os
import sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from iso9660 import Iso9660  # noqa: E402


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    image, work = sys.argv[1], sys.argv[2]
    os.makedirs(work, exist_ok=True)
    iso = Iso9660(image)

    entries = list(iso.walk())
    files = [e for e in entries if not e.is_dir]
    with open(os.path.join(work, "disc_listing.txt"), "w", encoding="utf-8") as out:
        for e in sorted(entries, key=lambda e: e.lba):
            kind = "DIR " if e.is_dir else "FILE"
            out.write(f"{kind} lba={e.lba:8d} size={e.size:11d} {e.path}\n")

    cnf_entry = iso.find("SYSTEM.CNF")
    if cnf_entry is None:
        print("SYSTEM.CNF not found")
        return 1
    cnf = iso.read_file(cnf_entry)
    with open(os.path.join(work, "SYSTEM.CNF"), "wb") as out:
        out.write(cnf)
    cnf_text = cnf.decode("ascii", "replace")
    boot_line = next((l for l in cnf_text.splitlines() if l.strip().upper().startswith("BOOT2")), "")
    # BOOT2 = cdrom0:\SLUS_xxx.xx;1
    boot_path = boot_line.split("=", 1)[1].strip().split(":", 1)[1].lstrip("\\").split(";")[0]
    boot_entry = iso.find(boot_path.replace("\\", "/"))
    elf = iso.read_file(boot_entry)
    elf_out = os.path.join(work, os.path.basename(boot_path))
    with open(elf_out, "wb") as out:
        out.write(elf)

    ext = Counter(os.path.splitext(e.path)[1].upper() or "(none)" for e in files)
    ext_bytes = Counter()
    for e in files:
        ext_bytes[os.path.splitext(e.path)[1].upper() or "(none)"] += e.size

    report = {
        "image": os.path.abspath(image),
        "image_size": os.path.getsize(image),
        "volume_id": iso.volume_id,
        "system_id": iso.system_id,
        "publisher": iso.publisher,
        "application": iso.application,
        "creation_date": iso.creation_date,
        "volume_blocks": iso.volume_blocks,
        "system_cnf": cnf_text,
        "boot_path": boot_path,
        "boot_elf": {
            "size": len(elf),
            "md5": hashlib.md5(elf).hexdigest(),
            "sha1": hashlib.sha1(elf).hexdigest(),
            "sha256": hashlib.sha256(elf).hexdigest(),
        },
        "file_count": len(files),
        "dir_count": len(entries) - len(files),
        "extensions": {k: {"count": ext[k], "bytes": ext_bytes[k]} for k in sorted(ext, key=lambda k: -ext_bytes[k])},
        "largest_files": [
            {"path": e.path, "size": e.size, "lba": e.lba}
            for e in sorted(files, key=lambda e: -e.size)[:25]
        ],
    }
    with open(os.path.join(work, "disc_report.json"), "w", encoding="utf-8") as out:
        json.dump(report, out, indent=2)
    print(json.dumps({k: v for k, v in report.items() if k not in ("largest_files",)}, indent=2))
    print("largest files:")
    for f in report["largest_files"]:
        print(f"  {f['size']:>11d}  {f['path']}")
    iso.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
