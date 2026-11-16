#!/usr/bin/env python3
"""Builds Hearthcooking's kitchen: every mesh and texture is made here, nothing of Bethesda's is used.

    python3 tools/make_meshes.py

Writes assets/meshes/Hearthcooking/*.nif and assets/textures/Hearthcooking/*.dds. The NIF layout (header, node,
shape, shader and texture-set blocks) is taken from tools/template.nif, a mesh Skyrim Yacht's make_meshes.py wrote
(itself made the same way, from a vanilla mesh's block layout only); every vertex, triangle and texel is ours.
Units are game units, z up, matching src/sim/Kitchen.h (layout).

The food is the game's own: the ingredients and the dishes are drawn from their own meshes in the player's game.
"""
import io, math, os, struct, sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from nif import Nif

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "assets")
TEX = "textures\\Hearthcooking\\"
rng = np.random.default_rng(11)

# ---- the layout (keep in step with src/sim/Kitchen.h)
TOP_W, TOP_D, HEARTH_EDGE = 74.0, 40.0, 5.0
BOARD_W, BOARD_D, BOARD_H = 20.0, 12.5, 1.1
FIRE_R = 7.5
POT_R, POT_WALL, POT_FLOOR, POT_RIM = 6.2, 0.45, 3.4, 10.2
SPIT_Z, SPIT_HALF, CRANK_OFF, CRANK_R = 13.0, 13.0, 16.1, 3.25
OVEN_FLOOR = 9.0


