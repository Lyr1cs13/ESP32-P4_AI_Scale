#!/usr/bin/env python3
import struct
import sys
from pathlib import Path


MODEL_NAMES = [
    "cooked_weight_g",
    "cooked_energy_kcal",
    "cooked_protein_g",
    "cooked_fat_g",
    "cooked_carbohydrate_g",
    "cooked_sodium_mg",
    "cooked_cholesterol_mg",
    "cooked_vitamin_c_mg",
    "cooked_calcium_mg",
    "cooked_iron_mg",
    "cooked_potassium_mg",
]


def align(blob: bytearray, boundary: int) -> None:
    while len(blob) % boundary:
        blob.append(0)


def parse_values(text: str, key: str, cast):
    prefix = key + "="
    if not text.startswith(prefix):
        return None
    body = text[len(prefix):].strip()
    if not body:
        return []
    return [cast(item) for item in body.split()]


def pack_tree(tree: dict) -> bytes:
    split_feature = tree["split_feature"]
    threshold = tree["threshold"]
    left_child = tree["left_child"]
    right_child = tree["right_child"]
    leaf_value = tree["leaf_value"]

    internal_count = len(split_feature)
    leaf_count = len(leaf_value)
    if not (internal_count == len(threshold) == len(left_child) == len(right_child)):
        raise ValueError("inconsistent internal node array lengths")
    if internal_count > 255 or leaf_count > 256:
        raise ValueError("tree exceeds compact binary format limits")

    out = bytearray()
    out += struct.pack("<HH", internal_count, leaf_count)
    out += bytes(split_feature)
    align(out, 2)
    out += struct.pack("<" + "h" * internal_count, *left_child)
    out += struct.pack("<" + "h" * internal_count, *right_child)
    align(out, 4)
    out += struct.pack("<" + "f" * internal_count, *threshold)
    out += struct.pack("<" + "f" * leaf_count, *leaf_value)
    return bytes(out)


def parse_model(path: Path):
    trees = []
    current = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line:
            continue

        parsed = parse_values(line, "split_feature", int)
        if parsed is not None:
            current["split_feature"] = parsed
            continue

        parsed = parse_values(line, "threshold", float)
        if parsed is not None:
            current["threshold"] = parsed
            continue

        parsed = parse_values(line, "left_child", int)
        if parsed is not None:
            current["left_child"] = parsed
            continue

        parsed = parse_values(line, "right_child", int)
        if parsed is not None:
            current["right_child"] = parsed
            continue

        parsed = parse_values(line, "leaf_value", float)
        if parsed is not None:
            current["leaf_value"] = parsed
            trees.append(pack_tree(current))
            current = {}

    if not trees:
        raise ValueError(f"no trees found in {path}")
    return trees


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: pack_nutricook_models.py <model-dir> <output-bin>", file=sys.stderr)
        return 2

    model_dir = Path(sys.argv[1])
    output = Path(sys.argv[2])
    model_blobs = []

    for name in MODEL_NAMES:
        path = model_dir / f"lgbm_{name}.txt"
        trees = parse_model(path)
        body = bytearray(struct.pack("<I", len(trees)))
        for tree in trees:
            align(body, 4)
            body += tree
        model_blobs.append((len(trees), bytes(body)))

    header_size = 8 + len(model_blobs) * 8
    cursor = header_size
    headers = []
    body = bytearray()
    for tree_count, blob in model_blobs:
        align(body, 4)
        cursor = header_size + len(body)
        headers.append((tree_count, cursor))
        body += blob

    packed = bytearray()
    packed += b"JFB1"
    packed += struct.pack("<I", len(model_blobs))
    for tree_count, offset in headers:
        packed += struct.pack("<II", tree_count, offset)
    packed += body

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(packed)
    print(f"packed {len(model_blobs)} models into {output} ({len(packed)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())


