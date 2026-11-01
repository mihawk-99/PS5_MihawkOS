"""PS5 Vulkan Template - the assets made here: meshes, textures, conversions.

The asset pack's files whose licence is unclear are replaced by ones made by
these functions, from geometry and noise written here or from CC0 sources
(Poly Haven, Khronos's glTF sample models) downloaded at pinned hashes.
build-assets.py calls build(name, out_dir, download) for each "generated"
entry of ps5/assets.json. Everything is deterministic: the same inputs give
the same bytes, so the deploy tool sees unchanged files as unchanged.

The sizes follow what each sample expects of the file it replaces (its
camera, its instance scales, its texture format); ps5/ASSETS.md lists them.

Copyright (C) 2026 Mihawk
SPDX-License-Identifier: MIT
"""

import json
import math
import shutil
import struct
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

# ------------------------------------------------------------------ KTX 1

KTX_IDENTIFIER = bytes([0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A])
KTX_FORMATS = {
    # name: glType, glTypeSize, glFormat, glInternalFormat, glBaseInternalFormat, bytes per pixel
    "rgba8": (0x1401, 1, 0x1908, 0x8058, 0x1908, 4),
    "r8": (0x1401, 1, 0x1903, 0x8229, 0x1903, 1),
    "rgba16f": (0x140B, 2, 0x1908, 0x881A, 0x1908, 8),
}


def write_ktx(path, fmt, levels, faces=1, layers=0):
    """levels[l] is a list (array layers x faces, layer-major) of HxWxC arrays,
    uint8 for rgba8/r8, float16 for rgba16f. A KTX 1 file as libktx reads it:
    rows padded to 4 bytes, a cube face's image size given per face."""
    gl_type, type_size, gl_format, internal, base, bpp = KTX_FORMATS[fmt]
    height, width = levels[0][0].shape[:2]
    out = bytearray(KTX_IDENTIFIER)
    out += struct.pack("<13I", 0x04030201, gl_type, type_size, gl_format, internal, base,
                       width, height, 0, layers, faces, len(levels), 0)
    for images in levels:
        blobs = []
        for image in images:
            h, w = image.shape[:2]
            row = w * bpp
            padded = (row + 3) & ~3
            data = np.ascontiguousarray(image).tobytes()
            if padded != row:
                data = b"".join(data[y * row:(y + 1) * row] + b"\0" * (padded - row) for y in range(h))
            blobs.append(data)
        size = len(blobs[0]) if (faces == 6 and layers == 0) else sum(len(b) for b in blobs)
        out += struct.pack("<I", size)
        for blob in blobs:
            out += blob
            out += b"\0" * ((4 - len(blob) % 4) % 4)
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_bytes(bytes(out))


