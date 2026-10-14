"""Write the game-file manifest the launcher verifies imports against.

usage: python -I make_manifest.py ASSETS_DIR OUT_FILE

One line per file: `<sha256> <size> <path>` with `/` separators, sorted.
Only hashes and sizes - no game content - so the file can be published.
A path listed twice accepts either content: the American disc's default.xex
(media 0DF2D3F6) holds the same program as the European one, with another
header, and every other file of the two discs is identical.
"""
import hashlib
import os
import sys

# Other releases of a file, written right after it: (path, size, sha256).
ALTERNATIVES = [
    ("default.xex", 11022336, "95008eb3166cc2720f763e26152c4bc0b7938379e8b5aff962c994db91731e7c"),  # USA
]


def main():
    root, out = sys.argv[1], sys.argv[2]
    rows = []
    for dirpath, _, files in os.walk(root):
        if os.path.relpath(dirpath, root).split(os.sep)[0] == "$SystemUpdate":
            continue  # console system update, not used by the port
        for name in files:
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, root).replace(os.sep, "/")
            h = hashlib.sha256()
            with open(full, "rb") as f:
                for chunk in iter(lambda: f.read(1 << 20), b""):
                    h.update(chunk)
            rows.append((rel, os.path.getsize(full), h.hexdigest()))
    rows.sort(key=lambda r: r[0].lower())
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("# Forza Motorsport 2 (title 4D5307EA; media 3E3180AE Europe, 0DF2D3F6 USA)"
                " - files the port was built from\n")
        for rel, size, digest in rows:
            f.write(f"{digest} {size} {rel}\n")
            for path, alt_size, alt_digest in ALTERNATIVES:
                if path == rel and alt_digest != digest:
                    f.write(f"{alt_digest} {alt_size} {rel}\n")
    print(f"{len(rows)} files, {sum(r[1] for r in rows) / 1e9:.2f} GB")


if __name__ == "__main__":
    main()
