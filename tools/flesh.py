"""The insides of things, for their cut faces: one atlas each (2048 x 1024), the left half a section across the thing
(its middle at the half's centre, its skin at the half's edge), the right half a section along it (its length along
u). Painted from noise, to look like the real thing close up: meat's grain and marbling, a carrot's core and rays, an
apple's star and seeds, a tomato's walls, jelly and seeds, a cabbage's packed leaves.

    python tools/flesh.py            (writes assets/textures/Hearthcooking/flesh_*.dds)
"""
import math
import os
import sys

import numpy as np
from scipy import ndimage

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_meshes as mm  # noqa: E402  (its dds writer and paths)

S = 1024  # each half


# ------------------------------------------------------------------ noise
def noise_img(seed, res, octaves=6, persistence=0.5):
    """A tiling fractal noise image (res x res), 0..1."""
    g = np.random.default_rng(seed)
    out = np.zeros((res, res))
    amp, tot = 1.0, 0.0
    cells = 4
    for _ in range(octaves):
        grid = g.random((cells, cells))
        out += ndimage.zoom(grid, res / cells, order=3, mode="grid-wrap", grid_mode=True)[:res, :res] * amp  # (tiles)
        tot += amp
        amp *= persistence
        cells *= 2
    out /= tot
    return (out - out.min()) / (out.max() - out.min() + 1e-9)


class Field:
    """Noise you can sample anywhere (wrapping), for warped and stretched patterns."""

    def __init__(self, seed, res=512, octaves=6, persistence=0.5):
        self.img = noise_img(seed, res, octaves, persistence)
        self.res = res

    def __call__(self, u, v):
        return ndimage.map_coordinates(self.img, [np.mod(v, 1.0) * self.res, np.mod(u, 1.0) * self.res], order=1, mode="grid-wrap")


def grid():
    yy, xx = np.mgrid[0:S, 0:S]
    X = (xx + 0.5) / S * 2 - 1
    Y = (yy + 0.5) / S * 2 - 1
    return X, Y, np.sqrt(X * X + Y * Y), np.arctan2(Y, X)


def mix(a, b, t):
    t = np.clip(t, 0, 1)[..., None]
    return np.asarray(a, float) * (1 - t) + np.asarray(b, float) * t


def col(c):
    return np.asarray(c, float)


