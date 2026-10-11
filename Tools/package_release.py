#!/usr/bin/env python3
"""Atomically stage the complete MixAgent bundle, standalone, image and provenance."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile

from vst3_gate import GateError, digest, discover, native_header, snapshot

JUCE_COMMIT = "f72bad64d29715216226685810c5196bd0d79d77"


def all_files(directory):
    result = []
    for path in sorted(directory.rglob("*")):
        if path.is_symlink() or not (path.is_file() or path.is_dir()):
            raise GateError(f"Unsafe package entry: {path}")
        if path.is_file():
            result.append(path)
    return result


def verify(directory, expected_source_commit=None):
    manifest = json.loads((directory / "build-manifest.json").read_text(encoding="utf-8"))
    if manifest.get("schemaVersion") != 1 or manifest.get("product") != "MixAgent" or \
            manifest.get("juceCommit") != JUCE_COMMIT or not re.fullmatch(r"[0-9a-f]{40}", manifest.get("sourceCommit", "")):
        raise GateError("Invalid release provenance")
    if expected_source_commit is not None and manifest["sourceCommit"] != expected_source_commit.lower():
        raise GateError("Release manifest source differs from the expected source commit")
    files = manifest["files"]
    expected = {item["path"]: item for item in files}
    if len(expected) != len(files):
        raise GateError("Duplicate manifest file paths")
    actual = {p.relative_to(directory).as_posix(): p for p in all_files(directory)
              if p.name not in ("build-manifest.json", "checksums.sha256") or p.parent != directory}
    if set(actual) != set(expected):
        raise GateError("Staged file set differs from manifest")
    for relative, path in actual.items():
        if path.stat().st_size != expected[relative]["sizeBytes"] or digest(path) != expected[relative]["sha256"]:
            raise GateError(f"Staged file integrity mismatch: {relative}")
    checksums = "".join(f"{item['sha256']}  {item['path']}\n" for item in files)
    if (directory / "checksums.sha256").read_text(encoding="utf-8") != checksums:
        raise GateError("Checksum report differs from manifest")
    if {p.name for p in directory.iterdir()} != {"vst3", "standalone", "mixagent-ui.png", "build-manifest.json", "checksums.sha256"}:
        raise GateError("Unexpected package layout")
    platform = manifest["platform"]
    expected_standalone = "standalone/MixAgent.exe" if platform == "windows-x64" else "standalone/MixAgent"
    if manifest["standalonePath"] != expected_standalone:
        raise GateError("Unexpected standalone path")
    bundles = discover(None, directory / "vst3", [], "Release", ["MixAgent"])
    executable = directory / manifest["standalonePath"]
    if snapshot(bundles, platform, "AgentMix", [executable]) != manifest["nativeIdentity"]:
        raise GateError("Staged architecture or identities differ from manifest")
    if (directory / "mixagent-ui.png").read_bytes()[:8] != b"\x89PNG\r\n\x1a\n":
        raise GateError("Invalid editor screenshot")
    return manifest


def package(build, output, configuration, platform, source_commit, juce_commit=JUCE_COMMIT):
    if not re.fullmatch(r"[0-9a-fA-F]{40}", source_commit) or juce_commit != JUCE_COMMIT:
        raise GateError("Full source SHA and pinned JUCE revision are required")
    bundles = discover(build, None, [], configuration, ["MixAgent"])
    extension = ".exe" if platform == "windows-x64" else ""
    standalone = build / "MixAgent_artefacts" / configuration / "Standalone" / ("MixAgent" + extension)
    image = build / "mixagent-ui.png"
    native_header(standalone, platform, False)
    if not image.is_file() or image.is_symlink() or image.read_bytes()[:8] != b"\x89PNG\r\n\x1a\n":
        raise GateError("Missing or invalid editor screenshot")
    snapshot(bundles, platform, "AgentMix", [standalone])
    output = output.resolve(); build = build.resolve()
    if output.exists() or output.is_relative_to(build) or build.is_relative_to(output):
        raise GateError("Use a fresh output outside the build tree")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=output.name + ".stage-", dir=output.parent) as temporary:
        stage = Path(temporary)
        shutil.copytree(bundles["MixAgent"], stage / "vst3/MixAgent.vst3")
        (stage / "standalone").mkdir()
        shutil.copy2(standalone, stage / "standalone" / standalone.name)
        shutil.copy2(image, stage / "mixagent-ui.png")
        files = [{"path": p.relative_to(stage).as_posix(), "sizeBytes": p.stat().st_size, "sha256": digest(p)}
                 for p in all_files(stage)]
        manifest = {"schemaVersion": 1, "product": "MixAgent", "sourceCommit": source_commit.lower(),
                    "juceVersion": "8.0.9", "juceCommit": juce_commit, "platform": platform,
                    "configuration": configuration, "standalonePath": "standalone/" + standalone.name, "files": files,
                    "nativeIdentity": snapshot({"MixAgent": stage / "vst3/MixAgent.vst3"}, platform, "AgentMix",
                                               [stage / "standalone" / standalone.name])}
        (stage / "build-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        (stage / "checksums.sha256").write_text("".join(f"{i['sha256']}  {i['path']}\n" for i in files), encoding="utf-8")
        verify(stage)
        os.replace(stage, output)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--configuration", default="Release")
    parser.add_argument("--platform", choices=("windows-x64", "linux-x86_64"))
    parser.add_argument("--source-commit")
    parser.add_argument("--juce-commit", default=JUCE_COMMIT)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--output", type=Path)
    mode.add_argument("--verify", type=Path)
    args = parser.parse_args()
    try:
        if args.verify:
            verify(args.verify, args.source_commit)
            print("MixAgent package integrity PASS")
        else:
            if args.build_dir is None or args.platform is None or args.source_commit is None:
                raise GateError("--build-dir, --platform and --source-commit are required when packaging")
            package(args.build_dir, args.output, args.configuration, args.platform, args.source_commit, args.juce_commit)
            print(f"MixAgent complete release package PASS: {args.output}")
        return 0
    except (GateError, OSError, ValueError) as error:
        print(f"MixAgent packaging FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
