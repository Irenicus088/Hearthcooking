"""The pastry edges, as meshes in the game's own units about the dough's middle (the rolled sheet is 5 across the
radius, its top at z 1.3).

  pie_crimp()    one scallop of a fluted pie rim (a 24th of the way round, centred on +x): the rim stood up as a
                 rounded wall, bulging out in a U between two pinches, the pinch pressed in on the inside
  galette_fold() one of the eight pleats of a crostata's edge (an eighth of the way round, centred on +x, and a
                 little more, to tuck under the next): the rim lifted and laid in over the fruit, the inside edge
                 a straight chord so the opening is many-sided, the fold rounded and a little uneven

Each returns (P, N, UV, I): positions, normals, uvs (n x 3, n x 3, n x 2) and triangle indices (m x 3).
"""
import math

import numpy as np


def _grid(P, closed_v):
    """Triangles over a (nu, nv) grid of points; v wraps round if closed."""
    nu, nv = P.shape[:2]
    I = []
    for i in range(nu - 1):
        for j in range(nv if closed_v else nv - 1):
            j1 = (j + 1) % nv
            a, b, c, d = i * nv + j, (i + 1) * nv + j, (i + 1) * nv + j1, i * nv + j1
            I += [(a, b, c), (a, c, d)]
    return np.array(I)


def _normals(P, I):
    N = np.zeros_like(P)
    t = P[I]
    fn = np.cross(t[:, 1] - t[:, 0], t[:, 2] - t[:, 0])
    for k in range(3):
        np.add.at(N, I[:, k], fn)
    return N / np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-9)


def _cap(P, ring_idx, centre, toward):
    """A fan closing an open end: the ring's points about a centre, facing 'toward'."""
    c = len(P)
    P = np.vstack([P, centre[None]])
    I = []
    n = len(ring_idx)
    for j in range(n):
        a, b = ring_idx[j], ring_idx[(j + 1) % n]
        tri = (c, a, b)
        f = np.cross(P[a] - P[c], P[b] - P[c])
        I.append(tri if np.dot(f, toward) > 0 else (c, b, a))
    return P, I


def _superellipse(phi, e):
    c, s = np.cos(phi), np.sin(phi)
    return np.sign(c) * np.abs(c) ** e, np.sign(s) * np.abs(s) ** e


def _orient(P, I, nphi, row, col):
    """The grid winds the same way all over: turned so the face at (row, col), which looks up, faces up."""
    I = np.array(I)
    k = next(n for n, tri in enumerate(I) if tri[0] == row * nphi + col)
    t = P[I[k]]
    if np.cross(t[1] - t[0], t[2] - t[0])[2] < 0:
        I = I[:, [0, 2, 1]]
    return I.tolist()


def _finish(P, I):
    I = np.array(I)
    N = _normals(P, I)
    UV = P[:, :2] * 0.16 + 0.5
    return P.astype(np.float64), N, UV, I


