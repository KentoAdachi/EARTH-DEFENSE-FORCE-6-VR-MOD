"""Build a reversible, local see-through scope patch for a known EDF6 model.

The two solid lens surfaces are independent, 80-triangle islands in the shipped
MDB mesh. Replacing their triangle indices with degenerate triangles leaves the
scope housing, all other geometry, and every other RAB entry intact.
The optional MMF01 profile is a geometry-only example for the KFF family;
its in-game appearance has not been validated, and the zoom compositor still
recognizes SVU01 only.
"""

import argparse
import hashlib
import struct
import sys
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent / "edf6"))
import cpk  # noqa: E402
import rab  # noqa: E402


@dataclass(frozen=True)
class LensProfile:
    archive: str
    model: str
    sha256: str
    index_count: int
    seeds: tuple = ()
    triangle_groups: tuple = ()


# Exact original model hashes and inspected triangle selections, build 17055427.
# SVU01 has independent curved lens islands. MMF01's flat disks have split
# vertex indices; a connected-component seed does not select the whole disk.
PROFILES = {
    "svu01": LensProfile(
        "S_SNIPER_SVU01.RAB", "s_sniper_SVU01.mdb",
        "f421a8a00ae710920a92f7b888846924deed2301a758166a94fd2134df2b4a45",
        28503, seeds=(7988, 7652)),
    "mmf01": LensProfile(
        "S_SNIPER_MMF01.RAB", "s_sniper_mmf01.mdb",
        "b593c2218b88b1b0bdbb1cf36cb50af02d1f2cf72fff509ccbcd0c7b6fbe44c5",
        8838, triangle_groups=(tuple(range(1258, 1272)),
                              tuple(range(2216, 2230)),
                              (*range(2548, 2560), 2816, 2817))),
}


def lens_island(triangles, seed):
    by_vertex = defaultdict(list)
    for number, triple in enumerate(triangles):
        for vertex in triple:
            by_vertex[vertex].append(number)
    found = {seed}
    queue = [seed]
    for number in queue:
        for vertex in triangles[number]:
            for other in by_vertex[vertex]:
                if other not in found:
                    found.add(other)
                    queue.append(other)
    return found


def patch_mdb(source, profile=PROFILES["svu01"]):
    digest = hashlib.sha256(source).hexdigest()
    if digest != profile.sha256:
        raise ValueError(f"Unexpected original MDB SHA256: {digest}")
    object_offset = struct.unpack_from("<I", source, 0x1C)[0]
    mesh_count, mesh_offset = struct.unpack_from("<II", source, object_offset + 8)
    if mesh_count != 1:
        raise ValueError(f"Expected one mesh, found {mesh_count}")
    mesh_start = object_offset + mesh_offset
    index_count, index_offset = struct.unpack_from("<II", source, mesh_start + 0x20)
    if index_count != profile.index_count or index_count % 3:
        raise ValueError(f"Unexpected index count: {index_count}")
    index_start = mesh_start + index_offset
    indices = struct.unpack_from(f"<{index_count}H", source, index_start)
    triangles = [indices[i:i + 3] for i in range(0, index_count, 3)]
    if profile.seeds:
        islands = [lens_island(triangles, seed) for seed in profile.seeds]
        if any(len(island) != 80 for island in islands) or islands[0] & islands[1]:
            raise ValueError("Lens topology differs from the inspected model")
    else:
        islands = [set(group) for group in profile.triangle_groups]
        if len(islands) != 3 or any(len(group) != 14 for group in islands):
            raise ValueError("Expected three 14-triangle MMF01 disks")
    changed = {i for island in islands for i in island}
    if len(changed) != sum(map(len, islands)) or any(i < 0 or i >= len(triangles) for i in changed):
        raise ValueError("Overlapping or invalid lens triangle selections")
    result = bytearray(source)
    for island in islands:
        for number in island:
            first = triangles[number][0]
            struct.pack_into("<3H", result, index_start + number * 6,
                             first, first, first)
    return bytes(result), len(changed)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path, help="EDF6 installation directory")
    parser.add_argument("output", type=Path, help="output RAB outside the game")
    parser.add_argument("--model", choices=PROFILES, default="svu01",
                        help="mmf01 is an untested KFF-family geometry example")
    args = parser.parse_args()
    profile = PROFILES[args.model]
    if args.output.resolve().is_relative_to(args.game.resolve()):
        parser.error("Generate the RAB outside the game directory, then install it manually")
    archive = cpk.Cpk(str(args.game / "Root.cpk"))
    original = archive.read("WEAPON", profile.archive)
    package = rab.parse(original)
    entry = package.find(profile.model)
    if entry is None or entry.directory != "MODEL":
        raise ValueError(f"Expected MDB was not found: {profile.model}")
    original_entries = [(e.directory, e.name, e.data) for e in package.entries]
    edited, count = patch_mdb(entry.data, profile)
    entry.replace(edited)
    output = package.to_bytes()
    check = rab.parse(output)
    if [(e.directory, e.name) for e in check.entries] != [
        (d, n) for d, n, _ in original_entries
    ]:
        raise AssertionError("RAB directory changed")
    for old_directory, old_name, old_data in original_entries:
        rebuilt = next(e for e in check.entries
                       if e.directory == old_directory and e.name == old_name)
        if rebuilt.data != (edited if old_name == profile.model else old_data):
            raise AssertionError(f"RAB entry changed unexpectedly: {old_name}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(output)
    print(f"Removed {count} lens triangles from {profile.model}")
    print(f"Output: {args.output}")
    print(f"SHA256: {hashlib.sha256(output).hexdigest()}")


if __name__ == "__main__":
    main()
