"""Renders the pie's crimped edge and the crostata's folded edge (tools/pastry.py) to PNGs, to look at before they go
in game: a small z-buffered rasteriser with smooth normals, a warm key light and soft fill."""
import math
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import pastry  # noqa: E402

W, H = 1100, 900


def rotz(P, a):
    c, s = math.cos(a), math.sin(a)
    R = np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])
    return P @ R.T


def lathe(prof, segs=96):
    P, I = [], []
    n = len(prof)
    for i in range(segs):
        a = 2 * math.pi * i / segs
        for r, z in prof:
            P.append((r * math.cos(a), r * math.sin(a), z))
    for i in range(segs):
        for j in range(n - 1):
            a, b = i * n + j, ((i + 1) % segs) * n + j
            I += [(a, b, b + 1), (a, b + 1, a + 1)]
    P, I = np.array(P, float), np.array(I)
    return P, I


def ellipsoid(c, rx, ry, rz, a=0.0, nu=18, nv=10):
    P, I = [], []
    for i in range(nv + 1):
        v = math.pi * i / nv
        for j in range(nu):
            u = 2 * math.pi * j / nu
            P.append((rx * math.sin(v) * math.cos(u), ry * math.sin(v) * math.sin(u), rz * math.cos(v)))
    for i in range(nv):
        for j in range(nu):
            a0, b0 = i * nu + j, i * nu + (j + 1) % nu
            I += [(a0, a0 + nu, b0 + nu), (a0, b0 + nu, b0)]
    P = rotz(np.array(P), a) + np.array(c)
    return P, np.array(I)


def normals(P, I):
    N = np.zeros_like(P)
    t = P[I]
    fn = np.cross(t[:, 1] - t[:, 0], t[:, 2] - t[:, 0])
    for k in range(3):
        np.add.at(N, I[:, k], fn)
    return N / np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-9)


class Scene:
    def __init__(self):
        self.parts = []  # (P, N, I, albedo fn)

    def add(self, P, I, albedo, N=None, spec=0.15):
        self.parts.append((P, normals(P, I) if N is None else N, I, albedo, spec))

    def render(self, eye, target, fov=32):
        f = np.array(target, float) - eye
        f /= np.linalg.norm(f)
        r = np.cross(f, (0, 0, 1))
        r /= np.linalg.norm(r)
        u = np.cross(r, f)
        img = np.zeros((H, W, 3))
        zb = np.full((H, W), np.inf)
        # backdrop: dark wood board
        yy, xx = np.mgrid[0:H, 0:W]
        grain = 0.5 + 0.5 * np.sin(yy * 0.045 + np.sin(xx * 0.01) * 3 + np.sin(yy * 0.3) * 0.2)
        img[:] = (np.stack([0.30, 0.20, 0.12])[None, None] * (0.75 + 0.25 * grain[..., None]))
        foc = 0.5 * H / math.tan(math.radians(fov) / 2)
        L = np.array([-0.45, -0.55, 0.75])
        L /= np.linalg.norm(L)
        L2 = np.array([0.6, 0.3, 0.5])
        L2 /= np.linalg.norm(L2)
        for P, N, I, alb, spec in self.parts:
            d = P - eye
            x, y, z = d @ r, d @ u, d @ f
            sx = W / 2 + foc * x / z
            sy = H / 2 - foc * y / z
            A = alb(P)
            for tri in I:
                a, b, c = tri
                if min(z[a], z[b], z[c]) <= 0.1:
                    continue
                xs, ys = np.array([sx[a], sx[b], sx[c]]), np.array([sy[a], sy[b], sy[c]])
                x0, x1 = int(max(0, np.floor(xs.min()))), int(min(W - 1, np.ceil(xs.max())))
                y0, y1 = int(max(0, np.floor(ys.min()))), int(min(H - 1, np.ceil(ys.max())))
                if x0 > x1 or y0 > y1:
                    continue
                den = (ys[1] - ys[2]) * (xs[0] - xs[2]) + (xs[2] - xs[1]) * (ys[0] - ys[2])
                if abs(den) < 1e-9:
                    continue
                gy, gx = np.mgrid[y0:y1 + 1, x0:x1 + 1] + 0.5
                w0 = ((ys[1] - ys[2]) * (gx - xs[2]) + (xs[2] - xs[1]) * (gy - ys[2])) / den
                w1 = ((ys[2] - ys[0]) * (gx - xs[2]) + (xs[0] - xs[2]) * (gy - ys[2])) / den
                w2 = 1 - w0 - w1
                m = (w0 >= -1e-4) & (w1 >= -1e-4) & (w2 >= -1e-4)
                if not m.any():
                    continue
                iz = w0 / z[a] + w1 / z[b] + w2 / z[c]
                zz = 1 / iz
                sub = zb[y0:y1 + 1, x0:x1 + 1]
                m &= zz < sub
                if not m.any():
                    continue
                pw0, pw1, pw2 = w0 / z[a] * zz, w1 / z[b] * zz, w2 / z[c] * zz
                n = pw0[..., None] * N[a] + pw1[..., None] * N[b] + pw2[..., None] * N[c]
                n /= np.linalg.norm(n, axis=-1, keepdims=True)
                if np.dot(n[m].mean(0), -f) < -0.6:
                    n = -n
                col = pw0[..., None] * A[a] + pw1[..., None] * A[b] + pw2[..., None] * A[c]
                pos = pw0[..., None] * P[a] + pw1[..., None] * P[b] + pw2[..., None] * P[c]
                vdir = eye - pos
                vdir /= np.linalg.norm(vdir, axis=-1, keepdims=True)
                dif = np.clip(n @ L, 0, 1)
                dif2 = np.clip(n @ L2, 0, 1)
                hv = L + vdir
                hv /= np.linalg.norm(hv, axis=-1, keepdims=True)
                sp = np.clip((n * hv).sum(-1), 0, 1) ** 28 * spec
                ao = np.clip(0.55 + 0.45 * n[..., 2], 0, 1)
                sh = col * (0.30 * ao[..., None] + 0.85 * dif[..., None] * np.array([1.0, 0.93, 0.82]) + 0.22 * dif2[..., None] * np.array([0.75, 0.82, 1.0])) + sp[..., None]
                sub[m] = zz[m]
                img[y0:y1 + 1, x0:x1 + 1][m] = sh[m]
        return Image.fromarray((np.clip(img, 0, 1) ** (1 / 1.1) * 255).astype(np.uint8))