def smooth(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def ridge(f, width):
    """Thin lines where a field crosses its middle."""
    return np.exp(-((f - 0.5) / width) ** 2)


def save(name, cross, along):
    rgb = np.concatenate([cross, along], axis=1)
    rgb = np.clip(rgb, 0, 255)
    out = os.path.join(mm.ROOT, "textures", "Hearthcooking")
    mm.dds(os.path.join(out, "flesh_" + name + ".dds"), mm.to_img(rgb), "DXT1")
    print("texture flesh_" + name)


X, Y, R, A = grid()


# ------------------------------------------------------------------ meat
def meat(name, seed, lean, lean_dark, fat, marbling, fat_width, sinew=0.0, sinew_col=(235, 225, 215), grain=0.5,
         fat_tint=None):
    """Muscle: a lean colour that shifts in patches; the grain - fine fibres in bundles - along it; fat running
    through in a branching net (marbling); a few pale sinew bands."""
    w1, w2, f1, f2, f3, f4 = (Field(seed + k) for k in range(6))
    out = {}
    for half in ("cross", "along"):
        if half == "cross":
            u, v = X * 0.5 + 0.5, Y * 0.5 + 0.5
            su, sv = 1.0, 1.0      # fibres end-on: a fine pebbled grain in bundles
        else:
            u, v = X * 0.5 + 0.5, Y * 0.5 + 0.5
            su, sv = 0.22, 2.2     # fibres along: long streaks
        # the lean: patches of darker and lighter muscle, warped
        wu = u + 0.08 * (w1(u * 1.0, v * 1.0) - 0.5)
        wv = v + 0.08 * (w2(u * 1.0, v * 1.0) - 0.5)
        patch = f1(wu * 1.3, wv * 1.3)
        base = mix(lean_dark, lean, smooth(0.25, 0.75, patch))
        # the grain: fibre bundles (cells) and the fibres within them
        bundles = f2(wu * su * 3.0, wv * sv * 3.0)
        fibres = f3(wu * su * 14.0, wv * sv * 14.0)
        g = (bundles - 0.5) * 0.5 + (fibres - 0.5) * 0.7
        base *= (1.0 + grain * 0.32 * g)[..., None]
        # bundle borders: thin pale lines of connective tissue between them
        borders = ridge(f2(wu * su * 3.0 + 0.37, wv * sv * 3.0 + 0.11), 0.018)
        base = mix(base, mix(lean, fat, 0.45), borders * 0.35)
        # marbling: a branching net of fat, soft-edged, flecked
        m1 = f4(wu * su * 1.6 + 0.2, wv * sv * 1.6)
        m2 = f1(wu * su * 4.0 + 0.5, wv * sv * 4.0 + 0.3)
        net = ridge(m1 + 0.12 * (m2 - 0.5), fat_width)
        fleck = smooth(0.78, 0.9, f3(wu * su * 9 + 0.6, wv * sv * 9 + 0.2)) * smooth(0.55, 0.75, m2)
        fatmask = np.clip(net * marbling + fleck * marbling * 0.5, 0, 1)
        fc = fat if fat_tint is None else mix(fat, fat_tint, f2(wu * 6, wv * 6))
        base = mix(base, fc, fatmask)
        # a slight translucency at the fat's edge: a pinkish halo
        halo = np.clip(ndimage.gaussian_filter(fatmask, 3) - fatmask, 0, 1)
        base = mix(base, mix(lean, fat, 0.3), halo * 0.6)
        # sinew: a few broad silvery bands
        if sinew > 0:
            sb = ridge(w1(wu * su * 0.9 + 0.3, wv * sv * 0.9 + 0.7), 0.03)
            base = mix(base, sinew_col, sb * sinew)
        # moisture: tiny bright specks
        spark = smooth(0.93, 0.99, f3(u * 40, v * 40)) * 0.12
        base = mix(base, (255, 245, 240), spark)
        out[half] = base
    save(name, out["cross"], out["along"])


# ------------------------------------------------------------------ vegetables and fruit
def carrot():
    f1, f2, f3, f4 = (Field(200 + k) for k in range(4))
    u, v = X * 0.5 + 0.5, Y * 0.5 + 0.5
    rw = R + 0.02 * (f1(u * 2, v * 2) - 0.5)
    core = 0.44 + 0.04 * np.cos(A * 7 + f2(u, v) * 6)  # the core's edge is a little lobed
    # phloem (outer, deep orange), cambium ring, xylem core (lighter, yellower)
    outer = mix((236, 106, 22), (248, 128, 34), f3(u * 3, v * 3))
    inner = mix((250, 160, 60), (255, 182, 86), f3(u * 4 + 0.3, v * 4))
    cross = mix(outer, inner, 1 - smooth(core - 0.015, core + 0.015, rw))
    cross = mix(cross, (214, 92, 18), ridge(rw - core + 0.5, 0.008) * 0.8)
    # rays: fine radial streaks across both
    rays = 0.5 + 0.5 * np.sin(A * 64 + f4(u * 2, v * 2) * 9)
    cross *= (1 + 0.07 * (rays - 0.5) * smooth(0.05, 0.3, rw))[..., None]
    # a star of paler pith right in the middle
    cross = mix(cross, (255, 196, 110), (1 - smooth(0.0, 0.07, rw)) * 0.7)
    # the skin edge: a thin darker rind
    cross = mix(cross, (200, 80, 12), smooth(0.93, 0.99, rw))
    cross *= (0.95 + 0.1 * f1(u * 12, v * 12))[..., None]
    # along: the core a band, its edges wavering, fibres along
    ay = np.abs(Y) + 0.03 * (f2(u * 4, v * 0.5) - 0.5)
    fib = f4(u * 1.2, v * 18)
    along = mix(mix((236, 106, 22), (248, 128, 34), fib), mix((250, 160, 60), (255, 182, 86), fib), 1 - smooth(0.42, 0.46, ay))
    along = mix(along, (214, 92, 18), ridge(ay - 0.44 + 0.5, 0.008) * 0.7)
    along = mix(along, (200, 80, 12), smooth(0.93, 0.99, ay))
    along *= (0.95 + 0.1 * f1(u * 2, v * 22))[..., None]
    save("carrot", cross, along)


def potato():
    f1, f2, f3, f4 = (Field(300 + k) for k in range(4))
    u, v = X * 0.5 + 0.5, Y * 0.5 + 0.5
    base = mix((246, 230, 160), (236, 214, 136), f1(u * 2, v * 2))
    # watery pith: paler, glassy streaks radiating from the middle
    pith = smooth(0.62, 0.78, f2(A / (2 * math.pi) * 6 + R * 0.6, R * 3)) * (1 - smooth(0.2, 0.75, R))
    cross = mix(base, (252, 244, 196), pith * 0.6)
    # the vascular ring just under the skin, with its dots
    rw = R + 0.015 * (f3(u * 3, v * 3) - 0.5)
    cross = mix(cross, (212, 184, 104), ridge(rw - 0.86 + 0.5, 0.012) * 0.55)
    dots = smooth(0.8, 0.9, f4(u * 30, v * 30)) * ridge(rw - 0.86 + 0.5, 0.03)
    cross = mix(cross, (196, 166, 90), dots * 0.6)
    # starch: a fine sparkle
    cross *= (0.96 + 0.08 * f4(u * 50, v * 50))[..., None]
    cross = mix(cross, (226, 200, 130), smooth(0.95, 0.995, rw) * 0.8)
    ay = np.abs(Y)
    along = mix((246, 230, 160), (236, 214, 136), f1(u * 2 + 0.4, v * 2))
    along = mix(along, (252, 244, 196), smooth(0.62, 0.78, f2(u * 2, v * 4)) * (1 - smooth(0.2, 0.75, ay)) * 0.6)
    along = mix(along, (212, 184, 104), ridge(ay - 0.86 + 0.5, 0.012) * 0.55)
    along *= (0.96 + 0.08 * f4(u * 50, v * 50))[..., None]
    along = mix(along, (226, 200, 130), smooth(0.95, 0.995, ay) * 0.8)
    save("potato", cross, along)


def apple():
    f1, f2, f3, f4 = (Field(400 + k) for k in range(4))
    u, v = X * 0.5 + 0.5, Y * 0.5 + 0.5
    base = mix((250, 244, 214), (240, 230, 190), f1(u * 2, v * 2))
    base *= (0.97 + 0.06 * f4(u * 40, v * 40))[..., None]       # juicy cells
    # the core: a five-pointed star of firmer, greener flesh
    star = 0.3 + 0.07 * np.cos(A * 5) + 0.01 * (f2(u * 3, v * 3) - 0.5)
    cross = mix(base, (232, 230, 182), (1 - smooth(star - 0.03, star + 0.01, R)) * 0.75)
    # its five seed chambers with papery walls, and the seeds
    for k in range(5):
        a = k * 2 * math.pi / 5
        ca, sa = math.cos(a), math.sin(a)
        lx = (X * ca + Y * sa) - 0.17
        ly = -X * sa + Y * ca
        cham = (lx / 0.11) ** 2 + (ly / 0.055) ** 2
        cross = mix(cross, (238, 232, 200), (1 - smooth(0.8, 1.0, cham)) * 0.9)
        cross = mix(cross, (214, 200, 160), ridge(cham * 0.5, 0.05) * 0.8)
        seed = ((lx + 0.01) / 0.07) ** 2 + (ly / 0.035) ** 2
        seedc = mix((92, 52, 26), (60, 34, 18), smooth(0, 1, seed))
        cross = mix(cross, seedc, 1 - smooth(0.85, 1.0, seed))
    # the ring of ten vessels round the core
    for k in range(10):
        a = k * 2 * math.pi / 10 + 0.31
        d = (X - 0.42 * math.cos(a)) ** 2 + (Y - 0.42 * math.sin(a)) ** 2
        cross = mix(cross, (226, 220, 170), np.exp(-d / 0.0006) * 0.7)
    cross = mix(cross, (236, 226, 176), smooth(0.94, 0.99, R) * 0.6)
    # along: the core a narrow channel, its seeds in it
    ay = np.abs(Y)
    along = mix((250, 244, 214), (240, 230, 190), f1(u * 2 + 0.3, v * 2))
    along *= (0.97 + 0.06 * f4(u * 40, v * 40))[..., None]
    along = mix(along, (232, 230, 182), (1 - smooth(0.12, 0.16, ay + 0.02 * (f2(u * 3, v) - 0.5))) * 0.75)
    for sx in (-0.15, 0.15):
        seed = ((X - sx) / 0.11) ** 2 + (Y / 0.045) ** 2
        along = mix(along, (80, 44, 22), 1 - smooth(0.85, 1.0, seed))
    along = mix(along, (236, 226, 176), smooth(0.94, 0.99, ay) * 0.6)
    save("apple", cross, along)


def tomato():
    f1, f2, f3, f4 = (Field(500 + k) for k in range(4))
    u, v = X * 0.5 + 0.5, Y * 0.5 + 0.5
    wall = mix((212, 36, 30), (228, 58, 40), f1(u * 3, v * 3))
    # the chambers (4 of them), between walls, round a pale centre
    aw = A + 0.08 * (f2(u * 2, v * 2) - 0.5)
    sector = np.abs(np.sin(aw * 2))
    inner = R < (0.72 + 0.03 * (f3(u * 2, v * 2) - 0.5))
    septum = 1 - smooth(0.08, 0.16, sector)
    cham = inner & (R > 0.2)
    gel = mix((240, 120, 70), (250, 150, 96), f4(u * 4, v * 4))
    cross = wall.copy()
    m = (cham * (1 - septum))
    cross = mix(cross, gel, m * 0.9)
    # the jelly's sheen and the seeds in it, each with its clear halo
    g = np.random.default_rng(55)
    seeds = np.zeros((S, S))
    for _ in range(160):
        rr = 0.3 + 0.35 * g.random()
        aa = g.random() * 2 * math.pi
        if abs(math.sin(aa * 2)) < 0.22:
            continue
        sx, sy = rr * math.cos(aa), rr * math.sin(aa)
        dx, dy = X - sx, Y - sy
        ta = aa + (g.random() - 0.5)
        lx = dx * math.cos(ta) + dy * math.sin(ta)
        ly = -dx * math.sin(ta) + dy * math.cos(ta)
        seeds = np.maximum(seeds, np.exp(-((lx / 0.018) ** 2 + (ly / 0.011) ** 2)))
    halo = ndimage.gaussian_filter(seeds, 4)
    cross = mix(cross, (252, 196, 150), np.clip(halo * 2.5, 0, 1) * 0.5 * cham)
    cross = mix(cross, (246, 222, 140), np.clip(seeds * 1.5, 0, 1) * cham)
    # the centre (columella): pale and fleshy
    cross = mix(cross, (238, 132, 104), (1 - smooth(0.16, 0.22, R)) * 0.85)
    cross = mix(cross, (186, 22, 20), smooth(0.95, 0.99, R))
    cross *= (0.95 + 0.1 * f1(u * 20, v * 20))[..., None]
    # along: walls at the edges, a chamber band with seeds, the core down the middle
    ay = np.abs(Y)
    along = mix((212, 36, 30), (228, 58, 40), f1(u * 3 + 0.2, v * 3))
    band = (ay > 0.16) & (ay < 0.7)
    along = mix(along, gel, band * 0.9)
    s2 = np.zeros((S, S))
    for _ in range(80):
        sx, sy = g.random() * 2 - 1, (0.2 + 0.45 * g.random()) * (1 if g.random() > 0.5 else -1)
        s2 = np.maximum(s2, np.exp(-(((X - sx) / 0.02) ** 2 + ((Y - sy) / 0.012) ** 2)))
    along = mix(along, (252, 196, 150), np.clip(ndimage.gaussian_filter(s2, 4) * 2.5, 0, 1) * 0.5 * band)
    along = mix(along, (246, 222, 140), np.clip(s2 * 1.5, 0, 1) * band)
    along = mix(along, (238, 132, 104), (1 - smooth(0.12, 0.17, ay)) * 0.85)
    along = mix(along, (186, 22, 20), smooth(0.95, 0.99, ay))
    along *= (0.95 + 0.1 * f1(u * 20, v * 20))[..., None]
    save("tomato", cross, along)


def cabbage():
    f1, f2, f3, f4 = (Field(600 + k) for k in range(4))
    u, v = X * 0.5 + 0.5, Y * 0.5 + 0.5
    # packed leaves: wavy layers round the heart, warped; each leaf pale with a thin greener edge
    rw = R + 0.05 * (f1(u * 2, v * 2) - 0.5) + 0.025 * np.sin(A * 3 + f2(u, v) * 5)
    t = rw * 15 + 1.5 * (f3(u * 1.5, v * 1.5) - 0.5)
    leaf = np.mod(t, 1.0)
    base = mix((246, 246, 222), (226, 236, 182), smooth(0.2, 1.0, rw))           # paler at the heart, greener out
    edge = ridge(leaf, 0.05)
    cross = mix(base, (176, 204, 120), edge * (0.35 + 0.5 * smooth(0.3, 1.0, rw)))
    gap = 1 - smooth(0.0, 0.04, np.minimum(leaf, 1 - leaf))
    cross = mix(cross, (210, 222, 170), gap * 0.4)
    # veins: pale thick ribs crossing the layers
    vein = ridge(f4(A / (2 * math.pi) * 3, rw * 1.5), 0.03)
    cross = mix(cross, (252, 252, 238), vein * 0.5)
    # the heart: the stem, solid and pale
    cross = mix(cross, (240, 244, 214), (1 - smooth(0.08, 0.13, R)))
    cross *= (0.96 + 0.08 * f1(u * 16, v * 16))[..., None]
    # along: the layers as long wavy stripes
    ayw = np.abs(Y) + 0.04 * (f1(u * 2, v * 1) - 0.5) + 0.02 * np.sin(X * 6 + f2(u, v) * 4)
    t2 = ayw * 15 + 1.5 * (f3(u * 0.7, v * 1.5) - 0.5)
    leaf2 = np.mod(t2, 1.0)
    along = mix((246, 246, 222), (226, 236, 182), smooth(0.2, 1.0, ayw))
    along = mix(along, (176, 204, 120), ridge(leaf2, 0.05) * (0.35 + 0.5 * smooth(0.3, 1.0, ayw)))
    along = mix(along, (210, 222, 170), (1 - smooth(0.0, 0.04, np.minimum(leaf2, 1 - leaf2))) * 0.4)
    along = mix(along, (240, 244, 214), (1 - smooth(0.08, 0.13, np.abs(Y))))
    along *= (0.96 + 0.08 * f1(u * 16, v * 16))[..., None]
    save("cabbage", cross, along)


def leek():
    f1, f2, f3 = (Field(700 + k) for k in range(3))
    u, v = X * 0.5 + 0.5, Y * 0.5 + 0.5
    # across: tight rings of sheath round the heart, white in, pale green out, a thin line between each
    rw = R + 0.02 * (f1(u * 3, v * 3) - 0.5) + 0.012 * np.sin(A * 2 + f2(u, v) * 4)
    t = rw * 9 + 0.3 * (f3(u, v) - 0.5)
    ring = np.mod(t, 1.0)
    cross = mix((250, 250, 238), (214, 230, 170), smooth(0.25, 0.95, rw))
    cross = mix(cross, (168, 196, 120), ridge(ring, 0.06) * (0.4 + 0.5 * smooth(0.2, 1.0, rw)))
    cross = mix(cross, (236, 244, 214), (1 - smooth(0.04, 0.09, R)))
    cross = mix(cross, (150, 180, 100), smooth(0.97, 1.0, R))  # its outer skin
    cross *= (0.97 + 0.06 * f1(u * 18, v * 18))[..., None]
    # along: the sheaths as long straight stripes
    ay = np.abs(Y) + 0.01 * (f1(u * 4, v) - 0.5)
    t2 = ay * 9 + 0.2 * (f3(u * 0.5, v) - 0.5)
    along = mix((250, 250, 238), (214, 230, 170), smooth(0.25, 0.95, ay))
    along = mix(along, (168, 196, 120), ridge(np.mod(t2, 1.0), 0.06) * (0.4 + 0.5 * smooth(0.2, 1.0, ay)))
    along = mix(along, (150, 180, 100), smooth(0.97, 1.0, ay))
    along *= (0.97 + 0.06 * f1(u * 30, v * 6))[..., None]
    save("leek", cross, along)


def all_flesh():
    meat("beef", 10, lean=(186, 40, 44), lean_dark=(140, 24, 32), fat=(244, 230, 218), marbling=0.95, fat_width=0.022,
         sinew=0.25, grain=0.6)
    meat("venison", 20, lean=(128, 22, 30), lean_dark=(92, 12, 20), fat=(214, 160, 150), marbling=0.25, fat_width=0.012,
         sinew=0.15, sinew_col=(196, 150, 150), grain=0.7)
    meat("pheasant", 30, lean=(160, 68, 86), lean_dark=(118, 42, 62), fat=(236, 214, 150), marbling=0.4,
         fat_width=0.014, sinew=0.45, sinew_col=(226, 196, 180), grain=0.6, fat_tint=(246, 230, 196))
    meat("chicken", 40, lean=(246, 206, 200), lean_dark=(234, 184, 180), fat=(252, 238, 232), marbling=0.35,
         fat_width=0.016, sinew=0.2, sinew_col=(252, 236, 228), grain=0.8)
    carrot()
    potato()
    apple()
    tomato()
    cabbage()
    leek()


if __name__ == "__main__":
    if len(sys.argv) > 1:
        for name in sys.argv[1:]:
            globals()[name]()
    else:
        all_flesh()