# =================================================================== textures
def fbm(size, octaves=5, base=4, seed=None, aspect=(1, 1)):
    """Fractal value noise in 0..1, tiling."""
    g = np.random.default_rng(seed if seed is not None else rng.integers(1 << 30))
    h, w = size
    out = np.zeros((h, w))
    amp, tot = 1.0, 0.0
    for o in range(octaves):
        gh, gw = base * aspect[0] * (2 ** o), base * aspect[1] * (2 ** o)
        grid = g.random((gh, gw))
        img = Image.fromarray((grid * 255).astype(np.uint8), "L")
        # tile by wrapping: pad one cell then crop after resize
        big = np.pad(grid, ((0, 1), (0, 1)), mode="wrap")
        img = Image.fromarray((big * 65535).astype(np.uint16).astype(np.int32), "I")
        up = np.asarray(img.resize((w + w // gw, h + h // gh), Image.BICUBIC), dtype=np.float64)[:h, :w] / 65535.0
        out += up * amp
        tot += amp
        amp *= 0.5
    out /= tot
    return (out - out.min()) / (out.max() - out.min() + 1e-9)


def to_img(rgb):
    return Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8), "RGB")


def normal_from_height(hgt, strength, spec):
    dx = (np.roll(hgt, -1, 1) - np.roll(hgt, 1, 1)) * strength
    dy = (np.roll(hgt, -1, 0) - np.roll(hgt, 1, 0)) * strength
    n = np.stack([-dx, dy, np.ones_like(hgt)], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    rgb = (n * 0.5 + 0.5) * 255
    a = spec if isinstance(spec, np.ndarray) else np.full(hgt.shape, spec)
    return Image.fromarray(np.dstack([rgb, a]).clip(0, 255).astype(np.uint8), "RGBA")


def dds(path, img, fmt="DXT1"):
    """A block-compressed DDS (DXT1, or DXT5 with alpha) with mips down to 4 x 4 (as Skyrim Yacht writes them)."""
    img = img.convert("RGBA")
    levels = [img]
    while min(levels[-1].size) > 4:
        w, h = levels[-1].size
        levels.append(levels[-1].resize((w // 2, h // 2), Image.LANCZOS))
    body = b""
    for m in levels:
        buf = io.BytesIO()
        m.save(buf, format="DDS", pixel_format=fmt)
        body += buf.getvalue()[128:]
    w, h = img.size
    top = (w // 4) * (h // 4) * (8 if fmt == "DXT1" else 16)
    hdr = struct.pack("<4sIIIIIII11I", b"DDS ", 124, 0xA1007, h, w, top, 0, len(levels), *([0] * 11))
    hdr += struct.pack("<II4sIIIII", 32, 0x4, fmt.encode(), 0, 0, 0, 0, 0)
    hdr += struct.pack("<IIIII", 0x401008, 0, 0, 0, 0)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "wb").write(hdr + body)


def save_tex(name, rgb, height, strength, spec):
    out = os.path.join(ROOT, "textures", "Hearthcooking")
    dds(os.path.join(out, name + ".dds"), to_img(rgb), "DXT1")
    dds(os.path.join(out, name + "_n.dds"), normal_from_height(height, strength, spec), "DXT5")
    print("texture", name)


def colorize(t, stops):
    """t in 0..1 -> colour along the stops [(t, (r, g, b)), ...]"""
    out = np.zeros(t.shape + (3,))
    for k in range(len(stops) - 1):
        t0, c0 = stops[k]
        t1, c1 = stops[k + 1]
        m = (t >= t0) & (t <= t1)
        f = ((t - t0) / (t1 - t0 + 1e-9))[m][:, None]
        out[m] = np.array(c0) * (1 - f) + np.array(c1) * f
    return out


def tex_wood(name, size, base, dark, plank, seed, spec=40):
    """Planks running along u: grain streaks, a seam between planks every 'plank' of v."""
    h, w = size
    n1 = fbm(size, 6, 4, seed, (1, 8)) if False else fbm(size, 6, 3, seed)
    v = np.linspace(0, 1, h, endpoint=False)[:, None] * np.ones((1, w))
    u = np.linspace(0, 1, w, endpoint=False)[None, :] * np.ones((h, 1))
    streak = fbm((h, w // 8), 4, 6, seed + 1)
    streak = np.asarray(Image.fromarray((streak * 255).astype(np.uint8)).resize((w, h), Image.BICUBIC), dtype=np.float64) / 255.0
    grain = 0.5 + 0.5 * np.sin((v * 90 + streak * 9.0 + n1 * 2.0) * math.pi)
    t = np.clip(0.55 * grain + 0.45 * n1, 0, 1)
    rgb = np.array(dark) * (1 - t[..., None]) + np.array(base) * t[..., None]
    height = 0.35 * grain + 0.4 * n1
    if plank:
        pv = (v * plank) % 1.0
        seam = np.exp(-((np.minimum(pv, 1 - pv)) / 0.012) ** 2)
        rgb *= (1 - 0.6 * seam)[..., None]
        height -= seam * 0.8
        # each plank a shade of its own
        idx = np.floor(v * plank).astype(int)
        shade = 0.88 + 0.24 * np.random.default_rng(seed).random(plank + 1)[idx]
        rgb *= shade[..., None]
    save_tex(name, rgb, height, 3.0, spec)


def tex_stone(name, size, cells, base, mortar, seed, spec=25):
    """Irregular blocks (Voronoi) with mortar between."""
    h, w = size
    g = np.random.default_rng(seed)
    pts = g.random((cells, 2)) * [w, h]
    yy, xx = np.mgrid[0:h, 0:w]
    best = np.full((h, w), 1e9)
    second = np.full((h, w), 1e9)
    owner = np.zeros((h, w), int)
    for k, (px, py) in enumerate(pts):
        for ox in (-w, 0, w):
            for oy in (-h, 0, h):
                d = np.hypot(xx - px - ox, yy - py - oy)
                closer = d < best
                second = np.where(closer, best, np.minimum(second, d))
                owner = np.where(closer, k, owner)
                best = np.where(closer, d, best)
    edge = second - best
    joint = np.clip(edge / (w / cells ** 0.5 * 0.08), 0, 1)
    n = fbm(size, 6, 4, seed + 3)
    shade = 0.8 + 0.35 * g.random(cells)[owner]
    rgb = np.array(base) * (shade * (0.75 + 0.5 * n))[..., None]
    rgb = rgb * joint[..., None] + np.array(mortar) * (1 - joint[..., None])
    height = joint * 0.8 + n * 0.4
    save_tex(name, rgb, height, 4.0, spec)


def tex_noise(name, size, stops, seed, strength=2.0, spec=30, octaves=6, base=4):
    n = fbm(size, octaves, base, seed)
    save_tex(name, colorize(n, stops), n, strength, spec)
    return n


def textures():
    tex_wood("worktop", (1024, 1024), (128, 88, 54), (70, 45, 27), 7, 1)
    tex_wood("board", (1024, 1024), (196, 158, 108), (140, 104, 66), 0, 2, 50)
    tex_wood("handle", (256, 256), (92, 60, 36), (45, 28, 17), 0, 3, 60)
    tex_wood("planks", (512, 512), (110, 74, 44), (58, 37, 22), 4, 4, 35)
    # bark: deep furrows running along the log (u)
    h, w = 512, 1024
    n = fbm((h, w), 6, 4, 5)
    st = fbm((h, w // 16), 4, 8, 6)
    st = np.asarray(Image.fromarray((st * 255).astype(np.uint8)).resize((w, h), Image.BICUBIC), dtype=np.float64) / 255.0
    furrow = np.abs(np.sin((np.linspace(0, 1, h)[:, None] * 26 + st * 3) * math.pi)) ** 0.5
    t = np.clip(furrow * 0.6 + n * 0.4, 0, 1)
    save_tex("bark", colorize(t, [(0, (28, 20, 14)), (0.5, (70, 52, 36)), (1, (112, 92, 70))]), t, 4.0, 20)
    # the cut end of a log: rings
    yy, xx = np.mgrid[0:512, 0:512] / 512.0 - 0.5
    r = np.hypot(xx, yy)
    n = fbm((512, 512), 5, 4, 7)
    ring = 0.5 + 0.5 * np.sin((r * 60 + n * 3) * math.pi)
    t = np.clip(0.6 * ring + 0.4 * n, 0, 1) * (r < 0.48)
    rgb = colorize(t, [(0, (90, 64, 40)), (0.5, (170, 128, 84)), (1, (206, 166, 116))])
    rgb[r >= 0.48] = (50, 36, 24)
    save_tex("logend", rgb, t, 2.0, 20)
    tex_stone("hearth", (1024, 1024), 60, (128, 120, 108), (52, 46, 40), 8)
    tex_stone("wall", (1024, 1024), 26, (120, 108, 92), (40, 34, 28), 9)
    tex_stone("ovenstone", (1024, 1024), 90, (146, 112, 86), (60, 44, 34), 10)
    tex_noise("iron", (512, 512), [(0, (24, 23, 22)), (0.6, (52, 50, 48)), (0.85, (70, 64, 58)), (1, (96, 60, 40))], 11, 2.0, 120)
    tex_noise("ash", (512, 512), [(0, (18, 16, 15)), (0.5, (58, 54, 50)), (1, (120, 116, 110))], 12, 3.0, 10)
    tex_noise("soot", (256, 256), [(0, (8, 7, 7)), (1, (36, 30, 26))], 13, 2.0, 5)
    tex_noise("flesh", (256, 256), [(0, (196, 178, 150)), (1, (238, 228, 208))], 14, 1.0, 40)
    # linen: a weave
    yy, xx = np.mgrid[0:512, 0:512]
    weave = 0.5 + 0.25 * np.sin(xx * math.pi / 2.0) * np.sign(np.sin(yy * math.pi / 8.0)) + 0.25 * np.sin(yy * math.pi / 2.0)
    n = fbm((512, 512), 5, 4, 15)
    t = np.clip(weave * 0.4 + n * 0.6, 0, 1)
    save_tex("linen", colorize(t, [(0, (150, 136, 112)), (1, (222, 210, 186))]), weave * 0.5 + n * 0.5, 2.0, 15)
    # broth: pale and neutral (the recipe tints it), swirled, with beads of fat
    yy, xx = (np.mgrid[0:1024, 0:1024] / 1024.0) - 0.5
    r, a = np.hypot(xx, yy), np.arctan2(yy, xx)
    n = fbm((1024, 1024), 6, 4, 16)
    swirl = 0.5 + 0.5 * np.sin(a * 3 + r * 26 + n * 5)
    beads = np.zeros_like(r)
    g = np.random.default_rng(17)
    for _ in range(140):
        cx, cy, cr = g.random() - 0.5, g.random() - 0.5, 0.004 + g.random() * 0.016
        beads = np.maximum(beads, np.clip(1 - np.hypot(xx - cx, yy - cy) / cr, 0, 1))
    t = np.clip(0.55 * swirl + 0.45 * n, 0, 1)
    rgb = colorize(t, [(0, (176, 162, 140)), (1, (236, 226, 206))]) + beads[..., None] * 40
    save_tex("broth", rgb, t * 0.3 + beads, 2.0, np.clip(110 + beads * 140, 0, 255))
    tex_noise("salt", (256, 256), [(0, (196, 196, 192)), (0.7, (240, 240, 236)), (1, (255, 255, 255))], 18, 4.0, 160, 6, 16)
    # dough: cream with flour dust and little air holes
    n = fbm((512, 512), 6, 4, 19)
    specks = (np.random.default_rng(20).random((512, 512)) > 0.985).astype(float)
    holes = (fbm((512, 512), 3, 24, 21) < 0.12).astype(float)
    t = np.clip(n * 0.5 + 0.5, 0, 1)
    rgb = colorize(t, [(0, (210, 186, 146)), (1, (242, 228, 198))]) + specks[..., None] * 30 - holes[..., None] * 35
    save_tex("dough", rgb, n * 0.6 - holes * 0.5, 1.5, 35)


# =================================================================== geometry
def nb(c):
    return max(0, min(255, int(round((c + 1.0) * 127.5))))


def vert(p, uv, n, t, bt):
    return (struct.pack("<4f", p[0], p[1], p[2], bt[0]) + struct.pack("<e", uv[0]) + struct.pack("<e", uv[1]) +
            bytes([nb(n[0]), nb(n[1]), nb(n[2]), nb(bt[1])]) + bytes([nb(t[0]), nb(t[1]), nb(t[2]), nb(bt[2])]))


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def norm(v):
    l = math.sqrt(sum(c * c for c in v)) or 1.0
    return tuple(c / l for c in v)


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def mul(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


class Mesh:
    def __init__(self):
        self.V, self.I = [], []

    def v(self, p, uv, n, t):
        n = norm(n)
        t = norm(sub(t, mul(n, dot(t, n)))) if abs(dot(norm(t), n)) < 0.999 else norm(cross(n, (0, 0, 1) if abs(n[2]) < 0.9 else (1, 0, 0)))
        self.V.append((p, uv, n, t, cross(n, t)))
        return len(self.V) - 1

    def tri(self, a, b, c):
        self.I += [a, b, c]

    def quad(self, ps, uvs, n=None, t=None):
        """Four corners counter-clockwise seen from the front."""
        if n is None:
            n = norm(cross(sub(ps[1], ps[0]), sub(ps[3], ps[0])))
        if t is None:
            t = norm(sub(ps[1], ps[0]))
        k = [self.v(p, uv, n, t) for p, uv in zip(ps, uvs)]
        self.I += [k[0], k[1], k[2], k[0], k[2], k[3]]

    def box(self, x0, x1, y0, y1, z0, z1, uscale=None, faces="xXyYzZ"):
        """An axis-aligned box; uvs in units / uscale (or 0..1 per face)."""
        def uv(a, b, la, lb):
            return (a / uscale, b / uscale) if uscale else ((a - la[0]) / (la[1] - la[0]), (b - lb[0]) / (lb[1] - lb[0]))
        X, Y, Z = (x0, x1), (y0, y1), (z0, z1)
        if "Z" in faces:
            self.quad([(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)], [uv(x0, -y0, X, (-y1, -y0)), uv(x1, -y0, X, (-y1, -y0)), uv(x1, -y1, X, (-y1, -y0)), uv(x0, -y1, X, (-y1, -y0))])
        if "z" in faces:
            self.quad([(x0, y1, z0), (x1, y1, z0), (x1, y0, z0), (x0, y0, z0)], [uv(x0, y1, X, Y), uv(x1, y1, X, Y), uv(x1, y0, X, Y), uv(x0, y0, X, Y)])
        if "y" in faces:
            self.quad([(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)], [uv(x0, -z0, X, (-z1, -z0)), uv(x1, -z0, X, (-z1, -z0)), uv(x1, -z1, X, (-z1, -z0)), uv(x0, -z1, X, (-z1, -z0))])
        if "Y" in faces:
            self.quad([(x1, y1, z0), (x0, y1, z0), (x0, y1, z1), (x1, y1, z1)], [uv(-x1, -z0, (-x1, -x0), (-z1, -z0)), uv(-x0, -z0, (-x1, -x0), (-z1, -z0)), uv(-x0, -z1, (-x1, -x0), (-z1, -z0)), uv(-x1, -z1, (-x1, -x0), (-z1, -z0))])
        if "x" in faces:
            self.quad([(x0, y1, z0), (x0, y0, z0), (x0, y0, z1), (x0, y1, z1)], [uv(-y1, -z0, (-y1, -y0), (-z1, -z0)), uv(-y0, -z0, (-y1, -y0), (-z1, -z0)), uv(-y0, -z1, (-y1, -y0), (-z1, -z0)), uv(-y1, -z1, (-y1, -y0), (-z1, -z0))])
        if "X" in faces:
            self.quad([(x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1)], [uv(y0, -z0, Y, (-z1, -z0)), uv(y1, -z0, Y, (-z1, -z0)), uv(y1, -z1, Y, (-z1, -z0)), uv(y0, -z1, Y, (-z1, -z0))])

    def rounded_box(self, sx, sy, sz, r, n=6, center=(0, 0, 0), noise=0.0, seed=0):
        """A box with rounded edges (the dice's method), each face mapped 0..1."""
        g = np.random.default_rng(seed)
        hs = (sx / 2, sy / 2, sz / 2)
        e = tuple(max(1e-4, h - r) for h in hs)
        faces = [((1, 0, 0), (0, 1, 0), (0, 0, 1)), ((-1, 0, 0), (0, -1, 0), (0, 0, 1)), ((0, 1, 0), (-1, 0, 0), (0, 0, 1)),
                 ((0, -1, 0), (1, 0, 0), (0, 0, 1)), ((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (1, 0, 0), (0, -1, 0))]
        wob = {}
        for nrm, a, b in faces:
            base = len(self.V)
            for i in range(n + 1):
                for j in range(n + 1):
                    s, t = i / n * 2 - 1, j / n * 2 - 1
                    p = tuple(nrm[k] * hs[k] + a[k] * s * hs[k] + b[k] * t * hs[k] for k in range(3))
                    c = tuple(max(-e[k], min(e[k], p[k])) for k in range(3))
                    d = sub(p, c)
                    dl = math.sqrt(dot(d, d))
                    dn = mul(d, 1 / dl) if dl > 1e-9 else nrm
                    q = add(c, mul(dn, r))
                    if noise:
                        key = tuple(round(x, 4) for x in q)
                        if key not in wob:
                            wob[key] = 1 + noise * (g.random() - 0.5)
                        q = mul(q, wob[key])
                    self.v(add(q, center), ((s * 0.5 + 0.5), 1 - (t * 0.5 + 0.5)), dn, a)
            for i in range(n):
                for j in range(n):
                    p0 = base + i * (n + 1) + j
                    p1 = base + (i + 1) * (n + 1) + j
                    p2 = base + (i + 1) * (n + 1) + j + 1
                    p3 = base + i * (n + 1) + j + 1
                    self.I += [p0, p1, p2, p0, p2, p3]

    def lathe(self, prof, segs=48, center=(0, 0, 0), urep=1.0, vrep=1.0, axis="z", a0=0.0, a1=2 * math.pi, skip=None):
        """Sweeps a profile [(r, h), ...] round an axis. The profile runs so the surface faces out on its left."""
        L = [0.0]
        for k in range(1, len(prof)):
            L.append(L[-1] + math.hypot(prof[k][0] - prof[k - 1][0], prof[k][1] - prof[k - 1][1]))
        tot = L[-1] or 1.0
        rows = []
        for i in range(segs + 1):
            ang = a0 + (a1 - a0) * i / segs
            ca, sa = math.cos(ang), math.sin(ang)
            row = []
            for k, (r, h) in enumerate(prof):
                k0, k1 = max(0, k - 1), min(len(prof) - 1, k + 1)
                dr, dh = prof[k1][0] - prof[k0][0], prof[k1][1] - prof[k0][1]
                nr, nh = dh, -dr  # the profile's normal (to the right of travel in (r, h))
                if axis == "z":
                    p = (center[0] + r * ca, center[1] + r * sa, center[2] + h)
                    nn = (nr * ca, nr * sa, nh)
                    tt = (-sa, ca, 0)
                else:  # along x
                    p = (center[0] + h, center[1] + r * ca, center[2] + r * sa)
                    nn = (nh, nr * ca, nr * sa)
                    tt = (0, -sa, ca)
                if abs(nr) + abs(nh) < 1e-9:
                    nn = (0, 0, 1) if axis == "z" else (1, 0, 0)
                row.append(self.v(p, (i / segs * urep, L[k] / tot * vrep), nn, tt))
            rows.append(row)
        for i in range(segs):
            for k in range(len(prof) - 1):
                if skip and skip(a0 + (a1 - a0) * (i + 0.5) / segs, (prof[k][1] + prof[k + 1][1]) * 0.5):
                    continue
                a, b, c, d = rows[i][k], rows[i + 1][k], rows[i + 1][k + 1], rows[i][k + 1]
                self.I += [a, c, b, a, d, c]

    def cylinder(self, p0, p1, r, segs=16, caps=True, cap_uv_scale=1.0, urep=1.0, wobble=0.0, seed=0):
        """A cylinder between two points; u along it, v round it."""
        g = np.random.default_rng(seed)
        d = sub(p1, p0)
        L = math.sqrt(dot(d, d))
        ax = mul(d, 1 / L)
        a = norm(cross(ax, (0, 0, 1) if abs(ax[2]) < 0.9 else (1, 0, 0)))
        b = cross(ax, a)
        rad = [r * (1 + wobble * (g.random() - 0.5)) for _ in range(segs)]
        rad.append(rad[0])
        base = len(self.V)
        for i in range(segs + 1):
            th = 2 * math.pi * i / segs
            dirv = add(mul(a, math.cos(th)), mul(b, math.sin(th)))
            for k, p in enumerate((p0, p1)):
                self.v(add(p, mul(dirv, rad[i])), (k * urep, i / segs), dirv, ax)
        for i in range(segs):
            q0, q1, q2, q3 = base + 2 * i, base + 2 * i + 1, base + 2 * i + 3, base + 2 * i + 2
            self.I += [q0, q2, q1, q1, q2, q3]
        if caps:
            for k, (p, nn) in enumerate(((p0, mul(ax, -1)), (p1, ax))):
                c = self.v(p, (0.5, 0.5), nn, a)
                ring = []
                for i in range(segs + 1):
                    th = 2 * math.pi * i / segs
                    dirv = add(mul(a, math.cos(th)), mul(b, math.sin(th)))
                    ring.append(self.v(add(p, mul(dirv, rad[i])), (0.5 + 0.5 * math.cos(th) * cap_uv_scale, 0.5 + 0.5 * math.sin(th) * cap_uv_scale), nn, a))
                for i in range(segs):
                    if k == 0:
                        self.I += [c, ring[i + 1], ring[i]]
                    else:
                        self.I += [c, ring[i], ring[i + 1]]

    def ellipsoid(self, c, rx, ry, rz, segs=20, rings=12, noise=0.0, seed=0, flatten_bottom=None):
        g = np.random.default_rng(seed)
        base = len(self.V)
        wob = g.random((rings + 1, segs + 1)) if noise else None
        for i in range(rings + 1):
            ph = math.pi * i / rings - math.pi / 2
            for j in range(segs + 1):
                th = 2 * math.pi * j / segs
                n = (math.cos(ph) * math.cos(th), math.cos(ph) * math.sin(th), math.sin(ph))
                k = 1 + (noise * (wob[i][j % segs] - 0.5) if noise else 0)
                z = rz * n[2] * k
                if flatten_bottom is not None and z < -flatten_bottom:
                    z = -flatten_bottom
                p = (c[0] + rx * n[0] * k, c[1] + ry * n[1] * k, c[2] + z)
                self.v(p, (j / segs, i / rings), (n[0] / rx, n[1] / ry, n[2] / rz), (-math.sin(th), math.cos(th), 0))
        for i in range(rings):
            for j in range(segs):
                p0 = base + i * (segs + 1) + j
                p1 = p0 + 1
                p2 = p0 + segs + 2
                p3 = p0 + segs + 1
                self.I += [p0, p1, p2, p0, p2, p3]

    def bounds(self):
        ps = [v[0] for v in self.V]
        c = tuple((min(p[k] for p in ps) + max(p[k] for p in ps)) / 2 for k in range(3))
        r = max(math.sqrt(sum((p[k] - c[k]) ** 2 for k in range(3))) for p in ps)
        return c, r

    def data(self):
        return b"".join(vert(*v) for v in self.V), self.I


# =================================================================== NIF
def shader_block(template, texset_ref, spec, gloss):
    b = bytearray(template)
    struct.pack_into("<I", b, 0, 0)
    sf1, sf2 = struct.unpack_from("<II", b, 16)
    struct.pack_into("<II", b, 16, sf1 & ~0x80, sf2 & ~0x8000)
    struct.pack_into("<i", b, 40, texset_ref)
    struct.pack_into("<f", b, 72, float(gloss))
    struct.pack_into("<3f", b, 76, 1.0, 1.0, 1.0)
    struct.pack_into("<f", b, 88, float(spec))
    return bytes(b[:100])


def texset_block(paths):
    paths = list(paths) + [""] * (9 - len(paths))
    return struct.pack("<I", 9) + b"".join(struct.pack("<I", len(p)) + p.encode() for p in paths)


def write_nif(out, root_name, shapes):
    """shapes: [(name, Mesh, texture name, (spec, gloss))]"""
    n = Nif(open(os.path.join(HERE, "template.nif"), "rb").read())
    assert [n.type(i) for i in range(len(n.blocks))][:3] == ["BSFadeNode", "BSInvMarker", "BSXFlags"], "unexpected template"
    shape_t, shader_t = n.blocks[6], n.blocks[7]
    vd, = struct.unpack_from("<Q", shape_t, 100)
    assert (vd & 0xF) * 4 == 28, "the template's vertices must be 28 bytes (position, uv, normal, tangent)"
    n.shorts = [b"Hearthcooking\x00", b"\x00", b"make_meshes.py\x00"]
    n.strings = [root_name, "INV", "BSX"] + [s[0] for s in shapes]
    n.types = ["BSFadeNode", "BSInvMarker", "BSXFlags", "BSTriShape", "BSLightingShaderProperty", "BSShaderTextureSet"]
    blocks, tidx = [], []
    child_refs = [3 + 3 * i for i in range(len(shapes))]
    root = (struct.pack("<iI2i", 0, 2, 1, 2) + struct.pack("<iI", -1, 0x0E) + struct.pack("<3f", 0, 0, 0) +
            struct.pack("<9f", 1, 0, 0, 0, 1, 0, 0, 0, 1) + struct.pack("<f", 1.0) + struct.pack("<i", -1) +
            struct.pack("<I", len(shapes)) + b"".join(struct.pack("<i", r) for r in child_refs) + struct.pack("<I", 0))
    blocks += [root, struct.pack("<i3Hf", 1, 0, 0, 0, 1.0), struct.pack("<iI", 2, 0)]
    tidx += [0, 1, 2]
    for k, (name, mesh, tex, (spec, gloss)) in enumerate(shapes):
        me = 3 + 3 * k
        head = bytearray(shape_t[:108])
        struct.pack_into("<i", head, 0, 3 + k)
        struct.pack_into("<I", head, 12, 0x0E)
        struct.pack_into("<3f9ff", head, 16, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1.0)
        struct.pack_into("<i", head, 68, -1)
        c, r = mesh.bounds()
        struct.pack_into("<4f", head, 72, c[0], c[1], c[2], r)
        struct.pack_into("<iii", head, 88, -1, me + 1, -1)
        verts, idx = mesh.data()
        assert len(mesh.V) < 65536, f"{name}: too many vertices ({len(mesh.V)})"
        data = verts + struct.pack(f"<{len(idx)}H", *idx)
        shape = bytes(head) + struct.pack("<HHI", len(idx) // 3, len(mesh.V), len(data)) + data + struct.pack("<I", 0)
        blocks += [shape, shader_block(shader_t, me + 2, spec, gloss), texset_block([TEX + tex + ".dds", TEX + tex + "_n.dds"])]
        tidx += [3, 4, 5]
    n.blocks = [bytearray(b) for b in blocks]
    n.tidx = tidx
    n.groups = []
    n.footer = struct.pack("<II", 1, 0)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    open(out, "wb").write(n.write())
    print("wrote", os.path.relpath(out, ROOT), sum(len(s[1].V) for s in shapes), "vertices")


WOOD, STONE, IRON, MATTE = (0.3, 20), (0.15, 12), (1.0, 50), (0.1, 8)


def nif(name, shapes):
    write_nif(os.path.join(ROOT, "meshes", "Hearthcooking", name + ".nif"), "Hearthcooking" + name, shapes)


# =================================================================== the kitchen
def kitchen():
    # the worktop (wood, left) and the hearth slab (stone, right); no wall behind (the game's room shows through)
    top, slab = Mesh(), Mesh()
    x0, x1, y0, y1 = -TOP_W / 2, TOP_W / 2, -TOP_D / 2, TOP_D / 2
    top.box(x0, HEARTH_EDGE, y0, y1, -2.5, 0, uscale=40.0, faces="Zyx")
    slab.box(HEARTH_EDGE, x1, y0, y1, -2.5, 0, uscale=24.0, faces="ZyX")
    # the worktop on its own, so a table of the game's can stand in for it
    nif("Worktop", [("Worktop", top, "worktop", WOOD)])
    nif("Room", [("Hearth", slab, "hearth", STONE)])

    # the linen the ingredients wait on
    cloth = Mesh()
    nx, ny = 16, 36
    cx0, cx1, cy0, cy1 = -36.0, -22.0, -18.0, 18.0
    base = len(cloth.V)
    for i in range(nx + 1):
        for j in range(ny + 1):
            x = cx0 + (cx1 - cx0) * i / nx
            y = cy0 + (cy1 - cy0) * j / ny
            z = 0.06 + 0.12 * (1 + math.sin(y * 0.7 + math.sin(x * 0.3) * 1.5)) * 0.5
            cloth.v((x, y, z), (i / nx * 1.5, j / ny * 3.5), (-0.06 * math.cos(x * 0.3), -0.08 * math.cos(y * 0.7), 1), (1, 0, 0))
    for i in range(nx):
        for j in range(ny):
            a = base + i * (ny + 1) + j
            cloth.I += [a, a + ny + 1, a + ny + 2, a, a + ny + 2, a + 1]
    nif("Cloth", [("Linen", cloth, "linen", MATTE)])

    # the cutting board, its middle at the origin
    board = Mesh()
    board.rounded_box(BOARD_W, BOARD_D, BOARD_H, 0.35, 8)
    nif("Board", [("Board", board, "board", WOOD)])

    # the knife: the edge along +y from the origin, the handle toward -y
    blade, handle, bolster = Mesh(), Mesh(), Mesh()
    L, Hb = 7.6, 1.7
    prof = [(0.0, 0.0), (L * 0.75, 0.0), (L, Hb * 0.55), (L, Hb), (0.0, Hb)]
    for side in (-1, 1):
        ps = [(side * (0.02 if (y < 0.01 and z < 0.01) else 0.07), y, z) for y, z in prof]
        n = (side, 0, 0)
        for k in range(1, len(prof) - 1):
            a, b, c = ps[0], ps[k], ps[k + 1]
            ia = blade.v(a, (a[1] / L, 1 - a[2] / Hb), n, (0, 1, 0))
            ib = blade.v(b, (b[1] / L, 1 - b[2] / Hb), n, (0, 1, 0))
            ic = blade.v(c, (c[1] / L, 1 - c[2] / Hb), n, (0, 1, 0))
            blade.I += [ia, ib, ic] if side < 0 else [ia, ic, ib]
    blade.box(-0.07, 0.07, 0.0, L, Hb - 0.05, Hb, faces="Z")
    handle.rounded_box(0.7, 4.2, 1.0, 0.3, 4, center=(0, -2.3, 1.05))
    bolster.box(-0.18, 0.18, -0.4, 0.0, 0.5, 1.6)
    nif("Knife", [("Blade", blade, "iron", IRON), ("Handle", handle, "handle", WOOD), ("Bolster", bolster, "iron", IRON)])

    # the pot (its axis at the origin, heights as in the kitchen), its legs and lugs
    pot = Mesh()
    f0 = POT_FLOOR
    prof = [(0.0, f0 - 0.6), (4.6, f0 - 0.6), (6.4, f0 + 0.6), (7.1, f0 + 2.8), (7.15, 6.8), (6.85, 9.3), (POT_R + POT_WALL, POT_RIM - 0.15),
            (POT_R + POT_WALL, POT_RIM), (POT_R, POT_RIM), (POT_R, 6.0), (5.8, f0 + 1.0), (4.6, f0), (0.0, f0)]
    pot.lathe(prof, 56, urep=3.0, vrep=2.0)
    for k in range(3):
        a = k * 2 * math.pi / 3 + 0.5
        pot.cylinder((4.4 * math.cos(a), 4.4 * math.sin(a), 0.0), (4.8 * math.cos(a), 4.8 * math.sin(a), f0 - 0.4), 0.35, 8)
    for s in (-1, 1):
        pot.box(s * (POT_R + POT_WALL) - 0.5, s * (POT_R + POT_WALL) + 0.5, -0.5, 0.5, POT_RIM - 1.6, POT_RIM - 0.6)
    nif("Pot", [("Pot", pot, "iron", IRON)])

    # the broth's surface: a disk (the DLL tints it for the dish and turns it as it's stirred)
    broth = Mesh()
    c = broth.v((0, 0, 0), (0.5, 0.5), (0, 0, 1), (1, 0, 0))
    ring = []
    for i in range(65):
        a = 2 * math.pi * i / 64
        ring.append(broth.v((POT_R * 0.985 * math.cos(a), POT_R * 0.985 * math.sin(a), 0), (0.5 + 0.5 * math.cos(a), 0.5 - 0.5 * math.sin(a)), (0, 0, 1), (1, 0, 0)))
    for i in range(64):
        broth.I += [c, ring[i], ring[i + 1]]
    nif("Broth", [("Broth", broth, "broth", (0.9, 60))])

    # the ladle: its bowl at the origin, the handle up and toward you
    bowl, stick = Mesh(), Mesh()
    bowl.lathe([(0.0, -1.2), (0.8, -1.05), (1.25, -0.5), (1.35, 0.0), (1.25, 0.0), (1.15, -0.45), (0.7, -0.95), (0.0, -1.08)], 20)
    d = norm((-0.25, -0.45, 1.0))
    stick.cylinder((0.0, -1.2, 0.1), (d[0] * 12, -1.2 + d[1] * 12, 0.1 + d[2] * 12), 0.22, 10)
    nif("Ladle", [("Bowl", bowl, "iron", IRON), ("Handle", stick, "handle", WOOD)])

    # a log: along x, its middle at the origin
    bark, ends = Mesh(), Mesh()
    bark.cylinder((-4.2, 0, 0), (4.2, 0, 0), 0.95, 14, caps=False, urep=1.0, wobble=0.18, seed=3)
    for x, nn in ((-4.2, (-1, 0, 0)), (4.2, (1, 0, 0))):
        c = ends.v((x, 0, 0), (0.5, 0.5), nn, (0, 1, 0))
        ring = []
        g = np.random.default_rng(3)
        rad = [0.95 * (1 + 0.18 * (g.random() - 0.5)) for _ in range(14)]
        rad.append(rad[0])
        for i in range(15):
            a = 2 * math.pi * i / 14
            ring.append(ends.v((x, rad[i] * math.cos(a), rad[i] * math.sin(a)), (0.5 + 0.48 * math.cos(a), 0.5 + 0.48 * math.sin(a)), nn, (0, 1, 0)))
        for i in range(14):
            ends.I += [c, ring[i + 1], ring[i]] if x < 0 else [c, ring[i], ring[i + 1]]
    nif("Log", [("Bark", bark, "bark", MATTE), ("Ends", ends, "logend", MATTE)])

    # the fire pit: a ring of stones and a bed of ash, its middle at the origin
    stones, ash = Mesh(), Mesh()
    g = np.random.default_rng(5)
    for k in range(13):
        a = 2 * math.pi * k / 13 + g.random() * 0.15
        r = FIRE_R + 0.6
        stones.ellipsoid((r * math.cos(a), r * math.sin(a), 0.6), 1.5 + g.random() * 0.4, 1.3 + g.random() * 0.4, 1.0 + g.random() * 0.4, 12, 8,
                         noise=0.35, seed=int(g.integers(1 << 20)), flatten_bottom=0.6)
    c = ash.v((0, 0, 0.12), (0.5, 0.5), (0, 0, 1), (1, 0, 0))
    ring = []
    for i in range(41):
        a = 2 * math.pi * i / 40
        ring.append(ash.v((FIRE_R * math.cos(a), FIRE_R * math.sin(a), 0.02), (0.5 + 0.5 * math.cos(a), 0.5 + 0.5 * math.sin(a)), (0, 0, 1), (1, 0, 0)))
    for i in range(40):
        ash.I += [c, ring[i], ring[i + 1]]
    nif("FirePit", [("Stones", stones, "hearth", STONE), ("Ash", ash, "ash", MATTE)])

    # the spit's posts (forked at the top), the rod with its crank (turns about x, its axis at the origin)
    posts = Mesh()
    for s in (-1, 1):
        x = s * SPIT_HALF
        posts.cylinder((x, 0, 0), (x, 0, SPIT_Z - 0.4), 0.45, 10, seed=7)
        posts.cylinder((x, 0, SPIT_Z - 0.6), (x, -0.9, SPIT_Z + 1.0), 0.28, 8)
        posts.cylinder((x, 0, SPIT_Z - 0.6), (x, 0.9, SPIT_Z + 1.0), 0.28, 8)
    nif("SpitPosts", [("Posts", posts, "handle", WOOD)])
    rod, grip = Mesh(), Mesh()
    rod.cylinder((-SPIT_HALF - 1.2, 0, 0), (CRANK_OFF, 0, 0), 0.24, 10, urep=(CRANK_OFF + SPIT_HALF + 1.2) / (2 * math.pi * 0.24))
    rod.cylinder((CRANK_OFF, 0, 0), (CRANK_OFF, 0, CRANK_R), 0.22, 8)
    grip.cylinder((CRANK_OFF, 0, CRANK_R), (CRANK_OFF + 1.9, 0, CRANK_R), 0.32, 10)
    nif("SpitRod", [("Rod", rod, "iron", IRON), ("Grip", grip, "handle", WOOD)])
    # with the game's spit frame, which has its own skewer: only the crank end, joined to it inside the frame
    crod, cgrip = Mesh(), Mesh()
    crod.cylinder((SPIT_HALF - 4.0, 0, 0), (CRANK_OFF, 0, 0), 0.24, 10, urep=(CRANK_OFF - SPIT_HALF + 4.0) / (2 * math.pi * 0.24))  # (from inside the frame, through its post)
    crod.cylinder((CRANK_OFF, 0, 0), (CRANK_OFF, 0, CRANK_R), 0.22, 8)
    cgrip.cylinder((CRANK_OFF, 0, CRANK_R), (CRANK_OFF + 1.9, 0, CRANK_R), 0.32, 10)
    nif("SpitCrank", [("Rod", crod, "iron", IRON), ("Grip", cgrip, "handle", WOOD)])

    # the oven: a stone base with the firebox in its front, the dome with its mouth, the door, the peel
    base = Mesh()
    bx0, bx1, by0, by1, bz = -12.5, 12.5, -12.0, 11.0, OVEN_FLOOR
    hx0, hx1, hz0, hz1, depth = -5.0, 5.0, 0.6, 5.8, 8.0
    base.box(bx0, bx1, by0, by1, 0, bz, uscale=10.0, faces="xXYZ")
    # the front, round the firebox opening
    u = 10.0
    def fq(a, b, c, d):
        base.quad([(a, by0, c), (b, by0, c), (b, by0, d), (a, by0, d)], [(a / u, -c / u), (b / u, -c / u), (b / u, -d / u), (a / u, -d / u)], (0, -1, 0), (1, 0, 0))
    fq(bx0, hx0, 0, bz)
    fq(hx1, bx1, 0, bz)
    fq(hx0, hx1, hz1, bz)
    fq(hx0, hx1, 0, hz0)
    soot = Mesh()
    soot.box(hx0, hx1, by0, by0 + depth, hz0, hz1, uscale=6.0, faces="xXzZY")
    # flip the box's faces to look inward
    soot.I = [i for k in range(0, len(soot.I), 3) for i in (soot.I[k], soot.I[k + 2], soot.I[k + 1])]
    dome = Mesh()
    R, Hd, cy = 10.5, 9.0, 0.0
    mouth_half, mouth_h = math.radians(29.0), 6.6
    prof_out = [(R * math.cos(math.radians(a)), Hd * math.sin(math.radians(a))) for a in range(0, 91, 3)]
    prof_in = [((R - 1.2) * math.cos(math.radians(a)), (Hd - 1.1) * math.sin(math.radians(a))) for a in range(90, -1, -3)]

    hw = (R - 0.6) * math.sin(mouth_half)
    spring = mouth_h - hw * 0.55

    def mouth(rr, hh):
        # skips the cells of a shell (radius rr, height hh) that fall inside the mouth's arch
        def skip(ang, h):
            da = (ang - (-math.pi / 2) + math.pi) % (2 * math.pi) - math.pi
            if math.cos(da) <= 0 or h >= mouth_h:
                return False
            r = rr * math.sqrt(max(0.0, 1 - (h / hh) ** 2))
            half = hw if h <= spring else hw * math.sqrt(max(0.0, 1 - ((h - spring) / (hw * 0.55)) ** 2))
            return abs(r * math.sin(da)) < half + 0.25
        return skip
    dome.lathe(prof_out, 96, center=(0, cy, bz), urep=6.0, vrep=1.5, skip=mouth(R, Hd))
    dome.lathe(prof_in, 96, center=(0, cy, bz), urep=6.0, vrep=1.5, skip=mouth(R - 1.2, Hd - 1.1))
    # the mouth's arch: a lip joining the inner and outer shells
    lip = Mesh()
    arch = []
    for k in range(0, 13):  # the arch's outline, bottom left -> over the top -> bottom right
        a = math.pi - math.pi * k / 12
        arch.append((hw * math.cos(a), mouth_h - hw * 0.55 + hw * 0.55 * math.sin(a) if k not in (0, 12) else 0.0))
    arch[0] = (-hw, 0.0)
    arch[-1] = (hw, 0.0)
    yo, yi = -R * math.cos(mouth_half) - 0.4, -(R - 1.4) * math.cos(mouth_half)
    for k in range(len(arch) - 1):
        (xa, za), (xb, zb) = arch[k], arch[k + 1]
        ps = [(xa, yo, bz + za), (xa, yi, bz + za), (xb, yi, bz + zb), (xb, yo, bz + zb)]
        lip.quad(ps, [(0, 0), (0, 0.3), (0.3, 0.3), (0.3, 0)])
    # a stone frame round the mouth's front
    for k in range(len(arch) - 1):
        (xa, za), (xb, zb) = arch[k], arch[k + 1]
        oa = (xa * 1.25, za * 1.12 if za > 0 else 0)
        ob = (xb * 1.25, zb * 1.12 if zb > 0 else 0)
        lip.quad([(xa, yo, bz + za), (xb, yo, bz + zb), (ob[0], yo, bz + ob[1]), (oa[0], yo, bz + oa[1])], [(0, 0), (0.3, 0), (0.3, 0.2), (0, 0.2)], (0, -1, 0), (1, 0, 0))
    nif("OvenBase", [("Base", base, "ovenstone", STONE), ("Firebox", soot, "soot", MATTE)])
    nif("OvenDome", [("Dome", dome, "ovenstone", STONE), ("Mouth", lip, "ovenstone", STONE)])

    # the door: hinged at its left edge (the origin), the arch's shape, planks with iron straps and a ring
    door, straps = Mesh(), Mesh()
    w, t = hw * 2 + 0.8, 0.6
    outline = [(0.0, 0.0), (w, 0.0)] + [(w * 0.5 + (w * 0.5) * math.cos(math.pi * k / 10), mouth_h + 0.3 - hw * 0.55 + (hw * 0.55 + 0.3) * math.sin(math.pi * k / 10)) for k in range(0, 11)]
    for side, yy in ((-1, -t / 2), (1, t / 2)):
        cidx = door.v((w / 2, yy, mouth_h * 0.45), (0.5, 0.5), (0, side, 0), (1, 0, 0))
        ring = [door.v((x, yy, z), (x / w, 1 - z / (mouth_h + 0.5)), (0, side, 0), (1, 0, 0)) for x, z in outline]
        ring.append(ring[0])
        for k in range(len(ring) - 1):
            door.I += [cidx, ring[k + 1], ring[k]] if side < 0 else [cidx, ring[k], ring[k + 1]]
    for k in range(len(outline)):
        (xa, za), (xb, zb) = outline[k], outline[(k + 1) % len(outline)]
        door.quad([(xa, -t / 2, za), (xb, -t / 2, zb), (xb, t / 2, zb), (xa, t / 2, za)], [(0, 0), (0.1, 0), (0.1, 0.05), (0, 0.05)])
    for z in (1.2, mouth_h - 1.6):
        straps.box(0.2, w - 0.2, -t / 2 - 0.12, -t / 2, z, z + 0.7)
    straps.cylinder((w - 1.4, -t / 2 - 0.5, mouth_h * 0.45 - 0.6), (w - 1.4, -t / 2 - 0.5, mouth_h * 0.45 + 0.6), 0.12, 8)
    nif("OvenDoor", [("Planks", door, "planks", WOOD), ("Straps", straps, "iron", IRON)])

    # the door for the game's oven: its mouth is a round arch, so a round-topped door (short straight sides,
    # a half circle over them), the same width and height as ours; it stands in front of the arch
    door, straps = Mesh(), Mesh()
    rr = w * 0.5
    side_h = mouth_h + 0.3 - rr
    outline = [(0.0, 0.0), (w, 0.0)] + [(rr + rr * math.cos(math.pi * k / 24), side_h + rr * math.sin(math.pi * k / 24)) for k in range(0, 25)]
    top = side_h + rr
    for side, yy in ((-1, -t / 2), (1, t / 2)):
        cidx = door.v((w / 2, yy, top * 0.45), (0.5, 0.5), (0, side, 0), (1, 0, 0))
        ring = [door.v((x, yy, z), (x / w, 1 - z / (top + 0.2)), (0, side, 0), (1, 0, 0)) for x, z in outline]
        ring.append(ring[0])
        for k in range(len(ring) - 1):
            door.I += [cidx, ring[k + 1], ring[k]] if side < 0 else [cidx, ring[k], ring[k + 1]]
    for k in range(len(outline)):
        (xa, za), (xb, zb) = outline[k], outline[(k + 1) % len(outline)]
        door.quad([(xa, -t / 2, za), (xb, -t / 2, zb), (xb, t / 2, zb), (xa, t / 2, za)], [(0, 0), (0.1, 0), (0.1, 0.05), (0, 0.05)])
    for z in (1.0, top - 2.6):
        zz = z + 0.7
        half = rr if zz <= side_h else math.sqrt(max(rr * rr - (zz - side_h) ** 2, 0.0))
        straps.box(rr - half + 0.25, rr + half - 0.25, -t / 2 - 0.12, -t / 2, z, z + 0.7)
    straps.cylinder((w - 1.6, -t / 2 - 0.5, top * 0.4 - 0.6), (w - 1.6, -t / 2 - 0.5, top * 0.4 + 0.6), 0.12, 8)
    nif("OvenDoorArch", [("Planks", door, "planks", WOOD), ("Straps", straps, "iron", IRON)])

    # the peel: one board of wood, a pizza paddle - a wide blade with rounded front corners, thin at its front edge,
    # its shoulders sweeping in to a long flat handle with a rounded end (the blade's middle at the origin, the handle
    # along -y). Built as cross-sections along its length: each a half-width and a thickness
    def peel_w(y):
        bw, r, front, shoulder0, shoulder1, hw, end = 3.5, 1.3, 3.5, -2.2, -5.0, 0.72, -24.0
        if y > front - r:
            return bw - r + math.sqrt(max(r * r - (y - (front - r)) ** 2, 0.0))
        if y >= shoulder0:
            return bw
        if y >= shoulder1:
            k = (shoulder0 - y) / (shoulder0 - shoulder1)
            k = k * k * (3 - 2 * k)
            return bw + (hw - bw) * k
        if y >= end:
            return hw
        return math.sqrt(max(hw * hw - (y - end) ** 2, 0.0))

    def peel_t(y):
        if y > 1.0:
            return 0.14 + 0.1 * (3.5 - y) / 2.5  # bevelled to a thin front edge, to slide under the dough
        if y > -2.2:
            return 0.24
        if y > -5.0:
            return 0.24 + 0.22 * (-2.2 - y) / 2.8
        return 0.46

    peel = Mesh()
    ys = [3.5 - 0.1 * i for i in range(14)] + [2.1 - 0.25 * i for i in range(30)] + [-5.5 - 0.5 * i for i in range(38)] + [-24.0 - 0.72 * math.sin(math.pi * 0.5 * i / 8) for i in range(1, 9)]
    U = 9.0  # (texture units)
    for i in range(len(ys) - 1):
        ya, yb = ys[i], ys[i + 1]
        wa, wb = peel_w(ya), peel_w(yb)
        ta, tb = peel_t(ya) / 2, peel_t(yb) / 2
        # top and bottom
        peel.quad([(-wb, yb, tb), (wb, yb, tb), (wa, ya, ta), (-wa, ya, ta)], [(-wb / U + 0.5, -yb / U), (wb / U + 0.5, -yb / U), (wa / U + 0.5, -ya / U), (-wa / U + 0.5, -ya / U)], (0, 0, 1), (1, 0, 0))
        peel.quad([(-wa, ya, -ta), (wa, ya, -ta), (wb, yb, -tb), (-wb, yb, -tb)], [(-wa / U + 0.5, -ya / U), (wa / U + 0.5, -ya / U), (wb / U + 0.5, -yb / U), (-wb / U + 0.5, -yb / U)], (0, 0, -1), (1, 0, 0))
        # the two edges, facing out
        for sx in (-1, 1):
            nx, ny = (ya - yb), sx * (wa - wb)
            nl = math.hypot(nx, ny) or 1.0
            n = (sx * abs(nx) / nl, ny / nl if sx > 0 else -ny / nl, 0.0)
            n = (sx * abs(nx) / nl, (wa - wb) / nl, 0.0)
            pa0, pa1, pb0, pb1 = (sx * wa, ya, -ta), (sx * wa, ya, ta), (sx * wb, yb, -tb), (sx * wb, yb, tb)
            uvs = [(-ya / U, 0.0), (-ya / U, 0.03), (-yb / U, 0.03), (-yb / U, 0.0)]
            if sx > 0:
                peel.quad([pa0, pb0, pb1, pa1], [uvs[0], uvs[3], uvs[2], uvs[1]], n, (0, -1, 0))
            else:
                peel.quad([pb0, pa0, pa1, pb1], [uvs[3], uvs[0], uvs[1], uvs[2]], n, (0, -1, 0))
    # its thin front edge
    w0, t0 = peel_w(3.5), peel_t(3.5) / 2
    peel.quad([(w0, 3.5, -t0), (-w0, 3.5, -t0), (-w0, 3.5, t0), (w0, 3.5, t0)], [(0, 0), (0.6, 0), (0.6, 0.02), (0, 0.02)], (0, 1, 0), (-1, 0, 0))
    nif("Peel", [("Paddle", peel, "board", WOOD)])

    # the bowl and plate the dish is served on, the salt
    sbowl = Mesh()
    sbowl.lathe([(0.0, 0.0), (1.9, 0.0), (3.2, 1.0), (3.5, 2.2), (3.2, 2.2), (2.9, 1.2), (1.8, 0.45), (0.0, 0.4)], 40, urep=2.0)
    nif("Bowl", [("Bowl", sbowl, "board", WOOD)])
    plate = Mesh()
    plate.lathe([(0.0, 0.0), (3.6, 0.0), (4.6, 0.45), (4.7, 0.7), (4.4, 0.7), (3.4, 0.35), (0.0, 0.3)], 48, urep=2.0)
    nif("Plate", [("Plate", plate, "board", WOOD)])
    salt, heap = Mesh(), Mesh()
    salt.lathe([(0.0, 0.0), (1.2, 0.0), (2.0, 0.7), (2.2, 1.5), (2.0, 1.5), (1.8, 0.85), (1.1, 0.35), (0.0, 0.3)], 32)
    heap.ellipsoid((0, 0, 1.0), 1.75, 1.75, 0.75, 20, 10, noise=0.2, seed=9, flatten_bottom=0.0)
    nif("SaltBowl", [("Bowl", salt, "board", WOOD), ("Salt", heap, "salt", (0.5, 30))])

    # the pieces: a unit rounded box (the DLL gives it the ingredient's own skin), a cap for a cut end, dough
    chunk = Mesh()
    chunk.rounded_box(1.0, 1.0, 1.0, 0.2, 4)
    nif("Chunk", [("Chunk", chunk, "flesh", (0.4, 30))])
    # a cut piece's solid: a plain unit box drawn round it; the shader finds the piece inside it exactly
    hull = Mesh()
    hull.box(-0.5, 0.5, -0.5, 0.5, -0.5, 0.5, uscale=1.0)
    nif("Hull", [("Hull", hull, "flesh", (0.4, 30))])
    cap = Mesh()
    c = cap.v((0, 0, 0), (0.5, 0.5), (1, 0, 0), (0, 1, 0))
    ring = [cap.v((0, 0.5 * math.cos(2 * math.pi * i / 24), 0.5 * math.sin(2 * math.pi * i / 24)), (0.5 + 0.5 * math.cos(2 * math.pi * i / 24), 0.5 + 0.5 * math.sin(2 * math.pi * i / 24)), (1, 0, 0), (0, 1, 0)) for i in range(25)]
    for i in range(24):
        cap.I += [c, ring[i], ring[i + 1]]
    nif("Cap", [("Cap", cap, "flesh", (0.4, 30))])
    dough = Mesh()
    dough.ellipsoid((0, 0, 0), 1.0, 1.0, 1.0, 28, 16, noise=0.04, seed=12)
    nif("Dough", [("Dough", dough, "dough", (0.3, 25))])
    disk = Mesh()
    disk.lathe([(0.0, -0.1), (0.93, -0.1), (1.0, 0.0), (0.93, 0.1), (0.0, 0.1)], 48, urep=1.0)
    nif("DoughDisk", [("Dough", disk, "dough", (0.3, 25))])


if __name__ == "__main__":
    if "--meshes-only" not in sys.argv:
        textures()
    kitchen()
