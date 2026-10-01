"""Read-only installed-data checks; generated RABs live in a temporary directory.

Usage: python tests/lens_patch_tests.py "<EDF6 game directory>"
Requires the original Root.cpk from the supported build 17055427.
No game asset is included in the test or written to the installation.
"""
import argparse
import hashlib
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path[:0] = [str(TOOLS), str(TOOLS / "edf6")]
import cpk  # noqa: E402
import rab  # noqa: E402
import patch_svu01_scope as patch  # noqa: E402

EXPECTED = {
    "svu01": (160, "a4c4fc9177de0f5bf9bd43f24bbfd0777db785cb49f07e5ce8d78c1608d5cd29"),
    "mmf01": (42, "97b995a46d6581d69b81c436f049e4feb22d952b2dc782a12a41144583d094a3"),
}


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def verify(game, scratch, key):
    profile = patch.PROFILES[key]
    output = scratch / profile.archive
    command = [sys.executable, str(TOOLS / "patch_svu01_scope.py"), str(game), str(output)]
    # Exercise the existing command's default profile as well as the new option.
    if key != "svu01":
        command += ["--model", key]
    subprocess.run(command, check=True)
    expected_count, expected_hash = EXPECTED[key]
    require(hashlib.sha256(output.read_bytes()).hexdigest() == expected_hash,
            f"{key}: generated RAB differs from the inspected result")

    original = rab.parse(cpk.Cpk(str(game / "Root.cpk")).read("WEAPON", profile.archive))
    rebuilt = rab.parse(output.read_bytes())
    require([(e.directory, e.name) for e in original.entries] ==
            [(e.directory, e.name) for e in rebuilt.entries], "RAB directory changed")
    source = original.find(profile.model).data
    edited = rebuilt.find(profile.model).data
    object_at = struct.unpack_from("<I", source, 0x1C)[0]
    mesh_at = object_at + struct.unpack_from("<I", source, object_at + 12)[0]
    count, relative = struct.unpack_from("<II", source, mesh_at + 0x20)
    index_at = mesh_at + relative
    require(source[:index_at] == edited[:index_at] and
            source[index_at + count * 2:] == edited[index_at + count * 2:],
            "MDB bytes outside the index buffer changed")
    changed_count = 0
    for number in range(count // 3):
        offset = index_at + number * 6
        before = struct.unpack_from("<3H", source, offset)
        after = struct.unpack_from("<3H", edited, offset)
        if before != after:
            changed_count += 1
            require(after == (before[0],) * 3, "Triangle was not made degenerate")
    require(changed_count == expected_count, f"{key}: unexpected triangle count")
    for before, after in zip(original.entries, rebuilt.entries):
        if before.name != profile.model:
            require(before.data == after.data, f"Other RAB entry changed: {before.name}")
    for invalid in (edited, source[:-1] + bytes([source[-1] ^ 1])):
        try:
            patch.patch_mdb(invalid, profile)
        except ValueError:
            pass
        else:
            raise AssertionError("Hash guard accepted a changed MDB")
    print(f"{key}: expected RAB hash, {changed_count} triangles, unchanged other data, hash rejection: PASS")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="EDF6VR-lens-test-") as temporary:
        for key in EXPECTED:
            verify(args.game.resolve(), Path(temporary), key)


if __name__ == "__main__":
    main()
