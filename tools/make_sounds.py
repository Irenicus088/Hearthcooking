#!/usr/bin/env python3
"""The kitchen's sounds, synthesized and baked to WAV for the SKSE plugin (Data/SKSE/Plugins/Hearthcooking/Sounds).

All mono, 16-bit, 48 kHz. Every sound is made here from noise and sine waves (nothing recorded, nothing borrowed);
the menu sounds (ui_*) are Skyrim Yacht's own.
  chop_N      the knife through something and into the board      thunk_N   the knife on the bare board
  log_N       a log set on the fire                                splash_N  a piece into the broth
  hiss_N      something landing in the fire                        stir_N    the ladle scraping round the pot
  salt_N      a pinch of salt                                      knead_N   dough pushed and slapped
  crank_N     the spit's crank turning a notch
  door_open / door_close  the oven door                            peel      the peel sliding on stone
  pour        something poured in                                  flour     a puff of flour
  done / burnt            the result
  loop_fire / loop_simmer / loop_boil / loop_sizzle   seamless loops the DLL fades in and out
"""
import math
import os
import wave

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets", "game_sounds")
SR = 48000
rng = np.random.default_rng(23)


def write(name, x):
    x = np.clip(np.asarray(x, dtype=np.float64), -1.0, 1.0)
    with wave.open(os.path.join(OUT, name + ".wav"), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes((x * 32767.0).astype("<i2").tobytes())


def noise(sec):
    return rng.standard_normal(int(sec * SR))


def biquad(x, kind, f0, q=0.7071, gain_db=0.0):
    A = 10 ** (gain_db / 40.0)
    w0 = 2 * math.pi * f0 / SR
    cw, sw = math.cos(w0), math.sin(w0)
    alpha = sw / (2 * q)
    if kind == "lowpass":
        b = [(1 - cw) / 2, 1 - cw, (1 - cw) / 2]; a = [1 + alpha, -2 * cw, 1 - alpha]
    elif kind == "highpass":
        b = [(1 + cw) / 2, -(1 + cw), (1 + cw) / 2]; a = [1 + alpha, -2 * cw, 1 - alpha]
    elif kind == "bandpass":
        b = [alpha, 0, -alpha]; a = [1 + alpha, -2 * cw, 1 - alpha]
    else:  # peaking
        b = [1 + alpha * A, -2 * cw, 1 - alpha * A]; a = [1 + alpha / A, -2 * cw, 1 - alpha / A]
    b = [v / a[0] for v in b]
    a = [v / a[0] for v in a]
    y = np.zeros_like(x)
    x1 = x2 = y1 = y2 = 0.0
    for i, xi in enumerate(x):
        yi = b[0] * xi + b[1] * x1 + b[2] * x2 - a[1] * y1 - a[2] * y2
        y[i] = yi
        x2, x1, y2, y1 = x1, xi, y1, yi
    return y


def env(n, attack, decay):
    t = np.arange(n) / SR
    return np.minimum(1.0, t / max(attack, 1e-4)) * np.exp(-t / decay)


def norm(x, peak):
    m = np.max(np.abs(x)) or 1.0
    return x / m * peak


def tone(f, sec, decay, phase=0.0):
    t = np.arange(int(sec * SR)) / SR
    return np.sin(2 * np.pi * f * t + phase) * np.exp(-t / decay)


def chirp(f0, f1, sec, decay):
    n = int(sec * SR)
    t = np.arange(n) / SR
    f = f0 * (f1 / f0) ** (t / sec)
    return np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t / decay)


def pad(x, sec):
    return np.concatenate([x, np.zeros(int(sec * SR))])


def mixat(out, x, at):
    a = int(at * SR)
    b = min(len(out), a + len(x))
    if a < len(out):
        out[a:b] += x[: b - a]


def loopify(y, sec, xf=0.25):
    n, k = int(sec * SR), int(xf * SR)
    loop = y[:n].copy()
    loop[:k] = loop[:k] * np.linspace(0, 1, k) + y[n:n + k] * np.linspace(1, 0, k)
    return loop


def chops():
    for i in range(6):
        n = int(0.22 * SR)
        crunch = biquad(noise(0.22), "bandpass", 2600 + rng.random() * 1500, q=0.9) * env(n, 0.002, 0.025)
        body = tone(380 + rng.random() * 120, 0.22, 0.03) + 0.5 * tone(820 + rng.random() * 200, 0.22, 0.018)
        click = biquad(noise(0.22), "highpass", 4000) * env(n, 0.0005, 0.004)
        x = 0.7 * crunch + 0.5 * body + 0.4 * click
        write(f"chop_{i}", norm(x, 0.8))
    for i in range(3):
        n = int(0.2 * SR)
        body = tone(300 + rng.random() * 60, 0.2, 0.04) + 0.6 * tone(690 + rng.random() * 80, 0.2, 0.02)
        click = biquad(noise(0.2), "bandpass", 3200, q=1.5) * env(n, 0.0005, 0.006)
        write(f"thunk_{i}", norm(body + 0.6 * click, 0.7))


