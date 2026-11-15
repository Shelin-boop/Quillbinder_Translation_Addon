#!/usr/bin/env python3
"""Writes a Quillbinder add-on package (.qbpkg) from a staged folder.

    make-package.py <folder> <out.qbpkg>
      <folder>/manifest.json   id, name, version, description, author, whatsNew, minApp
      <folder>/payload/...     the add-on's files

The package format is the one of Quillbinder's developer guide (docs/developer-guide.md of the public source, sections
2 and 5): a ZIP file with manifest.json and payload/; the manifest lists every file of payload/ with its SHA-256 and
size. An add-on with Linux libraries names the runtime it was built for (the Qt inside Quillbinder's AppImage) and
needs at least the first program version with that runtime. Packages of this add-on are not signed (only Quillbinder's
own packages are); the program says so when it installs one.
"""
import hashlib
import json
import os
import sys
import zipfile

# Quillbinder's runtime since 0.80.0: Ubuntu 24.04 with The Qt Company's Qt 6.12.0 (APP_RUNTIME and APP_RUNTIME_SINCE
# in the CMakeLists.txt of the public source)
RUNTIME = "u2404-qt6.12-r1"
RUNTIME_SINCE = "0.80.0"


def version_key(v):
    return [int(x) if x.isdigit() else 0 for x in v.split(".")]


def main():
    folder, out = sys.argv[1], sys.argv[2]
    manifest = json.load(open(os.path.join(folder, "manifest.json"), encoding="utf-8"))
    manifest["format"] = 1
    manifest["kind"] = "addon"
    files, payload = [], []
    root = os.path.join(folder, "payload")
    for d, _, names in sorted(os.walk(root)):
        for n in sorted(names):
            p = os.path.join(d, n)
            rel = os.path.relpath(p, root).replace(os.sep, "/")
            data = open(p, "rb").read()
            e = {"path": rel, "sha256": hashlib.sha256(data).hexdigest(), "size": len(data)}
            if n.endswith(".so") or ".so." in n:
                e["executable"] = True
            files.append(e)
            payload.append((rel, data))
    manifest["files"] = files
    if any(f["path"].endswith(".so") or ".so." in f["path"] for f in files):
        manifest["runtime"] = RUNTIME
        if version_key(manifest.get("minApp", "0")) < version_key(RUNTIME_SINCE):
            manifest["minApp"] = RUNTIME_SINCE
    if os.path.exists(out):
        os.remove(out)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        z.writestr("manifest.json", json.dumps(manifest, indent=2, ensure_ascii=False).encode("utf-8"))
        for rel, data in payload:
            z.writestr("payload/" + rel, data)
    print(f"{out}  ({os.path.getsize(out) / 1e6:.2f} MB, {len(files)} files)")


if __name__ == "__main__":
    main()