def dough_albedo(baked=0.0):
    def f(P):
        rng = np.random.default_rng(3)
        nz = 0.04 * np.sin(P[:, 0] * 7.1 + P[:, 1] * 3.3) + 0.03 * np.sin(P[:, 1] * 11.0 - P[:, 2] * 5.0)
        base = np.array([0.93, 0.84, 0.66])
        gold = np.array([0.86, 0.56, 0.24])
        # baked: the high, outer bits go deeper gold
        hi = np.clip((P[:, 2] - 1.4) / 0.6, 0, 1)
        k = baked * (0.55 + 0.45 * hi)
        c = base[None] * (1 - k[:, None]) + gold[None] * k[:, None]
        return np.clip(c * (1 + nz[:, None]) + 0.02 * (rng.random((len(P), 1)) - 0.5), 0, 1)
    return f


def solid(rgb):
    return lambda P: np.tile(np.array(rgb), (len(P), 1))


def sheet(r=5.0):
    k = r / 5.0
    prof = [(0.0, 1.3), (4.6 * k, 1.3), (4.9 * k, 1.22), (5.02 * k, 1.0), (4.98 * k, 0.6), (4.8 * k, 0.45), (0.0, 0.45)]
    return lathe(prof)


def pie(baked=0.0):
    S = Scene()
    P, I = lathe([(0.0, 1.3), (4.3, 1.3), (4.3, 0.45), (0.0, 0.45)])  # the sheet ends inside the rim: the crimped edge is the pie's outside
    S.add(P, I, dough_albedo(baked))
    cp, cn, _, ci = pastry.pie_crimp()
    for k in range(24):
        a = (k + 0.5) / 24 * 2 * math.pi
        S.add(rotz(cp, a), ci, dough_albedo(baked), N=rotz(cn, a))
    return S


def crostata(baked=0.0):
    S = Scene()
    P, I = sheet()
    S.add(P, I, dough_albedo(baked))
    # plum slices fanned round, overlapping, under the folds
    rng = np.random.default_rng(5)
    for ring, (rr, n) in enumerate(((3.7, 22), (2.7, 17), (1.7, 12), (0.8, 6))):
        for k in range(n):
            a = 2 * math.pi * (k + 0.5 * ring) / n
            c = (rr * math.cos(a), rr * math.sin(a), 1.42 + 0.06 * ring + 0.03 * rng.random())
            Pf, If = ellipsoid(c, 0.95, 0.32, 0.2, a + 0.5)
            S.add(Pf, If, solid((0.30 + 0.08 * rng.random(), 0.04, 0.09)), spec=0.6)
    for j in range(8):
        fp, fn, _, fi = pastry.galette_fold(j)
        a = (j + 0.5) / 8 * 2 * math.pi + 0.1
        S.add(rotz(fp, a), fi, dough_albedo(baked), N=rotz(fn, a))
    return S


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/out"
    eye = np.array([0.0, -11.5, 17.0])
    for name, sc in (("pie_crimp_preview", pie()), ("crostata_fold_preview", crostata(0.0)), ("crostata_baked_preview", crostata(0.85))):
        sc.render(eye, (0, 0.4, 1.0)).save(f"{out}/{name}.png")
        print(name)
