#!/usr/bin/env python3
"""Validate complete native bundles, run a host, and prove it changed no files.

Header checks establish architecture and structural bounds. Only the supplied
host establishes loadability. Reports record the host's real exit status.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys


class GateError(ValueError):
    pass


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def native_header(path: Path, platform: str, plugin: bool = True) -> dict:
    if path.is_symlink() or not path.is_file():
        raise GateError(f"Native binary is not a regular file: {path}")
    size = path.stat().st_size
    with path.open("rb") as stream:
        head = stream.read(64)
        if platform == "linux-x86_64":
            if len(head) != 64 or head[:7] != b"\x7fELF\x02\x01\x01":
                raise GateError(f"Invalid/nonempty ELF64 little-endian module: {path}")
            kind, machine, version = struct.unpack_from("<HHI", head, 16)
            phoff = struct.unpack_from("<Q", head, 32)[0]
            ehsize, phsize, count = struct.unpack_from("<HHH", head, 52)
            if machine != 62 or version != 1 or kind not in ((3,) if plugin else (2, 3)):
                raise GateError(f"Expected x86_64 {'shared module' if plugin else 'executable'}: {path}")
            if ehsize != 64 or phsize != 56 or count == 0 or phoff < 64 or phoff + count * phsize > size:
                raise GateError(f"Invalid ELF program table bounds: {path}")
            executable = False
            stream.seek(phoff)
            for _ in range(count):
                segment = stream.read(phsize)
                segkind, flags, offset, _, _, filesz, memsz, _ = struct.unpack("<IIQQQQQQ", segment)
                if offset + filesz > size or (segkind == 1 and memsz < filesz):
                    raise GateError(f"Invalid ELF segment bounds: {path}")
                executable |= segkind == 1 and bool(flags & 1) and filesz > 0
            if not executable:
                raise GateError(f"ELF has no executable load segment: {path}")
            return {"format": "ELF64", "architecture": "x86_64", "kind": kind}
        if platform != "windows-x64":
            raise GateError(f"Unsupported platform: {platform}")
        if len(head) != 64 or head[:2] != b"MZ":
            raise GateError(f"Invalid/nonempty Windows PE module: {path}")
        offset = struct.unpack_from("<I", head, 60)[0]
        if offset < 64 or offset + 24 > size:
            raise GateError(f"Invalid PE header bounds: {path}")
        stream.seek(offset)
        coff = stream.read(24)
        machine, sections = struct.unpack_from("<HH", coff, 4)
        optional_size, flags = struct.unpack_from("<HH", coff, 20)
        if coff[:4] != b"PE\0\0" or machine != 0x8664 or sections == 0:
            raise GateError(f"Expected Windows AMD64 module: {path}")
        if plugin and not flags & 0x2000:
            raise GateError(f"VST3 PE is not a DLL: {path}")
        if optional_size < 112 or offset + 24 + optional_size + sections * 40 > size:
            raise GateError(f"Invalid PE optional/section table bounds: {path}")
        optional = stream.read(optional_size)
        if struct.unpack_from("<H", optional)[0] != 0x20B:
            raise GateError(f"Expected PE32+ optional header: {path}")
        executable = False
        for _ in range(sections):
            section = stream.read(40)
            rawsize, rawoffset = struct.unpack_from("<II", section, 16)
            attributes = struct.unpack_from("<I", section, 36)[0]
            if rawoffset + rawsize > size:
                raise GateError(f"Invalid PE section file bounds: {path}")
            executable |= bool(attributes & 0x20000000) and rawsize > 0
        if not executable:
            raise GateError(f"PE has no executable section: {path}")
        return {"format": "PE32+", "architecture": "x86_64", "dll": bool(flags & 0x2000)}


def discover(build_dir: Path | None, bundle_dir: Path | None, bundles: list[Path],
             configuration: str, products: list[str]) -> dict[str, Path]:
    if len(products) != len(set(products)) or not products:
        raise GateError("Expected product names must be nonempty and unique")
    if not re.fullmatch(r"[A-Za-z0-9_-]+", configuration):
        raise GateError("Configuration must be a simple name")
    found = list(bundles)
    if bundle_dir is not None:
        if not bundle_dir.is_dir():
            raise GateError(f"Missing bundle directory: {bundle_dir}")
        found += list(bundle_dir.glob("*.vst3"))
    if build_dir is not None:
        if not build_dir.is_dir():
            raise GateError(f"Missing build directory: {build_dir}")
        for artefacts in build_dir.glob("*_artefacts"):
            for vst in (artefacts / configuration / "VST3", artefacts / "VST3"):
                if artefacts.is_symlink() or vst.is_symlink() or vst.parent.is_symlink():
                    raise GateError(f"Symlinked build artefacts: {vst}")
                found += list(vst.glob("*.vst3"))
    result = {}
    for bundle in found:
        product = bundle.name.removesuffix(".vst3")
        if bundle.suffix != ".vst3" or product not in products or product in result:
            raise GateError(f"Unexpected or duplicate product: {bundle}")
        result[product] = bundle
    if set(result) != set(products):
        raise GateError(f"Missing bundles: {sorted(set(products) - set(result))}; found {len(result)}")
    return result


def snapshot(bundles: dict[str, Path], platform: str, vendor: str,
             additional_binaries: list[Path] = ()) -> dict:
    arch, suffix = ("x86_64-win", ".vst3") if platform == "windows-x64" else ("x86_64-linux", ".so")
    records = []
    all_cids = set()
    for product, bundle in sorted(bundles.items()):
        if bundle.is_symlink() or not bundle.is_dir():
            raise GateError(f"Bundle is not a real directory: {bundle}")
        files = []
        for path in sorted(bundle.rglob("*")):
            if path.is_symlink() or not (path.is_file() or path.is_dir()):
                raise GateError(f"Unsafe bundle resource: {path}")
            if path.is_file():
                files.append({"path": path.relative_to(bundle).as_posix(),
                              "sizeBytes": path.stat().st_size, "sha256": digest(path)})
        binary = bundle / "Contents" / arch / (product + suffix)
        header = native_header(binary, platform)
        metadata = bundle / "Contents" / "Resources" / "moduleinfo.json"
        # JUCE's VST3 SDK writer currently emits trailing commas.
        info = json.loads(re.sub(r",(\s*[}\]])", r"\1", metadata.read_text(encoding="utf-8")))
        classes = info.get("Classes", [])
        if info.get("Name") != product or info.get("Factory Info", {}).get("Vendor") != vendor:
            raise GateError(f"Unexpected product/factory identity: {metadata}")
        identities = []
        for entry in classes:
            cid = entry.get("CID", "")
            if not re.fullmatch(r"[0-9A-Fa-f]{32}", cid) or entry.get("Vendor") != vendor:
                raise GateError(f"Invalid class/vendor identity: {metadata}")
            identities.append({"cid": cid.upper(), "name": entry.get("Name"), "category": entry.get("Category")})
            if cid.upper() in all_cids:
                raise GateError(f"Duplicate class identity across products: {metadata}")
            all_cids.add(cid.upper())
        if len(identities) != 2 or len({i["cid"] for i in identities}) != 2 or \
                {i["category"] for i in identities} != {"Audio Module Class", "Component Controller Class"}:
            raise GateError(f"Expected one audio class and one controller class: {metadata}")
        records.append({"product": product, "header": header,
                        "classes": sorted(identities, key=lambda i: i["cid"]), "files": files})
    extras = []
    for path in additional_binaries:
        extras.append({"name": path.name, "header": native_header(path, platform, False),
                       "sizeBytes": path.stat().st_size, "sha256": digest(path)})
    return {"schemaVersion": 1, "platform": platform, "vendor": vendor, "products": records,
            "additionalBinaries": extras}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    location = parser.add_mutually_exclusive_group(required=True)
    location.add_argument("--build-dir", type=Path)
    location.add_argument("--bundle-dir", type=Path)
    location.add_argument("--bundle", type=Path, action="append", default=[])
    parser.add_argument("--configuration", default="Release")
    parser.add_argument("--platform", choices=("windows-x64", "linux-x86_64"), required=True)
    parser.add_argument("--vendor", required=True)
    product_args = parser.add_mutually_exclusive_group(required=True)
    product_args.add_argument("--product", action="append", default=[])
    product_args.add_argument("--products", help="Comma-separated exact product names")
    parser.add_argument("--additional-binary", type=Path, action="append", default=[])
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--snapshot", type=Path)
    mode.add_argument("--check-snapshot", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--repeat", type=int, default=2)
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    report = {"schemaVersion": 1, "passed": False, "runs": []}
    try:
        if args.repeat < 1 or args.timeout <= 0:
            raise GateError("Repeat count and timeout must be positive")
        command = args.command[1:] if args.command[:1] == ["--"] else args.command
        if (args.snapshot or args.check_snapshot) and command:
            raise GateError("Snapshot modes do not run commands")
        products = args.products.split(",") if args.products is not None else args.product
        bundles = discover(args.build_dir, args.bundle_dir, args.bundle, args.configuration, products)
        before = snapshot(bundles, args.platform, args.vendor, args.additional_binary)
        report["before"] = before
        if args.snapshot:
            args.snapshot.parent.mkdir(parents=True, exist_ok=True)
            args.snapshot.write_text(json.dumps(before, indent=2) + "\n", encoding="utf-8")
        elif args.check_snapshot:
            if json.loads(args.check_snapshot.read_text(encoding="utf-8")) != before:
                raise GateError("Binary/resource hashes, architecture or class identities changed since snapshot")
        elif command:
            report["command"] = command
            for run in range(args.repeat):
                result = subprocess.run(command, timeout=args.timeout, check=False)
                run_report = {"run": run + 1, "hostExit": result.returncode, "filesAndIdentitiesUnchanged": False}
                report["runs"].append(run_report)
                after_bundles = discover(args.build_dir, args.bundle_dir, args.bundle, args.configuration, products)
                after = snapshot(after_bundles, args.platform, args.vendor, args.additional_binary)
                stable = after == before
                run_report["filesAndIdentitiesUnchanged"] = stable
                if result.returncode != 0:
                    raise GateError(f"Host exited {result.returncode} on run {run + 1}")
                if not stable:
                    raise GateError(f"Host changed binary/resources or class identities on run {run + 1}")
        report["passed"] = True
        print(f"VST3 gate PASS: {len(bundles)} bundles, {len(report['runs'])} actual host runs")
        return 0
    except (GateError, OSError, ValueError, subprocess.TimeoutExpired) as error:
        report["error"] = str(error)
        print(f"VST3 gate FAIL: {error}", file=sys.stderr)
        return 1
    finally:
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    raise SystemExit(main())