def logs():
    for i in range(3):
        n = int(0.6 * SR)
        thud = tone(95 + rng.random() * 30, 0.6, 0.09) + 0.6 * tone(210 + rng.random() * 40, 0.6, 0.05)
        knock = biquad(noise(0.6), "bandpass", 700, q=1.2) * env(n, 0.001, 0.03)
        x = thud + knock
        for k in range(5):  # embers stirred up
            mixat(x, biquad(noise(0.02), "highpass", 2500) * env(int(0.02 * SR), 0.0005, 0.004) * 0.6, 0.08 + rng.random() * 0.4)
        write(f"log_{i}", norm(x, 0.75))


def splashes():
    for i in range(4):
        out = np.zeros(int(0.45 * SR))
        mixat(out, chirp(300 + rng.random() * 100, 900 + rng.random() * 300, 0.08, 0.03) * 0.8, 0.0)
        n = int(0.35 * SR)
        mixat(out, biquad(noise(0.35), "bandpass", 1600, q=0.8) * env(n, 0.003, 0.06) * 0.6, 0.01)
        for k in range(3):
            mixat(out, chirp(600 + rng.random() * 400, 1400 + rng.random() * 600, 0.04, 0.015) * 0.3, 0.06 + rng.random() * 0.2)
        write(f"splash_{i}", norm(out, 0.7))


def hisses():
    for i in range(3):
        n = int(0.9 * SR)
        x = biquad(noise(0.9), "highpass", 3000) * env(n, 0.01, 0.25)
        for k in range(8):
            mixat(x, biquad(noise(0.01), "highpass", 2000) * env(int(0.01 * SR), 0.0005, 0.003) * 1.5, rng.random() * 0.6)
        write(f"hiss_{i}", norm(x, 0.55))


def stirs():
    for i in range(4):
        sec = 0.5
        n = int(sec * SR)
        t = np.arange(n) / SR
        x = biquad(noise(sec), "bandpass", 1100 + rng.random() * 400, q=2.5)
        x += 0.4 * biquad(noise(sec), "bandpass", 2600, q=4)
        g = np.sin(np.pi * np.minimum(1, t / sec)) ** 1.5 * (0.7 + 0.3 * np.sin(2 * np.pi * 9 * t))
        write(f"stir_{i}", norm(x * g, 0.4))


def salts():
    for i in range(3):
        out = np.zeros(int(0.6 * SR))
        for k in range(60):
            g = biquad(noise(0.006), "highpass", 5000) * env(int(0.006 * SR), 0.0003, 0.0015)
            mixat(out, g * (0.3 + rng.random()), rng.random() ** 1.5 * 0.5)
        write(f"salt_{i}", norm(out, 0.45))


def kneads():
    for i in range(4):
        out = np.zeros(int(0.5 * SR))
        n = int(0.3 * SR)
        squish = biquad(noise(0.3), "lowpass", 900) * env(n, 0.03, 0.08)
        mixat(out, squish, 0.0)
        slap = biquad(noise(0.08), "bandpass", 500, q=0.8) * env(int(0.08 * SR), 0.001, 0.02) + tone(140, 0.08, 0.02)
        mixat(out, slap * 0.9, 0.12 + rng.random() * 0.05)
        write(f"knead_{i}", norm(out, 0.6))


def cranks():
    for i in range(3):
        n = int(0.18 * SR)
        x = biquad(noise(0.18), "bandpass", 520 + rng.random() * 200, q=6) * env(n, 0.004, 0.05)
        x += 0.5 * tone(1900 + rng.random() * 300, 0.18, 0.01)
        write(f"crank_{i}", norm(x, 0.45))


def crackles():
    for i in range(6):
        n = int(0.06 * SR)
        x = biquad(noise(0.06), "bandpass", 1500 + rng.random() * 3000, q=1.0) * env(n, 0.0003, 0.006 + rng.random() * 0.01)
        write(f"crackle_{i}", norm(x, 0.5 + rng.random() * 0.3))


