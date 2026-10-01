"""Build a reversible, local see-through scope patch for EDF6's SVU01 model.

The two solid lens surfaces are independent, 80-triangle islands in the shipped
MDB mesh. Replacing their triangle indices with degenerate triangles leaves the
scope housing, all other geometry, and every other RAB entry intact.
"""

import argparse
import hashlib
import struct
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent / "edf6"))
import cpk  # noqa: E402
import rab  # noqa: E402


NAME = "S_SNIPER_SVU01.RAB"
MODEL = "s_sniper_SVU01.mdb"
EXPECTED_SHA256 = "f421a8a00ae710920a92f7b888846924deed2301a758166a94fd2134df2b4a45"
LENS_SEEDS = (7988, 7652)  # ocular and objective glass; game EDF6 build 17055427


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


def patch_mdb(source):
    digest = hashlib.sha256(source).hexdigest()
    if digest != EXPECTED_SHA256:
        raise ValueError(f"Unexpected original MDB SHA256: {digest}")
    object_offset = struct.unpack_from("<I", source, 0x1C)[0]
    mesh_count, mesh_offset = struct.unpack_from("<II", source, object_offset + 8)
    if mesh_count != 1:
        raise ValueError(f"Expected one mesh, found {mesh_count}")
    mesh_start = object_offset + mesh_offset
    index_count, index_offset = struct.unpack_from("<II", source, mesh_start + 0x20)
    if index_count != 28503 or index_count % 3:
        raise ValueError(f"Unexpected index count: {index_count}")
    index_start = mesh_start + index_offset
    indices = struct.unpack_from(f"<{index_count}H", source, index_start)
    triangles = [indices[i:i + 3] for i in range(0, index_count, 3)]
    islands = [lens_island(triangles, seed) for seed in LENS_SEEDS]
    if any(len(island) != 80 for island in islands) or islands[0] & islands[1]:
        raise ValueError("Lens topology differs from the inspected model")
    result = bytearray(source)
    for island in islands:
        for number in island:
            first = triangles[number][0]
            struct.pack_into("<3H", result, index_start + number * 6,
                             first, first, first)
    changed = {i for island in islands for i in island}
    expected = bytearray(source)
    for number in changed:
        first = triangles[number][0]
        struct.pack_into("<3H", expected, index_start + number * 6,
                         first, first, first)
    if result != expected:
        raise AssertionError("Unexpected MDB change")
    return bytes(result), len(changed)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path, help="EDF6 installation directory")
    parser.add_argument("output", type=Path, help="output RAB outside the game")
    args = parser.parse_args()
    archive = cpk.Cpk(str(args.game / "Root.cpk"))
    original = archive.read("WEAPON", NAME)
    package = rab.parse(original)
    entry = package.find(MODEL)
    if entry is None or entry.directory != "MODEL":
        raise ValueError("Expected SVU01 MDB was not found")
    original_entries = [(e.directory, e.name, e.data) for e in package.entries]
    edited, count = patch_mdb(entry.data)
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
        if rebuilt.data != (edited if old_name == MODEL else old_data):
            raise AssertionError(f"RAB entry changed unexpectedly: {old_name}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(output)
    print(f"Removed {count} lens triangles from {MODEL}")
    print(f"Output: {args.output}")
    print(f"SHA256: {hashlib.sha256(output).hexdigest()}")


if __name__ == "__main__":
    main()
