#!/usr/bin/env python3
"""Writes a manifest for every file under a folder.

Usage: make_manifest.py <folder> [-o assets/manifest.txt]

Each line is "<sha256-hex> <size-bytes> <relative/path>", sorted by path, with
'/' as the separator.
"""
import argparse
import hashlib
import os
import sys


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("folder")
    parser.add_argument("-o", "--output", default="assets/manifest.txt")
    args = parser.parse_args()

    lines = []
    for dirpath, dirnames, filenames in os.walk(args.folder):
        dirnames.sort()
        for name in sorted(filenames):
            path = os.path.join(dirpath, name)
            relative = os.path.relpath(path, args.folder).replace(os.sep, "/")
            if "\\" in relative or any(part in ("", ".", "..") for part in relative.split("/")):
                sys.exit(f"unsupported path: {relative}")
            lines.append(f"{sha256_of(path)} {os.path.getsize(path)} {relative}")
    lines.sort(key=lambda line: line.split(" ", 2)[2])
    # The app matches paths case-insensitively, like Windows does.
    seen = set()
    for line in lines:
        key = line.split(" ", 2)[2].lower()
        if key in seen:
            sys.exit(f"paths differ only in case: {key}")
        seen.add(key)
    with open(args.output, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print(f"{len(lines)} files written to {args.output}")


if __name__ == "__main__":
    main()
