"""Generate region-scalars.ttf with FontTools.

The VARC store is encoded explicitly for the draft format currently supported
by FreeType. Pass --off2026 when migrating the fixture to the revised format.
"""

import argparse
import struct
from pathlib import Path

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib.tables.DefaultTable import DefaultTable
from fontTools.ttLib.tables.TupleVariation import TupleVariation
from fontTools.ttLib.tables import otTables as ot

RANGES = [(0.5, 0.25, 1), (0, 1, 0.5), (-1, 0.5, 1), (-1, 0, 1), (0, 0.5, 1)]


def index(items):
    offsets = [1]
    for item in items:
        offsets.append(offsets[-1] + len(item))
    return struct.pack(">IB", len(items), 1) + bytes(offsets) + b"".join(items)


def offset_array(header, items):
    offset = len(header) + 4 * len(items)
    offsets = []
    for item in items:
        offsets.append(offset)
        offset += len(item)
    return header + struct.pack(">" + "I" * len(items), *offsets) + b"".join(items)


def varc(font, off2026):
    regions = []
    for tent in RANGES:
        axis = struct.pack(">Hhhh", 1, *(round(v * 16384) for v in tent))
        header = struct.pack(">HI", 1, 6) if off2026 else struct.pack(">H", 1)
        regions.append(header + axis)
    region_list = offset_array(
        struct.pack(">I" if off2026 else ">H", len(regions)), regions
    )
    data = []
    for i in range(len(regions)):
        header = struct.pack(">BHH", 1, 1, i)
        if off2026:
            header += struct.pack(">I", 9)
        data.append(header + index([b"\x00\x40"]))  # one delta: +64
    store_header_size = 8 + 4 * len(data)
    store = struct.pack(">HIH", 1, store_header_size, len(data))
    data_offset = store_header_size + len(region_list)
    for item in data:
        store += struct.pack(">I", data_offset)
        data_offset += len(item)
    store += region_list + b"".join(data)
    coverage = struct.pack(">HH5H", 1, 5, *range(7, 12))
    components = []
    for i in range(5):
        component = ot.VarComponent()
        component.glyphName = "base"
        component.flags = int(ot.VarComponentFlags.HAVE_TRANSLATE_X)
        component.transform.translateX = 400
        component.transformVarIndex = i << 16
        components.append(component.compile(font))
    header = struct.pack(
        ">6I", 0x10000, 24, 24 + len(coverage), 0, 0, 24 + len(coverage) + len(store)
    )
    return header + coverage + store + index(components)


def build(off2026=False):
    order = (
        [".notdef", "base"]
        + [f"tuple{i}" for i in range(5)]
        + [f"sparse{i}" for i in range(5)]
    )
    fb = FontBuilder(1000)
    fb.setupGlyphOrder(order)
    fb.setupCharacterMap(dict(zip(range(65, 75), order[2:])))
    glyphs = {}
    for name in order:
        pen = TTGlyphPen(None)
        if name == "base" or name.startswith("tuple"):
            pen.moveTo((100, 0))
            pen.lineTo((200, 0))
            pen.lineTo((100, 100))
            pen.closePath()
        glyphs[name] = pen.glyph()
    fb.setupGlyf(glyphs)
    fb.setupHorizontalMetrics(
        {name: (500, 100 if glyphs[name].numberOfContours else 0) for name in order}
    )
    fb.setupHorizontalHeader(ascent=800, descent=-200)
    fb.setupNameTable({"familyName": "Region scalars", "styleName": "Regular"})
    fb.setupOS2()
    fb.setupPost()
    fb.setupFvar([("TEST", 0, 0, 1, "Test"), ("LEAF", -1, 0, 1, "Leaf")], [])
    fb.setupGvar(
        {
            f"tuple{i}": [
                TupleVariation(
                    {"TEST": (0, 1, 1), "LEAF": tent}, [(64, 0)] * 3 + [(0, 0)] * 4
                )
            ]
            for i, tent in enumerate(RANGES)
        }
    )
    table = DefaultTable("VARC")
    table.data = varc(fb.font, off2026)
    fb.font.recalcBBoxes = False
    fb.font["head"].xMin, fb.font["head"].xMax = 100, 200
    fb.font["head"].yMin, fb.font["head"].yMax = 0, 100
    fb.font["VARC"] = table
    fb.font["head"].created = fb.font["head"].modified = 3406620153
    fb.font.recalcTimestamp = False
    return fb.font


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--off2026", action="store_true")
    parser.add_argument(
        "output",
        nargs="?",
        type=Path,
        default=Path(__file__).parents[1] / "data/region-scalars.ttf",
    )
    args = parser.parse_args()
    build(args.off2026).save(args.output)
