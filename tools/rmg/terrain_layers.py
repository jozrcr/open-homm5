"""Ground layers of a map's terrain file: each layer's tile, its share of the map, and its weight near given tiles.

    python tools/rmg/terrain_layers.py C:/Games/H5_test/data/RMGTemp/CurrentMap/GroundTerrain.bin 14,29 76,77

Read only. The format is described in docs/subsystems/rmg.md ("The terrain file").
"""
import struct
import sys

data = open(sys.argv[1], "rb").read()
points = [tuple(int(v) for v in a.split(",")) for a in sys.argv[2:]]


def chunks(off, end):
    """The chunks between off and end: (id, body offset, body size)."""
    out = []
    while off < end:
        cid = data[off]
        if data[off + 1] & 1:
            size = struct.unpack_from("<I", data, off + 1)[0] >> 1
            body = off + 5
        else:
            size = data[off + 1] >> 1
            body = off + 2
        out.append((cid, body, size))
        off = body + size
    return out


def child(parent, cid):
    _, body, size = parent
    return [c for c in chunks(body, body + size) if c[0] == cid]


def grid(chunk):
    """A (width, height, bytes) grid chunk: 1 = width, 2 = height, 3 = the bytes."""
    w = struct.unpack_from("<i", data, child(chunk, 1)[0][1])[0]
    h = struct.unpack_from("<i", data, child(chunk, 2)[0][1])[0]
    _, body, size = child(chunk, 3)[0]
    return w, h, data[body:body + size]


root = [c for c in chunks(0, len(data)) if c[0] == 1][0]
info = child(root, 1)[0]
width = struct.unpack_from("<i", data, child(info, 2)[0][1])[0]
height = struct.unpack_from("<i", data, child(info, 3)[0][1])[0]
layers = child(child(info, 4)[0], 1)
print("%d x %d tiles, %d layers; columns: share of the map, then the mean weight (0-255) within 3 tiles of each point"
      % (width, height, len(layers)))
for i, layer in enumerate(layers):
    w, h, weights = grid(child(layer, 2)[0])
    name_chunk = child(layer, 3)[0]
    _, body, size = chunks(name_chunk[1], name_chunk[1] + name_chunk[2])[0]
    path = data[body:body + size].decode("latin-1").split("#")[0]
    share = sum(weights) / 255.0 / (w * h) * 100
    near = []
    for x, y in points:
        cells = [weights[yy * w + xx]
                 for yy in range(max(0, y - 3), min(h, y + 4))
                 for xx in range(max(0, x - 3), min(w, x + 4))]
        near.append("%3d" % (sum(cells) // len(cells)))
    print("%2d %5.1f%%  %s  %s" % (i, share, " ".join(near), path))