def pie_crimp(nt=26, nphi=30, seed=0):
    rng = np.random.default_rng(seed)
    half = math.pi / 24  # 7.5 degrees
    ts = np.linspace(-0.5, 0.5, nt)
    phis = np.linspace(0, 2 * math.pi, nphi, endpoint=False)
    P = np.zeros((nt, nphi, 3))
    wob = 1.0 + 0.03 * (rng.random(nphi) - 0.5)
    for i, t in enumerate(ts):
        s = math.cos(math.pi * t)  # 1 in the scallop's middle, 0 at the pinches
        ro = 4.48 + 0.56 * s ** 0.55  # the U bulging out, pinched deep between
        ri = 4.12 - 0.34 * (1 - s) ** 4 + 0.04 * s  # the pinch pressed in on the inside
        zb, zt = 0.45, 1.72 + 0.16 * s ** 0.5  # down to the board: it is the pie's outside  # standing up off the sheet, highest mid-scallop
        rc, zc = (ro + ri) / 2, (zb + zt) / 2
        w, h = (ro - ri) / 2, (zt - zb) / 2
        a = t * 2 * half
        cr, sr = _superellipse(phis, 0.62)
        lean = 0.10 * (sr + 1) * 0.5  # the top leans out a touch
        r = rc + w * cr * wob + lean
        z = zc + h * sr
        P[i, :, 0], P[i, :, 1], P[i, :, 2] = r * math.cos(a), r * math.sin(a), z
    flat = P.reshape(-1, 3)
    I = _orient(flat, _grid(P, True), P.shape[1], nt // 2, nphi // 4)
    for end, toward in ((0, -1), (nt - 1, 1)):
        ring = list(range(end * nphi, end * nphi + nphi))
        cen = flat[ring].mean(axis=0)
        a = (end / (nt - 1) - 0.5) * 2 * half
        flat, tris = _cap(flat, ring, cen, np.array([-math.sin(a), math.cos(a), 0.0]) * toward)
        I += tris
    return _finish(flat, I)


def galette_fold(index=0, nt=34, seed=0):
    """Pleat 'index' (0..7) - each a little different."""
    rng = np.random.default_rng(1000 + index * 7 + seed)
    span = math.pi / 4  # 45 degrees
    over = math.radians(12)  # carried on under the next pleat
    a0, a1 = -span / 2, span / 2 + over
    ts = np.linspace(0, 1, nt)
    # the section: a closed loop in (r, z) - outside of the fold round over the top and in along the flap's top,
    # the inner edge rounded, back along its underside and round the inside of the fold
    nf, nflap, ne = 14, 14, 6
    tau = 0.19  # half the dough's thickness
    jitter = rng.random(4)
    rows = []
    rin0 = 3.05 + 0.3 * jitter[1]  # how far in this pleat comes
    for t in ts:
        a = a0 + (a1 - a0) * t
        tt = t * (a1 - a0) / span  # 0..1 over the pleat itself, past 1 on its tail
        tail = max(0.0, tt - 1.0) / (over / span)  # 0..1 over the bit tucked onto the next
        rF = 4.55 + 0.07 * math.sin(2 * math.pi * tt + jitter[0] * 6)
        zF = 1.12
        rho = 0.5 + 0.1 * math.sin(math.pi * min(1.0, tt))
        lift = 0.24 * tail + 0.04 * tt
        thick = tau * (1.0 - 0.6 * tail)  # thinning to an edge where it lies over the next
        # the inner edge: a straight chord (the opening has corners), a little wavy
        rin = rin0 / max(0.7, math.cos(a)) + 0.06 * math.sin(3 * math.pi * tt + jitter[2] * 6)
        rin = min(rin, rF - 0.8)
        zin = 1.6 + lift
        ztop = zF + rho + lift
        # the pleat's crease: dough gathered into a ridge running in from the fold, near the pleat's start
        def crease(u):
            return 0.12 * math.exp(-(((tt - 0.42 - 0.1 * u) / 0.06) ** 2))
        loop = []
        for k in range(nf):
            th = -math.pi / 2 + math.pi * k / (nf - 1)
            q = k / (nf - 1)
            loop.append((rF + (rho + thick) * math.cos(th), zF + (rho + thick) * math.sin(th) + lift * q + crease(0) * q))
        for k in range(1, nflap):
            u = k / (nflap - 1)
            r = rF + (rin - rF) * u
            z = ztop + thick + (zin - ztop) * u + 0.14 * math.sin(math.pi * u) + crease(u) + 0.03 * math.sin(9 * u + jitter[3] * 6)
            loop.append((r, z))
        for k in range(1, ne):
            th = math.pi / 2 + math.pi * k / ne
            loop.append((rin + thick * 0.55 * math.cos(th), zin + crease(1.0) + thick * math.sin(th)))
        for k in range(nflap - 2, 0, -1):
            u = k / (nflap - 1)
            r = rF + (rin - rF) * u
            z = ztop - thick + (zin - ztop) * u + 0.08 * math.sin(math.pi * u) + crease(u)
            loop.append((r, z))
        for k in range(nf - 1, -1, -1):
            th = -math.pi / 2 + math.pi * k / (nf - 1)
            q = k / (nf - 1)
            loop.append((rF + max(0.05, rho - thick) * math.cos(th), zF + max(0.05, rho - thick) * math.sin(th) + lift * q + crease(0) * q))
        loop = np.array(loop)
        rows.append([(r * math.cos(a), r * math.sin(a), z) for r, z in loop])
    P = np.array(rows)
    nphi = P.shape[1]
    flat = P.reshape(-1, 3)
    I = _orient(flat, _grid(P, True), P.shape[1], nt // 2, nf + nflap // 2)
    for end, toward in ((0, -1), (nt - 1, 1)):
        ring = list(range(end * nphi, end * nphi + nphi))
        cen = flat[ring].mean(axis=0)
        a = a0 if end == 0 else a1
        flat, tris = _cap(flat, ring, cen, np.array([-math.sin(a), math.cos(a), 0.0]) * toward)
        I += tris
    return _finish(flat, I)


def _layer(corners, bend, ruffle, z0, half, n=10, seed=0):
    """One thin, curved layer of leaf with straight cut sides: a sheet over the quad 'corners' (x, y), cupped by
    'bend' across its length, its surface a little rippled; solid (top, bottom and the four cut edges)."""
    rng = np.random.default_rng(seed)
    ph = rng.random(2) * 6.283
    c = np.array(corners, float)
    def at(u, v, side):
        p = (1 - u) * (1 - v) * c[0] + u * (1 - v) * c[1] + u * v * c[2] + (1 - u) * v * c[3]
        z = z0 + bend * ((u - 0.5) ** 2 * 4 - 1) * 0.5 + ruffle * math.sin(7 * v + ph[0]) * math.sin(5 * u + ph[1])
        return (p[0], p[1], z + side * half)
    P, I = [], []
    idx = {}
    for side in (1, -1):
        for i in range(n + 1):
            for j in range(n + 1):
                idx[(side, i, j)] = len(P)
                P.append(at(i / n, j / n, side))
    for side in (1, -1):
        for i in range(n):
            for j in range(n):
                a, b, cc, d = idx[(side, i, j)], idx[(side, i + 1, j)], idx[(side, i + 1, j + 1)], idx[(side, i, j + 1)]
                I += [(a, b, cc), (a, cc, d)] if side > 0 else [(a, cc, b), (a, d, cc)]
    # the cut edges round the outside
    ring = [(i, 0) for i in range(n)] + [(n, j) for j in range(n)] + [(i, n) for i in range(n, 0, -1)] + [(0, j) for j in range(n, 0, -1)]
    for k in range(len(ring)):
        (i0, j0), (i1, j1) = ring[k], ring[(k + 1) % len(ring)]
        a, b = idx[(1, i0, j0)], idx[(1, i1, j1)]
        cc, d = idx[(-1, i1, j1)], idx[(-1, i0, j0)]
        I += [(a, d, cc), (a, cc, b)]
    return np.array(P, float), I


def leaf_bit(variant=0):
    """A chopped bit of cabbage: two to four thin layers of leaf, stacked a little apart and fanned, cut with straight
    sides (the knife's) and curved through (the leaf's) - squares, strips and wedges - about one unit across."""
    rng = np.random.default_rng(31 + variant * 17)
    layers = 2 + variant % 3
    shape = variant % 3  # a squarish piece, a long strip, a wedge
    w0 = 0.9 + 0.2 * rng.random()
    h0 = (0.75, 0.38, 0.8)[shape] * (0.85 + 0.3 * rng.random())
    P_all, I_all = [], []
    for k in range(layers):
        w, h = w0 * (1.0 - 0.08 * k + 0.06 * rng.random()), h0 * (1.0 - 0.06 * k)
        sk = (rng.random() - 0.5) * 0.35
        corners = [(-w / 2 + sk, -h / 2), (w / 2 + sk * 0.3, -h / 2 + 0.06 * rng.random()), (w / 2 - sk * 0.4, h / 2), (-w / 2 - sk, h / 2 - 0.1 * rng.random())]
        if shape == 2:  # a wedge: one end come to (almost) a point
            corners[2] = (corners[2][0] - 0.05, corners[2][1] * 0.15 + corners[1][1] * 0.0)
            corners[1] = (corners[1][0], corners[1][1] * 0.15)
        ang = (rng.random() - 0.5) * 0.5 + k * 0.14  # fanned
        ca, sa = math.cos(ang), math.sin(ang)
        corners = [(x * ca - y * sa + 0.07 * k, x * sa + y * ca - 0.05 * k) for x, y in corners]
        P, I = _layer(corners, 0.3 + 0.2 * rng.random(), 0.03, -0.16 * (layers - 1) * 0.5 + 0.16 * k + 0.04 * rng.random(), 0.022, seed=variant * 10 + k)
        base = sum(len(p) for p in P_all)
        P_all.append(P)
        I_all += [(a + base, b + base, c + base) for a, b, c in I]
    P = np.vstack(P_all)
    I = np.array(I_all)
    N = _normals(P, I)
    UV = P[:, :2] * 0.9 + 0.5
    return P, N, UV, I
