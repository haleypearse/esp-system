#!/usr/bin/env python3
"""Serve a directory of firmware images/manifests over the LAN."""

from __future__ import annotations

import argparse
import http.server
from functools import partial


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("directory", nargs="?", default=".")
    p.add_argument("--port", type=int, default=8000)
    args = p.parse_args()

    handler = partial(http.server.SimpleHTTPRequestHandler, directory=args.directory)
    server = http.server.ThreadingHTTPServer(("0.0.0.0", args.port), handler)
    print(f"Serving {args.directory} on http://0.0.0.0:{args.port}/")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
