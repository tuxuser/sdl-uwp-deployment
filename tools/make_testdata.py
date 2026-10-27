#!/usr/bin/env python3
"""Creates the dummy game files that assets/manifest.txt describes.

Usage: make_testdata.py [-o testdata] [--bad testdata-bad]

The content is generated deterministically, so the files always match the
committed manifest and do not need to be stored in git. --bad also writes
copies with one byte changed, for testing hash rejection.
"""
import argparse
import hashlib
import os

# Relative path and size. The large file makes upload progress visible.
FILES = [
    ("readme.txt", 1_200),
    ("config/settings.ini", 4_096),
    ("levels/level01.dat", 3 * 1024 * 1024 + 17),
    ("levels/level02.dat", 12 * 1024 * 1024),
    ("media/intro.bin", 64 * 1024 * 1024),
]


def content(path, size):
    """Bytes derived from the path, so every file differs."""
    seed = hashlib.sha256(path.encode()).digest()
    out = bytearray()
    counter = 0
    while len(out) < size:
        out += hashlib.sha256(seed + counter.to_bytes(8, "little")).digest()
        counter += 1
    return bytes(out[:size])


def write(root, path, data):
    full = os.path.join(root, *path.split("/"))
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, "wb") as f:
        f.write(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("-o", "--output", default="testdata")
    parser.add_argument("--bad", help="also write corrupted copies here")
    args = parser.parse_args()

    for path, size in FILES:
        data = content(path, size)
        write(args.output, path, data)
        if args.bad:
            corrupt = bytearray(data)
            corrupt[len(corrupt) // 2] ^= 0xFF
            write(args.bad, path, bytes(corrupt))
    print(f"{len(FILES)} files written to {args.output}")


if __name__ == "__main__":
    main()
