"""VALORANT kill feed data collector.

Captures three regions per frame stacked vertically into one 128×96 image:

  rows  0-31: kill feed      (top-right quadrant)  — all kills, teammates too
  rows 32-63: kill banner    (bottom center)        — local player OR spectated
  rows 64-95: spectator HUD  (bottom center strip)  — visible ONLY when spectating

The model learns to combine all three. Kill banner lit + no spectator HUD = your
kill. Kill banner lit + spectator HUD visible = spectating a teammate (ignore).

When spectating in VALORANT, a portrait/nameplate of the spectated player appears
as a distinct UI element (below the kill banner area). When alive this region is
empty game world. The blurred color pattern differs enough to be a useful signal.

NOTE: The spectator HUD region (oy + sh*0.91, centered) is a starting estimate —
verify in-game and adjust if the portrait appears at a different screen position.

Setup:
    pip install mss pillow keyboard

Usage:
    1. Launch VALORANT, get into a deathmatch.
    2. Run:  python collect.py
    3. Press K on every kill or death YOU are involved in (not while spectating).
    4. Let the script run during spectating phases — those frames auto-save
       as negatives (kill banner may be lit but spectator HUD will also be lit).
    5. Press Q to quit.

Aim for 300+ positives, 600+ negatives before training.
"""

import mss
import keyboard
import time
from pathlib import Path
from PIL import Image, ImageFilter

# ── Model input ───────────────────────────────────────────────────────────────
MODEL_W   = 128
MODEL_H   = 96    # three 32px strips stacked
STRIP_H   = MODEL_H // 3   # 32

# ── Preprocessing ─────────────────────────────────────────────────────────────
BLUR_SIGMA = 8

# ── Collection params ─────────────────────────────────────────────────────────
KILL_WINDOW_S    = 2.5
NEGATIVE_EVERY_S = 1.5
CAPTURE_EVERY_S  = 0.1

# ── Paths ─────────────────────────────────────────────────────────────────────
POS = Path("training_data/positive")
NEG = Path("training_data/negative")
POS.mkdir(parents=True, exist_ok=True)
NEG.mkdir(parents=True, exist_ok=True)


def get_regions(monitor: dict) -> tuple[dict, dict, dict]:
    sw, sh = monitor["width"], monitor["height"]
    ox, oy = monitor["left"], monitor["top"]

    killfeed = {                        # top-right quadrant
        "top":    oy,
        "left":   ox + sw // 2,
        "width":  sw // 2,
        "height": sh // 2,
    }
    killbanner = {                      # bottom-center (kill confirmation)
        "top":    oy + int(sh * 0.78),
        "left":   ox + sw // 4,
        "width":  sw // 2,
        "height": int(sh * 0.10),
    }
    spectator = {                       # spectator portrait (only visible when spectating)
        "top":    oy + int(sh * 0.91),
        "left":   ox + sw // 4,
        "width":  sw // 2,
        "height": int(sh * 0.06),
    }
    return killfeed, killbanner, spectator


def grab_strip(sct, region: dict) -> Image.Image:
    raw = sct.grab(region)
    img = Image.frombytes("RGB", raw.size, raw.bgra, "raw", "BGRX")
    img = img.filter(ImageFilter.GaussianBlur(radius=BLUR_SIGMA))
    img = img.resize((MODEL_W, STRIP_H), Image.LANCZOS)
    return img


def capture_stack(sct, r_feed, r_banner, r_spectator) -> Image.Image:
    stacked = Image.new("RGB", (MODEL_W, MODEL_H))
    stacked.paste(grab_strip(sct, r_feed),      (0, 0))
    stacked.paste(grab_strip(sct, r_banner),    (0, STRIP_H))
    stacked.paste(grab_strip(sct, r_spectator), (0, STRIP_H * 2))
    return stacked


def main():
    pos_n = len(list(POS.glob("*.png")))
    neg_n = len(list(NEG.glob("*.png")))

    with mss.mss() as sct:
        monitor                    = sct.monitors[1]
        r_feed, r_banner, r_spectator = get_regions(monitor)
        sw, sh                        = monitor["width"], monitor["height"]

        print(f"Screen {sw}×{sh}")
        print(f"  Kill feed:    {r_feed}")
        print(f"  Kill banner:  {r_banner}")
        print(f"  Spectator HUD:{r_spectator}")
        print(f"Starting — {pos_n} pos / {neg_n} neg already collected.")
        print("  K = your kill or death    Q = quit\n")

        last_kill     = 0.0
        last_neg_save = time.time()

        while True:
            if keyboard.is_pressed("q"):
                break

            now = time.time()
            img = capture_stack(sct, r_feed, r_banner, r_spectator)

            if keyboard.is_pressed("k"):
                last_kill = now

            in_window = (now - last_kill) < KILL_WINDOW_S

            if in_window:
                img.save(POS / f"{pos_n:06d}.png")
                pos_n += 1
            elif now - last_neg_save >= NEGATIVE_EVERY_S:
                img.save(NEG / f"{neg_n:06d}.png")
                neg_n += 1
                last_neg_save = now

            print(f"\r  pos={pos_n:4d}  neg={neg_n:4d}", end="", flush=True)
            time.sleep(CAPTURE_EVERY_S)

    print(f"\nDone — {pos_n} positive, {neg_n} negative.")


if __name__ == "__main__":
    main()
