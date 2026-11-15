#!/usr/bin/env python3
"""A quick look at the kitchen's meshes laid out as the DLL lays them out, drawn flat-shaded to a PNG.

    python3 tools/preview_kitchen.py pot|spit|oven out.png

Only for checking the geometry and the layout (src/sim/Kitchen.h) without the game: no textures, no food.
"""
import math, os, struct, sys

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from nif import Nif

MESH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "meshes", "Hearthcooking")
COL = {"Worktop": (130, 90, 55), "Hearth": (125, 118, 106), "Wall": (110, 100, 86), "Linen": (215, 205, 180), "Board": (196, 158, 108),
       "Blade": (150, 150, 155), "Handle": (90, 60, 36), "Bolster": (120, 120, 120), "Pot": (55, 53, 50), "Broth": (150, 100, 55),
       "Bowl": (170, 130, 90), "Bark": (70, 52, 36), "Ends": (190, 150, 100), "Stones": (120, 115, 105), "Ash": (50, 46, 42),
       "Posts": (90, 60, 36), "Rod": (60, 60, 62), "Grip": (90, 60, 36), "Base": (150, 115, 88), "Firebox": (20, 16, 14),
       "Dome": (150, 115, 88), "Mouth": (140, 106, 80), "Planks": (110, 74, 44), "Straps": (50, 50, 50), "Plate": (170, 130, 90),
       "Salt": (240, 240, 236), "Chunk": (230, 220, 200), "Dough": (238, 220, 186), "Cap": (230, 220, 200)}


def load(name):
    n = Nif(open(os.path.join(MESH, name + ".nif"), "rb").read())
    out = []
    for i, b in enumerate(n.blocks):
        if n.type(i) != "BSTriShape":
            continue
        sname = n.strings[struct.unpack_from("<i", b, 0)[0]]
        tris, nv, size = struct.unpack_from("<HHI", b, 108)
        V = np.array([struct.unpack_from("<3f", b, 116 + k * 28) for k in range(nv)])
        I = np.array(struct.unpack_from(f"<{tris * 3}H", b, 116 + nv * 28)).reshape(-1, 3)
        out.append((sname, V, I))
    return out


def rot_z(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])


def main(kind, path):
    L = dict(FireX=18.0, FireY=3.0, BoardX=-7.0, BoardY=-1.0, PotX=18.0, PotY=3.0, LiquidZ=7.6, SpitZ=10.5, OvenX=18.0, OvenY=7.5,
             MouthY=-2.1, OvenFloor=9.0, PileX=31.5, PileY=-7.0, SaltX=-1.0, SaltY=-14.0, ServeX=7.0, ServeY=-14.0)
    place = [("Worktop", (0, 0, 0), 0), ("Room", (0, 0, 0), 0), ("Cloth", (0, 0, 0), 0), ("Board", (L["BoardX"], L["BoardY"], 0.55), 0),
             ("Knife", (L["BoardX"] + 5, L["BoardY"] - 3.8, 1.1), 0), ("SaltBowl", (L["SaltX"], L["SaltY"], 0), 0)]
    for k in range(3):
        place.append(("Log", (L["PileX"] + (k % 2) * 0.6, L["PileY"] + k * 2.1 - 2, 0.95 + (k // 2) * 1.7), 0.3))
    if kind in ("pot", "spit"):
        place.append(("FirePit", (L["FireX"], L["FireY"], 0), 0))
        place += [("Log", (L["FireX"] - 1, L["FireY"] - 1.2, 0.9), 0.5), ("Log", (L["FireX"] + 1, L["FireY"] + 1.2, 0.9), -0.4)]
    if kind == "pot":
        place += [("Pot", (L["PotX"], L["PotY"], 0), 0), ("Broth", (L["PotX"], L["PotY"], L["LiquidZ"]), 0),
                  ("Ladle", (L["PotX"] + 2.5, L["PotY"] - 1.5, L["LiquidZ"] + 0.5), 0), ("Bowl", (L["ServeX"], L["ServeY"], 0), 0)]
        for k in range(6):
            place.append(("Chunk", (L["BoardX"] + k * 1.6 - 4, L["BoardY"], 1.7), 0))
    if kind == "spit":
        place += [("SpitPosts", (L["FireX"], L["FireY"], 0), 0), ("SpitRod", (L["FireX"], L["FireY"], L["SpitZ"]), 0),
                  ("Plate", (L["ServeX"], L["ServeY"], 0), 0)]
    if kind == "oven":
        place += [("OvenBase", (L["OvenX"], L["OvenY"], 0), 0), ("OvenDome", (L["OvenX"], L["OvenY"], 0), 0),
                  ("OvenDoor", (L["OvenX"] - 5.2, L["MouthY"] - 0.35, L["OvenFloor"]), -1.6), ("Peel", (L["OvenX"] - 10, -12, 1.0), 0.3),
                  ("Dough", (L["BoardX"], L["BoardY"], 3.0), 0), ("Plate", (L["ServeX"], L["ServeY"], 0), 0)]
    tris = []
    for name, at, ang in place:
        for sname, V, I in load(name):
            W = V @ rot_z(ang).T + np.array(at)
            if name == "Dough":
                W = V * np.array([3.5, 3.0, 2.0]) + np.array(at)
            if name == "Chunk":
                W = V * 1.3 + np.array(at)
            for t in I:
                tris.append((W[t], COL.get(sname, (200, 0, 200))))
    # the camera: as the DLL's, looking down at 55 degrees
    Wd, Hd = 1600, 900
    tilt = math.radians(55)
    vfov = math.radians(38)
    dist = 92.0
    tgt = np.array([0.0, -1.0, 0.0])
    eye = tgt + np.array([0, -math.cos(tilt) * dist, math.sin(tilt) * dist])
    f = tgt - eye
    f /= np.linalg.norm(f)
    r = np.cross(f, [0, 0, 1])
    r /= np.linalg.norm(r)
    u = np.cross(r, f)
    light = np.array([-0.35, -0.5, 0.8])
    light /= np.linalg.norm(light)
    th = math.tan(vfov / 2)
    polys = []
    for P, col in tris:
        d = P - eye
        z = d @ f
        if (z <= 1).any():
            continue
        sx = Wd / 2 + (d @ r) / (z * th * Wd / Hd) * Wd / 2
        sy = Hd / 2 - (d @ u) / (z * th) * Hd / 2
        n = np.cross(P[1] - P[0], P[2] - P[0])
        ln = np.linalg.norm(n)
        if ln < 1e-9:
            continue
        n /= ln
        if n @ (eye - P.mean(0)) < 0:
            n = -n
        k = 0.35 + 0.65 * max(0.0, n @ light)
        polys.append((z.mean(), list(zip(sx, sy)), tuple(int(c * k) for c in col)))
    polys.sort(key=lambda p: -p[0])
    img = Image.new("RGB", (Wd, Hd), (24, 20, 18))
    dr = ImageDraw.Draw(img)
    for _, pts, c in polys:
        dr.polygon(pts, fill=c)
    img.save(path)
    print("wrote", path, len(polys), "triangles")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
