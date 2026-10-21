#!/usr/bin/env python3
"""Validate ESP System OTA partition tables, including flash coredump storage."""
from __future__ import annotations
import argparse
import csv
from pathlib import Path


def number(value: str) -> int | None:
    value = value.strip()
    return int(value, 0) if value else None


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path)
    parser.add_argument("--flash-size", required=True, type=lambda x: int(x, 0))
    args = parser.parse_args()
    partitions = []
    with args.csv.open(newline="", encoding="utf-8") as handle:
        rows = csv.reader(line for line in handle if line.strip() and not line.lstrip().startswith("#"))
        for row in rows:
            if len(row) < 5:
                parser.error(f"invalid partition row: {row}")
            name, kind, subtype, offset_text, size_text = [v.strip() for v in row[:5]]
            offset, size = number(offset_text), number(size_text)
            if offset is None or size is None or size <= 0:
                parser.error(f"{name}: offset and positive size must be explicit")
            end = offset + size
            if end > args.flash_size:
                parser.error(f"{name}: ends at 0x{end:X}, beyond flash size 0x{args.flash_size:X}")
            partitions.append((offset, end, name, kind, subtype))
    partitions.sort()
    for previous, current in zip(partitions, partitions[1:]):
        if current[0] < previous[1]:
            parser.error(f"partition overlap: {previous[2]} and {current[2]}")
    ota_apps = {p[2]: p for p in partitions if p[3] == "app" and p[4] in ("ota_0", "ota_1")}
    if set(ota_apps) != {"ota_0", "ota_1"}:
        parser.error("requires app partitions named ota_0 and ota_1")
    ota0_size = ota_apps["ota_0"][1] - ota_apps["ota_0"][0]
    ota1_size = ota_apps["ota_1"][1] - ota_apps["ota_1"][0]
    if ota0_size != ota1_size:
        parser.error(f"OTA slots must have equal sizes (ota_0=0x{ota0_size:X}, ota_1=0x{ota1_size:X})")

    ota_data = [p for p in partitions if p[3] == "data" and p[4] == "ota"]
    if len(ota_data) != 1 or ota_data[0][1] - ota_data[0][0] < 0x2000:
        parser.error("requires one OTA data partition of at least 0x2000 bytes")

    coredump = [p for p in partitions if p[3] == "data" and p[4] == "coredump"]
    if len(coredump) != 1 or coredump[0][1] - coredump[0][0] < 0x10000:
        parser.error("requires one data,coredump partition of at least 0x10000 bytes (64 KiB)")

    print(f"OK: {args.csv}: {len(partitions)} partitions, flash bound 0x{args.flash_size:X}, "
          f"OTA slots: ota_0=0x{ota0_size:X}, ota_1=0x{ota1_size:X}, "
          f"coredump=0x{coredump[0][1]-coredump[0][0]:X}")


if __name__ == "__main__":
    main()
