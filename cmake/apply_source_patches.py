#!/usr/bin/env python3
"""Apply pinned, checked compatibility edits to a build-tree source copy."""

import argparse
import hashlib
import json
from pathlib import Path


def digest(text):
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("source", type=Path)
    parser.add_argument("--destination", type=Path)
    args = parser.parse_args()
    root = args.source.resolve()
    patches = json.loads(args.manifest.read_text(encoding="utf-8"))
    patched_files = {}
    for patch in patches:
        path = (root / patch["path"]).resolve()
        if not path.is_relative_to(root):
            raise ValueError(f"Patch path escapes source directory: {path}")
        text = path.read_text(encoding="utf-8")
        current = digest(text)
        if current == patch["after"]:
            patched_files[path] = text.encode("utf-8")
            continue
        if current != patch["before"]:
            raise ValueError(f"Unexpected source revision or local edits: {path}")
        for edit in patch["edits"]:
            if text.count(edit["old"]) != 1:
                raise ValueError(f"Patch context is not unique: {path}")
            text = text.replace(edit["old"], edit["new"], 1)
        if digest(text) != patch["after"]:
            raise ValueError(f"Patch result did not match expected contents: {path}")
        patched_files[path] = text.encode("utf-8")

    if args.destination:
        destination = args.destination.resolve()
        # Copy only the build inputs. Write only changed bytes, so configuring
        # again does not rebuild every patched translation unit.
        for directory in ("src", "tools"):
            for path in (root / directory).rglob("*"):
                if not path.is_file() or "__pycache__" in path.parts:
                    continue
                data = patched_files.get(path.resolve())
                if data is None:
                    data = path.read_bytes()
                output = destination / path.relative_to(root)
                if not output.exists() or output.read_bytes() != data:
                    output.parent.mkdir(parents=True, exist_ok=True)
                    output.write_bytes(data)
    else:
        for path, data in patched_files.items():
            if path.read_bytes() != data:
                path.write_bytes(data)


if __name__ == "__main__":
    main()