def mip_chain(image, levels=None):
    """Box-filtered levels down to 1x1 (float math, rounded back to the input type)."""
    chain = [image]
    count = levels or (int(math.floor(math.log2(max(image.shape[:2])))) + 1)
    current = image.astype(np.float32)
    for _ in range(count - 1):
        h, w = current.shape[:2]
        nh, nw = max(1, h // 2), max(1, w // 2)
        if h > 1 and w > 1:
            current = 0.25 * (current[0:nh * 2:2, 0:nw * 2:2] + current[1:nh * 2:2, 0:nw * 2:2]
                              + current[0:nh * 2:2, 1:nw * 2:2] + current[1:nh * 2:2, 1:nw * 2:2])
        elif h > 1:
            current = 0.5 * (current[0:nh * 2:2] + current[1:nh * 2:2])
        else:
            current = 0.5 * (current[:, 0:nw * 2:2] + current[:, 1:nw * 2:2])
        if image.dtype == np.uint8:
            chain.append(np.clip(np.rint(current), 0, 255).astype(np.uint8))
        else:
            chain.append(current.astype(image.dtype))
    return chain


def rgba(image, size=None):
    image = image.convert("RGBA")
    if size:
        image = image.resize((size, size), Image.LANCZOS)
    return np.asarray(image, dtype=np.uint8)


def ktx_2d(path, array, fmt="rgba8", mips=True):
    chain = mip_chain(array) if mips else [array]
    write_ktx(path, fmt, [[level] for level in chain])


def ktx_array(path, arrays):
    chains = [mip_chain(a) for a in arrays]
    write_ktx(path, "rgba8", [[c[l] for c in chains] for l in range(len(chains[0]))], layers=len(arrays))


def ktx_cube(path, faces, fmt):
    chains = [mip_chain(f) for f in faces]
    write_ktx(path, fmt, [[c[l] for c in chains] for l in range(len(chains[0]))], faces=6)


# ------------------------------------------------------------------ glTF

def write_gltf(path, meshes):
    """meshes: dicts with pos, nrm, (uv), (tan), idx, color (RGBA material
    factor), name. One node and one material a mesh; a .gltf and a .bin."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    blob = bytearray()
    views, accessors, gl_meshes, nodes, materials = [], [], [], [], []

    def add(array, kind, target, minmax=False):
        array = np.ascontiguousarray(array)
        offset = len(blob)
        blob.extend(array.tobytes())
        blob.extend(b"\0" * ((4 - len(blob) % 4) % 4))
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": array.nbytes, "target": target})
        accessor = {"bufferView": len(views) - 1,
                    "componentType": 5125 if array.dtype == np.uint32 else 5126,
                    "count": int(array.shape[0]), "type": kind}
        if minmax:
            accessor["min"] = [float(v) for v in array.min(axis=0)]
            accessor["max"] = [float(v) for v in array.max(axis=0)]
        accessors.append(accessor)
        return len(accessors) - 1

    for i, mesh in enumerate(meshes):
        attributes = {"POSITION": add(mesh["pos"].astype(np.float32), "VEC3", 34962, True),
                      "NORMAL": add(mesh["nrm"].astype(np.float32), "VEC3", 34962)}
        if mesh.get("uv") is not None:
            attributes["TEXCOORD_0"] = add(mesh["uv"].astype(np.float32), "VEC2", 34962)
        if mesh.get("tan") is not None:
            attributes["TANGENT"] = add(mesh["tan"].astype(np.float32), "VEC4", 34962)
        indices = add(mesh["idx"].astype(np.uint32), "SCALAR", 34963)
        materials.append({"name": mesh.get("name", f"material{i}"),
                          "pbrMetallicRoughness": {"baseColorFactor": [float(c) for c in mesh.get("color", (1, 1, 1, 1))],
                                                   "metallicFactor": 0.0, "roughnessFactor": 0.8}})
        gl_meshes.append({"name": mesh.get("name", f"mesh{i}"),
                          "primitives": [{"attributes": attributes, "indices": indices, "material": i}]})
        nodes.append({"name": mesh.get("name", f"node{i}"), "mesh": i})
    bin_name = path.with_suffix(".bin").name
    gltf = {"asset": {"version": "2.0", "generator": "PS5 Vulkan Template, ps5/tools/generate_assets.py"},
            "scene": 0, "scenes": [{"nodes": list(range(len(nodes)))}], "nodes": nodes,
            "meshes": gl_meshes, "materials": materials, "accessors": accessors, "bufferViews": views,
            "buffers": [{"uri": bin_name, "byteLength": len(blob)}]}
    path.with_suffix(".bin").write_bytes(bytes(blob))
    path.write_text(json.dumps(gltf, indent=1) + "\n")
    return path


def mesh(pos, nrm, idx, uv=None, color=(1, 1, 1, 1), name="mesh", tan=None):
    return {"pos": np.asarray(pos, np.float32), "nrm": np.asarray(nrm, np.float32), "idx": np.asarray(idx, np.uint32),
            "uv": None if uv is None else np.asarray(uv, np.float32), "color": color, "name": name,
            "tan": None if tan is None else np.asarray(tan, np.float32)}


def transformed(m, scale=1.0, translate=(0, 0, 0), rotate_y=0.0):
    c, s = math.cos(rotate_y), math.sin(rotate_y)
    rot = np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]], np.float32)
    scale = np.broadcast_to(np.asarray(scale, np.float32), (3,))
    out = dict(m)
    out["pos"] = (m["pos"] * scale) @ rot.T + np.asarray(translate, np.float32)
    n = (m["nrm"] / scale) @ rot.T
    out["nrm"] = n / np.linalg.norm(n, axis=1, keepdims=True)
    return out


def merged(meshes, name, color):
    pos, nrm, uv, idx, base = [], [], [], [], 0
    for m in meshes:
        pos.append(m["pos"]); nrm.append(m["nrm"])
        uv.append(m["uv"] if m["uv"] is not None else np.zeros((len(m["pos"]), 2), np.float32))
        idx.append(m["idx"] + base); base += len(m["pos"])
    return mesh(np.concatenate(pos), np.concatenate(nrm), np.concatenate(idx), np.concatenate(uv), color, name)


# ------------------------------------------------------------------ shapes (glTF's Y up)

def grid_indices(rows, cols, flip=False):
    idx = []
    for r in range(rows):
        for c in range(cols):
            a, b = r * (cols + 1) + c, r * (cols + 1) + c + 1
            d, e = (r + 1) * (cols + 1) + c, (r + 1) * (cols + 1) + c + 1
            idx += [a, d, b, b, d, e] if not flip else [a, b, d, b, e, d]
    return np.array(idx, np.uint32)


def uv_sphere(radius=1.0, rings=32, segments=64, inward=False):
    pos, nrm, uv = [], [], []
    for r in range(rings + 1):
        v = r / rings
        phi = v * math.pi
        for s in range(segments + 1):
            u = s / segments
            theta = u * 2 * math.pi
            n = (math.sin(phi) * math.cos(theta), math.cos(phi), math.sin(phi) * math.sin(theta))
            pos.append([radius * x for x in n]); nrm.append([-x for x in n] if inward else n); uv.append((u, v))
    return mesh(pos, nrm, grid_indices(rings, segments, flip=not inward), uv)


def box(size=(1, 1, 1), uv_scale=1.0):
    sx, sy, sz = (s / 2 for s in size)
    faces = [((1, 0, 0), (0, 0, -1), (0, 1, 0)), ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
             ((0, 1, 0), (1, 0, 0), (0, 0, -1)), ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
             ((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (-1, 0, 0), (0, 1, 0))]
    pos, nrm, uv, idx = [], [], [], []
    half = np.array([sx, sy, sz])
    for n, u, v in faces:
        n, u, v = np.array(n), np.array(u), np.array(v)
        base = len(pos)
        for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            pos.append((n + a * u + b * v) * half); nrm.append(n); uv.append(((a + 1) / 2 * uv_scale, (1 - b) / 2 * uv_scale))
        idx += [base, base + 1, base + 2, base, base + 2, base + 3]
    return mesh(pos, nrm, idx, uv)


def cylinder(radius=1.0, height=1.0, segments=48, caps=True, inward=False, axis="y"):
    pos, nrm, uv = [], [], []
    for r in range(2):
        y = (r - 0.5) * height
        for s in range(segments + 1):
            t = s / segments * 2 * math.pi
            n = (math.cos(t), 0.0, math.sin(t))
            pos.append((radius * n[0], y, radius * n[2])); nrm.append(tuple(-x for x in n) if inward else n)
            uv.append((s / segments, r))
    idx = list(grid_indices(1, segments, flip=inward))
    if caps:
        for top in (0, 1):
            y = (top - 0.5) * height
            center = len(pos)
            pos.append((0, y, 0)); nrm.append((0, 1 if top else -1, 0)); uv.append((0.5, 0.5))
            for s in range(segments + 1):
                t = s / segments * 2 * math.pi
                pos.append((radius * math.cos(t), y, radius * math.sin(t))); nrm.append((0, 1 if top else -1, 0))
                uv.append((0.5 + 0.5 * math.cos(t), 0.5 + 0.5 * math.sin(t)))
            for s in range(segments):
                a, b = center + 1 + s, center + 2 + s
                idx += [center, b, a] if top else [center, a, b]
    m = mesh(pos, nrm, idx, uv)
    if axis == "x":
        m["pos"] = m["pos"][:, [1, 0, 2]] * np.array([1, 1, 1], np.float32)
        m["nrm"] = m["nrm"][:, [1, 0, 2]]
        m["idx"] = m["idx"].reshape(-1, 3)[:, [0, 2, 1]].reshape(-1)
    return m


def cone(radius=1.0, height=1.0, segments=48):
    pos, nrm, uv, idx = [], [], [], []
    slope = radius / height
    for s in range(segments + 1):
        t = s / segments * 2 * math.pi
        n = np.array([math.cos(t), slope, math.sin(t)]); n /= np.linalg.norm(n)
        pos += [(radius * math.cos(t), -height / 2, radius * math.sin(t)), (0, height / 2, 0)]
        nrm += [n, n]; uv += [(s / segments, 1), (s / segments, 0)]
    for s in range(segments):
        a = s * 2
        idx += [a, a + 1, a + 2]
    base = len(pos)
    pos.append((0, -height / 2, 0)); nrm.append((0, -1, 0)); uv.append((0.5, 0.5))
    for s in range(segments + 1):
        t = s / segments * 2 * math.pi
        pos.append((radius * math.cos(t), -height / 2, radius * math.sin(t))); nrm.append((0, -1, 0)); uv.append((0, 0))
    for s in range(segments):
        idx += [base, base + 1 + s, base + 2 + s]
    return mesh(pos, nrm, idx, uv)


def torus_knot(p=2, q=3, radius=1.0, tube=0.28, segments=256, sides=24):
    def curve(t):
        r = radius * (2 + math.cos(q * t)) / 3
        return np.array([r * math.cos(p * t), radius * math.sin(q * t) / 3 * 1.0, r * math.sin(p * t)])
    pos, nrm, uv = [], [], []
    for i in range(segments + 1):
        t = i / segments * 2 * math.pi
        c, d = curve(t), curve(t + 1e-3)
        tangent = d - c; tangent /= np.linalg.norm(tangent)
        normal = np.cross(tangent, (0, 1, 0)); normal /= np.linalg.norm(normal)
        binormal = np.cross(tangent, normal)
        for j in range(sides + 1):
            a = j / sides * 2 * math.pi
            n = math.cos(a) * normal + math.sin(a) * binormal
            pos.append(c + tube * n); nrm.append(n); uv.append((i / segments * 8, j / sides))
    return mesh(pos, nrm, grid_indices(segments, sides, flip=True), uv)


def plane(size=1.0, uv_repeat=1.0, y=0.0, divisions=1):
    pos, nrm, uv, tan = [], [], [], []
    for r in range(divisions + 1):
        for c in range(divisions + 1):
            u, v = c / divisions, r / divisions
            pos.append(((u - 0.5) * size, y, (v - 0.5) * size)); nrm.append((0, 1, 0))
            uv.append((u * uv_repeat, v * uv_repeat)); tan.append((1, 0, 0, 1))
    return mesh(pos, nrm, grid_indices(divisions, divisions), uv, tan=tan)


def disc(radius=1.0, segments=96, uv_scale=1.0):
    pos, nrm, uv = [(0, 0, 0)], [(0, 1, 0)], [(0.5 * uv_scale, 0.5 * uv_scale)]
    for s in range(segments + 1):
        t = s / segments * 2 * math.pi
        pos.append((radius * math.cos(t), 0, radius * math.sin(t))); nrm.append((0, 1, 0))
        uv.append(((0.5 + 0.5 * math.cos(t)) * uv_scale, (0.5 + 0.5 * math.sin(t)) * uv_scale))
    idx = []
    for s in range(segments):
        idx += [0, s + 2, s + 1]
    return mesh(pos, nrm, idx, uv)


def icosphere(subdivisions=3):
    t = (1 + 5 ** 0.5) / 2
    verts = [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t), (0, -1, -t), (0, 1, -t),
             (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]
    verts = [np.array(v) / np.linalg.norm(v) for v in verts]
    faces = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6),
             (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10),
             (8, 6, 7), (9, 8, 1)]
    for _ in range(subdivisions):
        cache, new = {}, []
        def mid(a, b):
            key = (min(a, b), max(a, b))
            if key not in cache:
                m = verts[a] + verts[b]; verts.append(m / np.linalg.norm(m)); cache[key] = len(verts) - 1
            return cache[key]
        for a, b, c in faces:
            ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
            new += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
        faces = new
    return np.array(verts, np.float32), np.array(faces, np.uint32)


def smooth_normals(pos, faces):
    normals = np.zeros_like(pos)
    tri = pos[faces]
    fn = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    for k in range(3):
        np.add.at(normals, faces[:, k], fn)
    return normals / np.maximum(np.linalg.norm(normals, axis=1, keepdims=True), 1e-9)


def value_noise_3d(points, seed, frequency):
    """Smooth pseudo-random noise at points (N,3): trilinear value noise."""
    rng = np.random.default_rng(seed)
    lattice = rng.random((32, 32, 32)).astype(np.float32)
    p = points * frequency
    i = np.floor(p).astype(int); f = p - i
    f = f * f * (3 - 2 * f)
    result = np.zeros(len(points), np.float32)
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                w = (f[:, 0] if dx else 1 - f[:, 0]) * (f[:, 1] if dy else 1 - f[:, 1]) * (f[:, 2] if dz else 1 - f[:, 2])
                result += w * lattice[(i[:, 0] + dx) % 32, (i[:, 1] + dy) % 32, (i[:, 2] + dz) % 32]
    return result


def noise_2d(width, height, seed, octaves=5, scale=4.0, tile=True):
    """Tileable fractal value noise in [0, 1], HxW."""
    rng = np.random.default_rng(seed)
    out = np.zeros((height, width), np.float32)
    amplitude, total = 1.0, 0.0
    for o in range(octaves):
        cells = int(scale * 2 ** o)
        grid = rng.random((cells, cells)).astype(np.float32)
        ys = np.arange(height) / height * cells
        xs = np.arange(width) / width * cells
        y0 = np.floor(ys).astype(int); x0 = np.floor(xs).astype(int)
        fy = (ys - y0)[:, None]; fx = (xs - x0)[None, :]
        fy = fy * fy * (3 - 2 * fy); fx = fx * fx * (3 - 2 * fx)
        y1, x1 = (y0 + 1) % cells, (x0 + 1) % cells
        y0, x0 = y0 % cells, x0 % cells
        v = (grid[y0][:, x0] * (1 - fx) + grid[y0][:, x1] * fx) * (1 - fy) + \
            (grid[y1][:, x0] * (1 - fx) + grid[y1][:, x1] * fx) * fy
        out += amplitude * v; total += amplitude; amplitude *= 0.5
    return out / total


# ------------------------------------------------------------------ HDR images

def read_hdr(path):
    """A Radiance RGBE (.hdr) file as an HxWx3 float32 array."""
    data = Path(path).read_bytes()
    header_end = data.index(b"\n\n") + 2
    line_end = data.index(b"\n", header_end)
    _, height, _, width = data[header_end:line_end].split()
    height, width = int(height), int(width)
    pos = line_end + 1
    pixels = np.zeros((height, width, 4), np.uint8)
    for y in range(height):
        if data[pos] == 2 and data[pos + 1] == 2 and (data[pos + 2] << 8 | data[pos + 3]) == width:
            pos += 4
            for c in range(4):
                x = 0
                while x < width:
                    count = data[pos]; pos += 1
                    if count > 128:
                        count -= 128
                        pixels[y, x:x + count, c] = data[pos]; pos += 1
                    else:
                        pixels[y, x:x + count, c] = np.frombuffer(data[pos:pos + count], np.uint8); pos += count
                    x += count
        else:
            pixels[y] = np.frombuffer(data[pos:pos + width * 4], np.uint8).reshape(width, 4); pos += width * 4
    exponent = pixels[..., 3].astype(np.int32)
    scale = np.where(exponent > 0, np.ldexp(1.0, exponent - 136), 0.0).astype(np.float32)
    return pixels[..., :3].astype(np.float32) * scale[..., None]


def cube_faces_from_equirect(equirect, size):
    """The six faces (+X, -X, +Y, -Y, +Z, -Z, Vulkan's order) of a cube map
    sampled bilinearly from an equirectangular image. The samples' skyboxes
    look the cube map up with positions whose Y the base class flipped (glTF's
    Y is up, the samples' is down): the sky goes to -Y."""
    h, w = equirect.shape[:2]
    a = (np.arange(size) + 0.5) / size * 2 - 1
    u, v = np.meshgrid(a, a)
    ones = np.ones_like(u)
    directions = [(ones, -v, -u), (-ones, -v, u), (u, ones, v), (u, -ones, -v), (u, -v, ones), (-u, -v, -ones)]
    faces = []
    for x, y, z in directions:
        norm = np.sqrt(x * x + y * y + z * z)
        x, y, z = x / norm, -y / norm, z / norm
        lon = np.arctan2(x, -z)
        lat = np.arcsin(np.clip(y, -1, 1))
        fx = (lon / (2 * np.pi) + 0.5) * w - 0.5
        fy = (0.5 - lat / np.pi) * h - 0.5
        x0 = np.floor(fx).astype(int); y0 = np.floor(fy).astype(int)
        tx = (fx - x0)[..., None]; ty = (fy - y0)[..., None]
        x0m, x1m = x0 % w, (x0 + 1) % w
        y0c, y1c = np.clip(y0, 0, h - 1), np.clip(y0 + 1, 0, h - 1)
        face = (equirect[y0c, x0m] * (1 - tx) + equirect[y0c, x1m] * tx) * (1 - ty) + \
               (equirect[y1c, x0m] * (1 - tx) + equirect[y1c, x1m] * tx) * ty
        faces.append(face)
    return faces


# ------------------------------------------------------------------ the generators

COLORS = {
    "ground": (0.62, 0.62, 0.60, 1), "red": (0.80, 0.22, 0.18, 1), "orange": (0.92, 0.52, 0.16, 1),
    "yellow": (0.90, 0.78, 0.24, 1), "green": (0.30, 0.66, 0.32, 1), "teal": (0.18, 0.62, 0.62, 1),
    "blue": (0.24, 0.42, 0.82, 1), "purple": (0.56, 0.32, 0.74, 1), "white": (0.88, 0.88, 0.86, 1),
    "stone": (0.72, 0.68, 0.60, 1),
}


def gen_cube(out, _):
    return write_gltf(out / "models/ps5/cube.gltf", [dict(box((10, 10, 10)), name="cube")])


def gen_tunnel(out, _):
    # 100 long on X, radius 2, seen from inside: as texturemipmapgen's camera expects
    m = cylinder(radius=2.0, height=100.0, segments=64, caps=False, inward=True)
    m["uv"] = np.stack([(m["pos"][:, 1] / 100.0 + 0.5) * 12.0,
                        np.arctan2(m["pos"][:, 2], m["pos"][:, 0]) / (2 * np.pi) * 2.0], axis=1)
    m["pos"] = m["pos"][:, [1, 0, 2]]
    m["nrm"] = m["nrm"][:, [1, 0, 2]]
    m["idx"] = m["idx"].reshape(-1, 3)[:, [0, 2, 1]].reshape(-1)
    return write_gltf(out / "models/ps5/tunnel.gltf", [dict(m, name="tunnel")])


def shadow_scene_columns():
    parts = [dict(plane(size=22.0, divisions=8), color=COLORS["ground"], name="ground")]
    colors = ["red", "orange", "yellow", "green", "teal", "blue", "purple", "white"]
    for i, c in enumerate(colors):
        a = i / len(colors) * 2 * math.pi
        parts.append(dict(transformed(cylinder(0.45, 4.5), translate=(6.5 * math.cos(a), 2.25, 6.5 * math.sin(a))),
                          color=COLORS[c], name=f"column{i}"))
    parts.append(dict(transformed(torus_knot(radius=2.4, tube=0.55), translate=(0, 2.6, 0)), color=COLORS["stone"], name="knot"))
    parts.append(dict(transformed(box((2.0, 1.0, 2.0)), translate=(0, 0.5, 0)), color=COLORS["white"], name="plinth"))
    return parts


def shadow_scene_shapes():
    parts = [dict(plane(size=22.0, divisions=8), color=COLORS["ground"], name="ground")]
    shapes = [(uv_sphere(1.2), (-4, 1.2, -3), "red"), (box((2, 2, 2)), (0, 1, -4), "yellow"),
              (cone(1.2, 2.8), (4, 1.4, -3), "green"), (cylinder(0.9, 3.0), (-4.5, 1.5, 2.5), "blue"),
              (torus_knot(radius=1.3, tube=0.35), (0, 1.6, 1.5), "orange"), (uv_sphere(0.8), (4.5, 0.8, 3), "purple"),
              (box((1.2, 4.0, 1.2)), (1.8, 2.0, 5.2), "teal"), (box((5.0, 0.6, 1.4)), (-1.5, 0.3, 5.5), "white")]
    for i, (shape, where, c) in enumerate(shapes):
        parts.append(dict(transformed(shape, translate=where), color=COLORS[c], name=f"shape{i}"))
    return parts


def gen_shadow_scenes(out, _):
    write_gltf(out / "models/ps5/shadowscene_columns.gltf", shadow_scene_columns())
    write_gltf(out / "models/ps5/shadowscene_shapes.gltf", shadow_scene_shapes())
    return out / "models/ps5/shadowscene_columns.gltf"


def gen_imgui_scene(out, _):
    # As small as the scene imgui's camera expects (about 3 units across)
    models = [dict(transformed(torus_knot(radius=0.42, tube=0.1), translate=(0, 0.1, 0)), color=COLORS["orange"], name="knot"),
              dict(transformed(uv_sphere(0.22), translate=(-0.62, -0.32, 0.35)), color=COLORS["red"], name="sphere"),
              dict(transformed(box((0.4, 0.4, 0.4)), translate=(0.6, -0.34, 0.3), rotate_y=0.6), color=COLORS["blue"], name="box"),
              dict(transformed(cone(0.22, 0.5), translate=(0.1, -0.29, 0.75)), color=COLORS["green"], name="cone")]
    background = [dict(transformed(cylinder(1.4, 0.08, segments=96), translate=(0, -0.58, 0)), color=COLORS["ground"], name="floor"),
                  dict(transformed(cylinder(1.4, 2.4, segments=96, caps=False, inward=True), translate=(0, 0.62, 0)),
                       color=(0.30, 0.32, 0.38, 1), name="wall")]
    rings = []
    for i in range(12):
        a = i / 12 * 2 * math.pi
        rings.append(dict(transformed(uv_sphere(0.06, 12, 24), translate=(1.0 * math.cos(a), 1.05, 1.0 * math.sin(a))),
                          color=COLORS["yellow" if i % 2 else "teal"], name=f"light{i}"))
    write_gltf(out / "models/ps5/imgui_models.gltf", models)
    write_gltf(out / "models/ps5/imgui_background.gltf", background)
    write_gltf(out / "models/ps5/imgui_ring.gltf", rings)
    return out / "models/ps5/imgui_models.gltf"


def gen_instancing(out, download):
    # A rock: a displaced icosphere 0.09 across, as the asteroid instancing expects
    verts, faces = icosphere(3)
    bumps = value_noise_3d(verts * 3 + 7, seed=11, frequency=1.0) * 0.45 + value_noise_3d(verts * 9, seed=12, frequency=1.0) * 0.15
    verts = verts * (0.75 + bumps[:, None]) * np.array([1.0, 0.8, 0.9], np.float32) * 0.09
    uv = np.stack([np.arctan2(verts[:, 2], verts[:, 0]) / (2 * np.pi) + 0.5,
                   np.arccos(np.clip(verts[:, 1] / np.linalg.norm(verts, axis=1), -1, 1)) / np.pi], axis=1)
    write_gltf(out / "models/ps5/rock.gltf", [mesh(verts, smooth_normals(verts, faces), faces.reshape(-1), uv, name="rock")])
    # The planet: a sphere of radius 4
    write_gltf(out / "models/ps5/planet.gltf", [dict(uv_sphere(4.0, 64, 128), name="planet")])
    # Its surface: cooled crust broken by glowing lava, made here from noise
    n = noise_2d(1024, 512, seed=21, octaves=6, scale=6)
    cracks = np.clip(1 - np.abs(noise_2d(1024, 512, seed=22, octaves=4, scale=8) - 0.5) * 9, 0, 1) ** 3
    crust = np.stack([0.20 + 0.22 * n, 0.14 + 0.13 * n, 0.12 + 0.10 * n], axis=-1)
    lava = np.stack([np.ones_like(n), 0.35 + 0.45 * n, 0.05 * np.ones_like(n)], axis=-1)
    color = crust * (1 - cracks[..., None]) + lava * cracks[..., None]
    image = np.concatenate([np.clip(color * 255, 0, 255), np.full((512, 1024, 1), 255)], axis=-1).astype(np.uint8)
    image = np.asarray(Image.fromarray(image).resize((512, 512), Image.LANCZOS))
    ktx_2d(out / "textures/ps5/lavaplanet_rgba.ktx", image)
    # The rocks' colours: four CC0 rock surfaces from Poly Haven, one layer each
    layers = [rgba(Image.open(download(url, sha)), 512) for url, sha in ROCK_TEXTURES]
    ktx_array(out / "textures/ps5/texturearray_rocks_rgba.ktx", layers)
    return out / "models/ps5/rock.gltf"


def leaf_layer(kind, seed):
    """One plant's picture, drawn here: 512x512 with alpha."""
    rng = np.random.default_rng(seed)
    image = Image.new("RGBA", (512, 512), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    greens = [(52, 120, 40), (70, 140, 52), (92, 160, 60), (40, 100, 36)]
    if kind == "grass":
        for _ in range(60):
            x = rng.uniform(40, 472); h = rng.uniform(220, 500); lean = rng.uniform(-60, 60); w = rng.uniform(6, 14)
            g = greens[rng.integers(len(greens))]
            draw.polygon([(x - w, 511), (x + w, 511), (x + lean, 511 - h)], fill=g + (255,))
    elif kind in ("fern", "bush"):
        for _ in range(26 if kind == "fern" else 70):
            cx = rng.uniform(80, 432) if kind == "bush" else 256 + rng.uniform(-40, 40)
            cy = rng.uniform(120, 470) if kind == "bush" else rng.uniform(80, 480)
            rx, ry = (rng.uniform(30, 70), rng.uniform(16, 30)) if kind == "bush" else (rng.uniform(90, 200), rng.uniform(10, 18))
            g = greens[rng.integers(len(greens))]
            if kind == "fern":
                side = 1 if rng.random() > 0.5 else -1
                draw.ellipse([cx, cy - ry, cx + side * rx, cy + ry] if side > 0 else [cx - rx, cy - ry, cx, cy + ry], fill=g + (255,))
            else:
                draw.ellipse([cx - rx, cy - ry, cx + rx, cy + ry], fill=g + (255,))
        if kind == "fern":
            draw.line([(256, 511), (256, 70)], fill=(60, 90, 40, 255), width=8)
    else:  # flowers
        for _ in range(24):
            x = rng.uniform(60, 452); h = rng.uniform(200, 460)
            draw.line([(x, 511), (x + rng.uniform(-20, 20), 511 - h)], fill=(60, 120, 50, 255), width=6)
        petals = {"flower_red": (220, 60, 60), "flower_yellow": (240, 210, 70), "flower_blue": (110, 120, 230)}[kind]
        for _ in range(18):
            x = rng.uniform(60, 452); y = rng.uniform(60, 300); r = rng.uniform(16, 30)
            draw.ellipse([x - r, y - r, x + r, y + r], fill=petals + (255,))
            draw.ellipse([x - r / 3, y - r / 3, x + r / 3, y + r / 3], fill=(250, 230, 120, 255))
    shading = (noise_2d(512, 512, seed=seed + 100, octaves=3, scale=6) * 0.5 + 0.75)
    array = np.asarray(image).astype(np.float32)
    array[..., :3] *= shading[..., None]
    return np.clip(array, 0, 255).astype(np.uint8)


PLANT_KINDS = ["grass", "fern", "bush", "flower_red", "flower_yellow", "flower_blue"]


def gen_indirectdraw(out, download):
    # One node a plant: two crossed quads, up to 0.5 tall (1.5 at the largest instance
    # scale), the texture array layer by node order
    plants = []
    for i, kind in enumerate(PLANT_KINDS):
        h = {"grass": 0.35, "fern": 0.5, "bush": 0.4}.get(kind, 0.45); w = h * 0.9
        pos, nrm, uv, idx = [], [], [], []
        for q in range(2):
            a = q * math.pi / 2 + 0.3
            dx, dz = math.cos(a) * w / 2, math.sin(a) * w / 2
            base = len(pos)
            pos += [(-dx, 0, -dz), (dx, 0, dz), (dx, h, dz), (-dx, h, -dz)]
            n = (-math.sin(a), 0.4, math.cos(a))
            nrm += [n] * 4
            uv += [(0, 1), (1, 1), (1, 0), (0, 0)]
            idx += [base, base + 1, base + 2, base, base + 2, base + 3, base, base + 2, base + 1, base, base + 3, base + 2]
        m = mesh(pos, nrm, idx, uv, name=f"plant_{kind}")
        m["nrm"] = m["nrm"] / np.linalg.norm(m["nrm"], axis=1, keepdims=True)
        plants.append(m)
    write_gltf(out / "models/ps5/plants.gltf", plants)
    write_gltf(out / "models/ps5/ground_disc.gltf", [dict(disc(radius=25.0), name="ground")])
    write_gltf(out / "models/ps5/skysphere.gltf", [dict(uv_sphere(1.0, 32, 64), name="skysphere")])
    ktx_array(out / "textures/ps5/texturearray_plants_rgba.ktx", [leaf_layer(k, 40 + i) for i, k in enumerate(PLANT_KINDS)])
    ktx_2d(out / "textures/ps5/ground_dry_rgba.ktx", rgba(Image.open(download(*GROUND_TEXTURE)), 512))
    return out / "models/ps5/plants.gltf"


def gen_deferred_floor(out, download):
    # 40 by 40 at y = -2.35, with tangents for the normal map, as deferred's floor
    write_gltf(out / "models/ps5/floor.gltf", [dict(plane(size=40.0, uv_repeat=8.0, y=-2.35, divisions=4), name="floor")])
    ktx_2d(out / "textures/ps5/cobblestone_color_rgba.ktx", rgba(Image.open(download(*COBBLE_COLOR))))
    ktx_2d(out / "textures/ps5/cobblestone_normal_rgba.ktx", rgba(Image.open(download(*COBBLE_NORMAL))))
    return out / "models/ps5/floor.gltf"


def gen_metal_plate(out, download):
    # One level: texturemipmapgen makes the rest at run time
    ktx_2d(out / "textures/ps5/metalplate_nomips_rgba.ktx", rgba(Image.open(download(*METAL_PLATE)), 1024), mips=False)
    return out / "textures/ps5/metalplate_nomips_rgba.ktx"


def gen_particles(out, _):
    a = (np.arange(64) + 0.5) / 64 * 2 - 1
    x, y = np.meshgrid(a, a)
    r = np.sqrt(x * x + y * y)
    glow = np.clip(1 - r, 0, 1) ** 2.2
    sprite = np.stack([glow * 255] * 4, axis=-1).astype(np.uint8)
    ktx_2d(out / "textures/ps5/particle01_rgba.ktx", sprite, mips=False)
    stops = [(0.0, (255, 250, 230)), (0.25, (255, 200, 80)), (0.5, (240, 90, 40)), (0.75, (120, 40, 140)), (1.0, (20, 30, 120))]
    ramp = np.zeros((1, 256, 4), np.uint8)
    for i in range(256):
        t = i / 255
        for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
            if t0 <= t <= t1:
                f = (t - t0) / (t1 - t0)
                ramp[0, i, :3] = [round(c0[k] + (c1[k] - c0[k]) * f) for k in range(3)]
        ramp[0, i, 3] = 255
    ktx_2d(out / "textures/ps5/particle_gradient_rgba.ktx", ramp, mips=False)
    return out / "textures/ps5/particle01_rgba.ktx"


def gen_space_cubemap(out, _):
    """A starfield with faint nebulae, made here: no stitching, the stars are
    placed on the sphere and each face draws the ones in front of it."""
    size = 1024
    rng = np.random.default_rng(31)
    stars = rng.normal(size=(9000, 3)); stars /= np.linalg.norm(stars, axis=1, keepdims=True)
    brightness = rng.random(9000) ** 6
    tint = rng.random((9000, 3)) * 0.25 + 0.75
    faces = []
    a = (np.arange(size) + 0.5) / size * 2 - 1
    u, v = np.meshgrid(a, a)
    ones = np.ones_like(u)
    axes = [(ones, -v, -u), (-ones, -v, u), (u, ones, v), (u, -ones, -v), (u, -v, ones), (-u, -v, -ones)]
    face_frames = [((1, 0, 0), (0, 0, -1), (0, -1, 0)), ((-1, 0, 0), (0, 0, 1), (0, -1, 0)), ((0, 1, 0), (1, 0, 0), (0, 0, 1)),
                   ((0, -1, 0), (1, 0, 0), (0, 0, -1)), ((0, 0, 1), (1, 0, 0), (0, -1, 0)), ((0, 0, -1), (-1, 0, 0), (0, -1, 0))]
    for (x, y, z), (normal, right, down) in zip(axes, face_frames):
        d = np.stack([x, y, z], -1); d /= np.linalg.norm(d, axis=-1, keepdims=True)
        nebula = value_noise_3d(d.reshape(-1, 3) * 2.5 + 5, seed=33, frequency=1.0).reshape(size, size)
        nebula2 = value_noise_3d(d.reshape(-1, 3) * 5 + 9, seed=34, frequency=1.0).reshape(size, size)
        image = np.zeros((size, size, 3), np.float32)
        image += np.clip(nebula - 0.55, 0, 1)[..., None] * np.array([90, 40, 140]) * 2.2
        image += np.clip(nebula2 - 0.6, 0, 1)[..., None] * np.array([30, 80, 150]) * 2.2
        normal, right, down = map(np.array, (normal, right, down))
        facing = stars @ normal
        front = facing > 0
        sx = (stars[front] @ right) / facing[front]; sy = (stars[front] @ down) / facing[front]
        px = ((sx + 1) / 2 * size).astype(int); py = ((sy + 1) / 2 * size).astype(int)
        inside = (px >= 0) & (px < size) & (py >= 0) & (py < size)
        for X, Y, b, t in zip(px[inside], py[inside], brightness[front][inside], tint[front][inside]):
            image[Y, X] += 255 * (0.25 + 0.75 * b) * t
            if b > 0.5:
                for ox, oy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    if 0 <= X + ox < size and 0 <= Y + oy < size:
                        image[Y + oy, X + ox] += 120 * b * t
        faces.append(np.concatenate([np.clip(image, 0, 255), np.full((size, size, 1), 255)], -1).astype(np.uint8))
    # One level, as the file it replaces: the skybox is seen at about one texel a pixel
    write_ktx(out / "textures/ps5/cubemap_space.ktx", "rgba8", [faces], faces=6)
    return out / "textures/ps5/cubemap_space.ktx"


def gen_hdri_cube(out, download):
    equirect = read_hdr(download(*HDRI))
    # Half floats end at 65504: the sun's brightest texels are clamped below it
    faces = [np.minimum(f, 60000.0).astype(np.float32) for f in cube_faces_from_equirect(equirect, 512)]
    faces = [np.concatenate([f, np.ones(f.shape[:2] + (1,), np.float32)], -1).astype(np.float16) for f in faces]
    ktx_cube(out / "textures/ps5/hdr/kloofendal_cube.ktx", faces, "rgba16f")
    return out / "textures/ps5/hdr/kloofendal_cube.ktx"


def compute_tangents(pos, nrm, uv, idx):
    tan = np.zeros((len(pos), 3)); bit = np.zeros((len(pos), 3))
    tri = idx.reshape(-1, 3)
    p0, p1, p2 = pos[tri[:, 0]], pos[tri[:, 1]], pos[tri[:, 2]]
    w0, w1, w2 = uv[tri[:, 0]], uv[tri[:, 1]], uv[tri[:, 2]]
    e1, e2 = p1 - p0, p2 - p0
    d1, d2 = w1 - w0, w2 - w0
    r = d1[:, 0] * d2[:, 1] - d2[:, 0] * d1[:, 1]
    r = np.where(np.abs(r) < 1e-12, 1e-12, r)
    sdir = (e1 * d2[:, 1:2] - e2 * d1[:, 1:2]) / r[:, None]
    tdir = (e2 * d1[:, 0:1] - e1 * d2[:, 0:1]) / r[:, None]
    for k in range(3):
        np.add.at(tan, tri[:, k], sdir); np.add.at(bit, tri[:, k], tdir)
    t = tan - nrm * np.sum(nrm * tan, axis=1, keepdims=True)
    t /= np.maximum(np.linalg.norm(t, axis=1, keepdims=True), 1e-9)
    w = np.where(np.sum(np.cross(nrm, t) * bit, axis=1) < 0, -1.0, 1.0)
    return np.concatenate([t, w[:, None]], axis=1).astype(np.float32)


def read_accessor(gltf, binary, index):
    accessor = gltf["accessors"][index]
    view = gltf["bufferViews"][accessor["bufferView"]]
    count = accessor["count"]
    width = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[accessor["type"]]
    dtype = {5126: np.float32, 5125: np.uint32, 5123: np.uint16, 5121: np.uint8}[accessor["componentType"]]
    offset = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    stride = view.get("byteStride", 0) or width * np.dtype(dtype).itemsize
    raw = np.frombuffer(binary, np.uint8, count=stride * (count - 1) + width * np.dtype(dtype).itemsize, offset=offset)
    out = np.lib.stride_tricks.as_strided(raw.view(np.uint8), shape=(count, width * np.dtype(dtype).itemsize), strides=(stride, 1))
    return np.ascontiguousarray(out).view(dtype).reshape(count, width) if width > 1 else np.ascontiguousarray(out).view(dtype).reshape(count)


def gen_pbr_camera(out, download):
    """Poly Haven's CC0 vintage video camera, made into what pbrtexture reads:
    one glTF with tangents, about 1.4 across like the model it replaces, and
    its maps as separate KTX files (albedo and normal RGBA, the rest R8)."""
    gltf = json.loads(Path(download(*CAMERA["gltf"])).read_text())
    binary = Path(download(*CAMERA["bin"])).read_bytes()
    parts = []
    for m in gltf["meshes"]:
        for p in m["primitives"]:
            a = p["attributes"]
            pos = read_accessor(gltf, binary, a["POSITION"]).astype(np.float64)
            nrm = read_accessor(gltf, binary, a["NORMAL"]).astype(np.float64)
            uv = read_accessor(gltf, binary, a["TEXCOORD_0"]).astype(np.float64)
            idx = read_accessor(gltf, binary, p["indices"]).astype(np.uint32)
            parts.append((pos, nrm, uv, idx))
    lo = np.min([p[0].min(axis=0) for p in parts], axis=0); hi = np.max([p[0].max(axis=0) for p in parts], axis=0)
    center, scale = (lo + hi) / 2, 1.4 / np.max(hi - lo)
    meshes = []
    for i, (pos, nrm, uv, idx) in enumerate(parts):
        pos = (pos - center) * scale
        meshes.append(mesh(pos, nrm, idx, uv, name=f"camera{i}", tan=compute_tangents(pos, nrm, uv, idx)))
    folder = out / "models/ps5/videocamera"
    write_gltf(folder / "videocamera.gltf", meshes)
    # 1024x1024: the camera covers a third of the 4K screen at most
    ktx_2d(folder / "albedo.ktx", rgba(Image.open(download(*CAMERA["diff"])), 1024))
    ktx_2d(folder / "normal.ktx", rgba(Image.open(download(*CAMERA["nor_gl"])), 1024))
    for name, key in (("ao", "ao"), ("metallic", "metal"), ("roughness", "rough")):
        gray = np.asarray(Image.open(download(*CAMERA[key])).convert("L").resize((1024, 1024), Image.LANCZOS), np.uint8)
        chain = mip_chain(gray[..., None])
        write_ktx(folder / f"{name}.ktx", "r8", [[level] for level in chain])
    return folder


def node_matrix(node):
    if "matrix" in node:
        return np.array(node["matrix"], np.float64).reshape(4, 4).T
    t = np.eye(4); t[:3, 3] = node.get("translation", [0, 0, 0])
    x, y, z, w = node.get("rotation", [0, 0, 0, 1])
    r = np.eye(4)
    r[:3, :3] = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                 [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                 [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
    sc = np.diag(list(node.get("scale", [1, 1, 1])) + [1])
    return t @ r @ sc


def world_bounds(gltf):
    """The scene's bounding box: each mesh's POSITION min and max through its nodes' transforms."""
    lo, hi = np.full(3, np.inf), np.full(3, -np.inf)
    def visit(index, parent):
        node = gltf["nodes"][index]
        m = parent @ node_matrix(node)
        nonlocal lo, hi
        if "mesh" in node:
            for p in gltf["meshes"][node["mesh"]]["primitives"]:
                a = gltf["accessors"][p["attributes"]["POSITION"]]
                for corner in np.array(np.meshgrid(*zip(a["min"], a["max"]))).T.reshape(-1, 3):
                    w = m @ np.append(corner, 1)
                    lo, hi = np.minimum(lo, w[:3]), np.maximum(hi, w[:3])
        for child in node.get("children", []):
            visit(child, m)
    for root in gltf["scenes"][gltf.get("scene", 0)]["nodes"]:
        visit(root, np.eye(4))
    return lo, hi


def gen_lantern(out, download):
    """Khronos's CC0 Lantern for multisampling, dynamicrendering and the
    starter, scaled to what each one's camera frames (5.5, 8 and 1.6 tall) and
    centred where it looks
    (multisampling's camera is turned 90 degrees and offset by (2.5, 2.5): it
    looks through (0, 2.5, 2.5) in glTF's axes; dynamicrendering's through
    the origin). A parent node does both, in a .gltf of each sample's beside
    Khronos's own buffer and images."""
    folder = out / "models/ps5/lantern"
    folder.mkdir(parents=True, exist_ok=True)
    gltf = json.loads(Path(download(*LANTERN["Lantern.gltf"])).read_text())
    for name in [b["uri"] for b in gltf["buffers"]] + [i["uri"] for i in gltf.get("images", [])]:
        shutil.copyfile(download(*LANTERN[name]), folder / name)
    scene = gltf["scenes"][gltf.get("scene", 0)]
    roots = list(scene["nodes"])
    lo, hi = world_bounds(gltf)
    center = (lo + hi) / 2
    # (multisampling's is turned a quarter so its camera sees the lantern beside the post)
    for name, height, target, turn in (("lantern_multisampling", 5.5, (0.0, 2.5, 2.5), math.pi / 2),
                                       ("lantern_dynamicrendering", 8.0, (0.0, 0.0, 0.0), 0.0),
                                       ("lantern_starter", 1.6, (0.0, 0.0, 0.0), 0.0)):
        variant = json.loads(json.dumps(gltf))
        scale = height / (hi[1] - lo[1])
        c, s_ = math.cos(turn), math.sin(turn)
        turned = np.array([c * center[0] + s_ * center[2], center[1], -s_ * center[0] + c * center[2]])
        variant["nodes"].append({"name": "ps5_place", "children": roots, "scale": [scale] * 3,
                                 "rotation": [0.0, math.sin(turn / 2), 0.0, math.cos(turn / 2)],
                                 "translation": [float(t - v * scale) for t, v in zip(target, turned)]})
        variant["scenes"][variant.get("scene", 0)]["nodes"] = [len(variant["nodes"]) - 1]
        (folder / f"{name}.gltf").write_text(json.dumps(variant, indent=1) + "\n")
    return folder


# ------------------------------------------------------------------ the guided setup (MihawkOS)

# The Chinese and Japanese faces: Google Fonts' Noto Sans SC and JP (OFL-1.1, ps5/licenses/NotoSansCJK-OFL.txt), the
# variable TrueType files at a pinned revision. TrueType, not the CFF .otf: stb_truetype's distance fields draw only
# lines and quadratic curves, and CFF's cubic outlines came out torn.
GOOGLE_FONTS = "https://raw.githubusercontent.com/google/fonts/47d54ca1ebb14fd864695f1d39dc734f39017c33/ofl/"
SETUP_FACES = {
    "noto-sans-sc": (GOOGLE_FONTS + "notosanssc/NotoSansSC%5Bwght%5D.ttf", "a3041811a78c361b1de50f953c805e0244951c21c5bd412f7232ef0d899af0da"),
    "noto-sans-jp": (GOOGLE_FONTS + "notosansjp/NotoSansJP%5Bwght%5D.ttf", "c2f3b4d463500a2ddcd3849cded1fceeb9fd6d1c32e6cbecd568453ba50fc68f"),
}


def gen_setup(out, download):
    """The language tiles' flags, rasterized from the SVGs kept in ps5/assets/flags (lipis/flag-icons, MIT, pinned
    in its SOURCE.json) by rsvg-convert; and each CJK language's face, baked with only the characters its text uses
    (src/pw_setup.c) by tools/bake-huifont.cpp."""
    import re
    import subprocess
    for tool in ("rsvg-convert", "clang++"):
        if not shutil.which(tool):
            raise SystemExit(f"generator setup: {tool} is needed (librsvg)")
    flags = out / "setup" / "flags"
    flags.mkdir(parents=True, exist_ok=True)
    for svg in sorted((Path(__file__).resolve().parents[1] / "assets" / "flags").glob("*.svg")):
        subprocess.run(["rsvg-convert", "-w", "240", "-h", "180", "-f", "png", "-o", str(flags / (svg.stem + ".png")),
                        str(svg)], check=True)
    root = Path(__file__).resolve().parents[2]
    work = out / "setup-work"   # the baker and its character lists: not shipped
    work.mkdir(parents=True, exist_ok=True)
    baker = work / "bake-huifont"
    subprocess.run(["clang++", "-std=c++20", "-O2", "-w", "-I", str(root / ".deps/hui"), str(root / "tools/bake-huifont.cpp"),
                    "-o", str(baker)], check=True)
    source = (root / "src/pw_setup.c").read_text()
    fonts = out / "setup" / "fonts"
    fonts.mkdir(parents=True, exist_ok=True)
    for language in re.finditer(r'\{"[^"]+", "([^"]+)", "[^"]+", "[^"]+", "([^"]+)", (text_\w+)\}', source):
        own_name, face, table = language.groups()
        block = re.search(r"static const char \*const " + table + r"\[PW_TEXT_COUNT\] = \{(.*?)\n\};", source, re.S).group(1)
        text = own_name + "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', block)) + "".join(map(chr, range(0x20, 0x7F)))
        characters = work / (face + ".txt")
        characters.write_text(text)
        # Only these characters, at the Medium weight (the variable font's default is Thin)
        from fontTools import subset
        from fontTools.ttLib import TTFont
        from fontTools.varLib import instancer
        url, sha256 = SETUP_FACES[face]
        # The source's own timestamps, not the time of the cut: a new head.modified each build gave a face of the same
        # size with other bytes, which the title deploy (by size) skipped (c4a3458, 2026-10-27)
        variable = TTFont(str(download(url, sha256)), recalcTimestamp=False)
        options = subset.Options()
        options.layout_features = ["*"]
        subsetter = subset.Subsetter(options)
        subsetter.populate(text=text)
        subsetter.subset(variable)
        medium = work / (face + ".ttf")
        instancer.instantiateVariableFont(variable, {"wght": 500}).save(str(medium))
        subprocess.run([str(baker), str(medium), str(fonts / (face + ".huifont")), str(characters), "56", "8", "2048"],
                       check=True)
        # The cut face itself too: the title draws the Steam guide's session screens with it (src/pw_steam_screen.h)
        shutil.copyfile(medium, fonts / (face + ".ttf"))


def gen_launcher(out, _):
    """The main menu's pictures (examples/ps5proton/home_grid.hpp), rasterized by rsvg-convert at twice the size they are
    drawn on the menu's 1920x1080 page, one texel a pixel on the 3840x2160 swapchain: the title's Aero user tile
    (ps5/tools/icon0.svg), the pointer, the dock's Settings cog and Steam's tile (ps5/assets/launcher/*.svg), and the
    README's orbs for the rest of the dock and the tiles without art (docs/media/readme/orb-*-day.svg)."""
    import subprocess
    if not shutil.which("rsvg-convert"):
        raise SystemExit("generator launcher: rsvg-convert is needed (librsvg)")
    root = Path(__file__).resolve().parents[2]
    folder = out / "launcher"
    shutil.rmtree(folder / "orbs", ignore_errors=True)   # an orb the menu no longer draws leaves the title too
    (folder / "orbs").mkdir(parents=True, exist_ok=True)
    pictures = [(root / "ps5/tools/icon0.svg", folder / "aero-logo.png", 432),
                (root / "ps5/assets/launcher/cursor.svg", folder / "cursor.png", 224),
                (root / "ps5/assets/launcher/dock-settings.svg", folder / "dock-settings.png", 208),
                (root / "ps5/assets/launcher/steam-tile.svg", folder / "steam-tile.png", 208)]
    for orb in ("folder", "document", "app-browser", "app-generic"):
        pictures.append((root / f"docs/media/readme/orb-{orb}-day.svg", folder / "orbs" / f"orb-{orb}.png", 208))
    for source, target, size in pictures:
        subprocess.run(["rsvg-convert", "-w", str(size), "-h", str(size), "-f", "png", "-o", str(target), str(source)],
                       check=True)


# Files beside this one that a generator reads: a change makes it run again
GENERATOR_INPUTS = {"setup": ["src/pw_setup.c", "tools/bake-huifont.cpp", "ps5/assets/flags/*.svg"],
                    "launcher": ["ps5/tools/icon0.svg", "ps5/assets/launcher/*.svg", "docs/media/readme/orb-*-day.svg"]}

GENERATORS = {
    "cube": gen_cube, "tunnel": gen_tunnel, "shadow_scenes": gen_shadow_scenes, "imgui_scene": gen_imgui_scene,
    "instancing": gen_instancing, "indirectdraw": gen_indirectdraw, "deferred_floor": gen_deferred_floor,
    "metal_plate": gen_metal_plate, "particles": gen_particles, "space_cubemap": gen_space_cubemap,
    "hdri_cube": gen_hdri_cube, "pbr_camera": gen_pbr_camera, "lantern": gen_lantern,
    "setup": gen_setup, "launcher": gen_launcher,
}

# The downloads (URL, SHA-256): CC0, Poly Haven and Khronos (ps5/assets.json)
PH = "https://dl.polyhaven.org/file/ph-assets/"
KHRONOS = "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/f36bfdabd1031c3cf6689a50570b8cdf3678b49c/Models/"
HDRI = (PH + "HDRIs/hdr/2k/kloofendal_48d_partly_cloudy_2k.hdr", "3fbd33f279f29bd64925c1dd3214fd46c627493d21f1100248b6b1098da4d06e")
METAL_PLATE = (PH + "Textures/jpg/1k/metal_plate/metal_plate_diff_1k.jpg", "c6b6739eac2c75ccc898e753d7833df24eafbdb9aa92fb6eabfb92abd1a680a5")
COBBLE_COLOR = (PH + "Textures/jpg/1k/cobblestone_floor_01/cobblestone_floor_01_diff_1k.jpg", "7630d6a59501cab4d2295b2be47bad892b8dae49895b4bee6bc26fd45c423e05")
COBBLE_NORMAL = (PH + "Textures/jpg/1k/cobblestone_floor_01/cobblestone_floor_01_nor_gl_1k.jpg", "e8a07bd38fdaeb92a419bc4829631847df7c03fd657dd32b44f19e2550a4fb35")
GROUND_TEXTURE = (PH + "Textures/jpg/1k/dry_ground_01/dry_ground_01_diff_1k.jpg", "75222fc97a82b635a09cf8f2891dd58bc42e49599b9fda920b2c50193fe85e9f")
ROCK_TEXTURES = [(PH + f"Textures/jpg/1k/{r}/{r}_diff_1k.jpg", h) for r, h in (
    ("rock_boulder_dry", "20f653a42fb24f42aa40641663070d578678da49af18cc1049dbdd4ed8679f3c"), ("rock_face", "cce4b50517161264bdef196f5e247e328ca3739083cd6044ad3cc54d88cb82e2"),
    ("aerial_rocks_02", "708373b2af7fd07ac8de1500556006aaf904c41a99985a849cacc78874f38eac"), ("dry_riverbed_rock", "5dc1fd5777f613451a703d2fc292d4a4e79987f90b991efa98c7ca02458c6a8a"))]
CAMERA = {
    "gltf": (PH + "Models/gltf/2k/vintage_video_camera/vintage_video_camera_2k.gltf", "3866057e38a97ac33cba4fec3bd04efff5cf7d1f6f4bc121285bf5b700e7438a"),
    "bin": (PH + "Models/gltf/8k/vintage_video_camera/vintage_video_camera.bin", "1c92781aba4557dda38c66b1eb328fdb2cc0869e5af40413d69de13a9e28d9f4"),
    **{m: (PH + f"Models/jpg/2k/vintage_video_camera/vintage_video_camera_{m}_2k.jpg", h) for m, h in (
        ("diff", "8a394452ca50544f4201571e1fa6801c2a3afe6b4f7f7dc5d0db85bd58e7b0d4"), ("nor_gl", "273d8841d4d694d928e0913ebd0d4a41cefd5d71a5ba3c864a0a3bc6ee750d86"), ("ao", "d76441c0ace6d353c9f7d62a77b3c3e891f51886bc2a1fb4c948fdda50ba4dcd"),
        ("rough", "85d3ac5b642db3d2fb099111a5c352cf5cb97436fe4b90a33a4a50e95fe38b52"), ("metal", "a576883de1ad5a4be9112adaa4061c992670f6a249fcb3425e4d19d00e39a7f5"))},
}
LANTERN = {name: (KHRONOS + "Lantern/glTF/" + name, h) for name, h in (
    ("Lantern.gltf", "2106fea76c458caf43078c34ae7da7016cedd1f22e6f4badf94590be590f53d6"), ("Lantern.bin", "ef0e2e89e4b14ff9035b43867a16dc3218569b29363ccb16b283b86f1dc50eb0"),
    ("Lantern_baseColor.png", "a2d6aa660f0b9ce46b3863e955248454e73be96063218b799af9f750e0f7be71"), ("Lantern_roughnessMetallic.png", "01b8105756fd86f13e66602f85ee03e7e5daf22a8531f32bef8bbe678cf38cf6"),
    ("Lantern_normal.png", "3318a4d3bef8be53c192d87b7401145493774126c54a22e50e1269f9476065fa"), ("Lantern_emissive.png", "eafa6390501d537f5bbcf92c64ad1c39d2493aef4508e9cc9d537771efe8bad7"))}


def build(name, out_dir, download):
    """Run the generator `name` into out_dir, unless it ran already with this
    version of this file (a stamp records the file's hash)."""
    import hashlib
    out_dir = Path(out_dir)
    version = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    root = Path(__file__).resolve().parents[2]
    for extra in GENERATOR_INPUTS.get(name, []):
        for path in sorted(root.glob(extra)):
            version = hashlib.sha256((version + hashlib.sha256(path.read_bytes()).hexdigest()).encode()).hexdigest()
    stamp = out_dir / ".stamps" / name
    if stamp.is_file() and stamp.read_text() == version:
        return
    print(f"generate {name}")
    GENERATORS[name](out_dir, download)
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.write_text(version)
