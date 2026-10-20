#!/usr/bin/env python3
"""Create a small device-pull OTA manifest for an ESP System release."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from urllib.parse import urljoin


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("firmware", type=Path)
    p.add_argument("--product", required=True)
    p.add_argument("--version", required=True)
    p.add_argument("--hardware", required=True)
    p.add_argument("--base-url", required=True, help="Base URL ending in '/'")
    p.add_argument("--notes", default="")
    p.add_argument("--output", type=Path, default=Path("manifest.json"))
    args = p.parse_args()

    image = args.firmware.resolve()
    if not image.is_file() or image.stat().st_size <= 0:
        p.error(f"firmware must be an existing non-empty file: {image}")
    if args.product.strip() in ("", "*") or args.hardware.strip() in ("", "*"):
        p.error("--product and --hardware must identify one exact target; wildcards are not accepted")
    base = args.base_url if args.base_url.endswith("/") else args.base_url + "/"
    data = {
        "product": args.product,
        "version": args.version,
        "hardware": args.hardware,
        "url": urljoin(base, image.name),
        "size": image.stat().st_size,
        "sha256": sha256(image),
        "notes": args.notes,
    }
    args.output.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(data, indent=2))


if __name__ == "__main__":
    main()