def doors():
    for name, f0, f1 in (("door_open", 300, 520), ("door_close", 520, 260)):
        sec = 0.7
        n = int(sec * SR)
        t = np.arange(n) / SR
        scrape = np.zeros(n)
        for s in range(0, n, 480):
            f = f0 * (f1 / f0) ** (s / n)
            scrape[s:s + 480] = biquad(noise(0.02)[:min(480, n - s) + 400], "bandpass", f, q=3)[-min(480, n - s):]
        g = np.sin(np.pi * np.minimum(1, t / sec))
        x = scrape * g * 0.6
        thud = tone(110, 0.3, 0.06) + biquad(noise(0.3), "lowpass", 600) * env(int(0.3 * SR), 0.001, 0.05)
        mixat(x, thud * (1.0 if name == "door_close" else 0.4), sec - 0.12)
        write(name, norm(pad(x, 0.2), 0.6))
    sec = 0.6
    n = int(sec * SR)
    t = np.arange(n) / SR
    x = biquad(noise(sec), "bandpass", 1300, q=1.4) * np.sin(np.pi * t / sec) ** 2
    write("peel", norm(x, 0.45))


def pours():
    sec = 1.1
    n = int(sec * SR)
    t = np.arange(n) / SR
    x = biquad(noise(sec), "bandpass", 700, q=1.0) * (0.6 + 0.4 * np.sin(2 * np.pi * 7 * t)) * np.minimum(1, t / 0.08) * np.minimum(1, (sec - t) / 0.2)
    for k in range(10):
        mixat(x, chirp(400 + rng.random() * 300, 800 + rng.random() * 500, 0.05, 0.02) * 0.5, rng.random() * (sec - 0.1))
    write("pour", norm(x, 0.5))
    n = int(0.5 * SR)
    write("flour", norm(biquad(noise(0.5), "lowpass", 1400) * env(n, 0.02, 0.12), 0.35))


def results():
    # done: a warm, soft bell; burnt: a dull low thud with a fizz
    sec = 2.4
    x = np.zeros(int(sec * SR))
    for f, a, d in ((523.25, 1.0, 0.9), (784.0, 0.5, 0.7), (1046.5, 0.35, 0.5), (1568.0, 0.12, 0.3)):
        mixat(x, tone(f, sec, d) * a, 0.0)
    for f, a, d in ((659.25, 0.7, 0.8), (987.8, 0.3, 0.5)):
        mixat(x, tone(f, sec, d) * a, 0.18)
    write("done", norm(x, 0.45))
    y = tone(80, 1.2, 0.25) + 0.4 * biquad(noise(1.2), "highpass", 3000) * env(int(1.2 * SR), 0.05, 0.35)
    write("burnt", norm(y, 0.5))


def loops():
    # the fire: a low roar with crackles
    sec = 4.0
    base = biquad(biquad(noise(sec + 0.3), "lowpass", 350), "highpass", 60) * 0.8
    x = base.copy()
    for k in range(70):
        n = int(0.05 * SR)
        c = biquad(noise(0.05), "bandpass", 1200 + rng.random() * 3500, q=1.0) * env(n, 0.0003, 0.004 + rng.random() * 0.012)
        mixat(x, c * (0.8 + rng.random() * 1.6), rng.random() * (sec + 0.25))
    write("loop_fire", norm(loopify(x, sec), 0.6))
    # a simmer: slow bubbles; a boil: a lot of them
    for name, count, low in (("loop_simmer", 45, 0.25), ("loop_boil", 160, 0.5)):
        x = biquad(noise(sec + 0.3), "lowpass", 500) * low
        for k in range(count):
            f0 = 250 + rng.random() * 350
            mixat(x, chirp(f0, f0 * (2.0 + rng.random()), 0.05 + rng.random() * 0.04, 0.02) * (0.4 + rng.random() * 0.6), rng.random() * (sec + 0.25))
        write(name, norm(loopify(x, sec), 0.55))
    # meat on the spit: a steady sizzle with spits of fat
    x = biquad(noise(sec + 0.3), "highpass", 2500) * 0.5
    x *= 0.75 + 0.25 * np.sin(2 * np.pi * 0.7 * np.arange(len(x)) / SR)
    for k in range(90):
        n = int(0.012 * SR)
        mixat(x, biquad(noise(0.012), "highpass", 1800) * env(n, 0.0003, 0.003) * 2.0, rng.random() * (sec + 0.25))
    write("loop_sizzle", norm(loopify(x, sec), 0.5))


def main():
    os.makedirs(OUT, exist_ok=True)
    chops()
    logs()
    splashes()
    hisses()
    stirs()
    salts()
    kneads()
    cranks()
    doors()
    pours()
    results()
    loops()
    print("wrote", len([f for f in os.listdir(OUT) if f.endswith(".wav")]), "files to", OUT)


if __name__ == "__main__":
    main()
