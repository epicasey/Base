#!/usr/bin/env python3
"""Post-build step (invoked from a project's top-level CMakeLists.txt 'app' target): assemble OTA/, the folder uploaded to
the OTA server (see Common.h's FIRMWARE_UPGRADE_URL and base/OTA_Update.cpp, which fetches firmware.json, then
GETs its "file" URL directly as the firmware image - so "file" must be a full URL, not a path).
Shared by every project that uses base/; each project calls it as ${CMAKE_SOURCE_DIR}/../base/make_ota_package.py.
base/upload_ota.cmd then uploads the result.
"""
import argparse
import re
import shutil
import sys
from pathlib import Path


def find_firmware_upgrade_url(common_header: Path) -> str:
    text = common_header.read_text(encoding="utf-8")
    m = re.search(r'#define\s+FIRMWARE_UPGRADE_URL\s+"([^"]+)"', text)
    if not m:
        sys.exit(f"Could not find FIRMWARE_UPGRADE_URL in {common_header}")
    return m.group(1)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--version", required=True, help="PROJECT_VER, e.g. 1.28")
    p.add_argument("--common-header", required=True, type=Path, help="Path to main/Common.h")
    p.add_argument("--bin", required=True, type=Path, help="Path to the just-built .bin")
    p.add_argument("--out-dir", required=True, type=Path, help="OTA output folder")
    args = p.parse_args()

    manifest_url = find_firmware_upgrade_url(args.common_header)
    # The manifest (firmware.json) and the firmware image live side by side on the server; the image's URL is
    # that same directory, with the just-built binary's own filename.
    bin_url = manifest_url.rsplit("/", 1)[0] + "/" + args.bin.name

    args.out_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(args.bin, args.out_dir / args.bin.name)
    (args.out_dir / "firmware.json").write_text(
        '{"version":%s,"file":"%s"}\n' % (args.version, bin_url), encoding="utf-8"
    )
    print(f"OTA package written to {args.out_dir}: {args.bin.name}, firmware.json (version {args.version}, file {bin_url})")


if __name__ == "__main__":
    main()
