#!/usr/bin/env python3
"""Генератор демо-ассетов KOJIMA PACK.

Рисует попиксельно (без внешних картинок — всё оригинальное, "по мотивам"):
  1. textures/item/totem_of_undying.png — BB-капсула, 16x16 x4 кадра (стрип 16x64).
     Кадры: сканирующая линия идёт вниз, лампочка статуса BB
     зелёный -> жёлтый -> оранжевый -> красный (нарастание стресса BB).
  2. textures/item/echo_shard.png — хиралиевый кристалл, 16x16 x4 кадра.
     Кадры: переливающийся блик в 4 позициях.
  3. pack.png — иконка пака 64x64.

Запуск:  python3 tools/make_demo_assets.py
"""
from PIL import Image
import os

HERE = os.path.dirname(os.path.abspath(__file__))
RP = os.path.join(HERE, "..", "resourcepack")
ITEM = os.path.join(RP, "assets", "minecraft", "textures", "item")

T = (0, 0, 0, 0)  # transparent


# ---------------------------------------------------------------- BB-капсула
def bb_frame(status_color, scan_y):
    px = Image.new("RGBA", (16, 16), T)
    P = px.load()
    OUT = (16, 20, 34, 255)      # контур
    SHELL_D = (36, 46, 66, 255)  # корпус тёмный
    SHELL_M = (62, 78, 108, 255)  # корпус светлый
    GLASS = (150, 186, 208, 255)  # стекло
    GLASS_L = (205, 228, 242, 255)  # блик стекла
    GLASS_D = (110, 138, 162, 255)  # тень стекла
    BABY = (242, 201, 155, 255)  # силуэт BB
    BABY_D = (206, 152, 112, 255)
    ORANGE = (255, 157, 46, 255)  # скан-линия
    ORANGE_B = (255, 214, 130, 255)
    ORANGE_D = (150, 80, 20, 255)

    # --- боковые баки (ручки капсулы)
    for x in (3, 12):
        for y in range(6, 10):
            P[x, y] = SHELL_D
        P[x, 5] = OUT
        P[x, 10] = OUT

    # --- корпус капсулы x5..10, y1..14 (скруглённые углы)
    for x in range(5, 11):
        for y in range(1, 15):
            if (x, y) in ((5, 1), (10, 1), (5, 14), (10, 14)):
                continue
            P[x, y] = SHELL_M
    # контур
    for x in range(6, 10):
        P[x, 1] = OUT
        P[x, 14] = OUT
    for y in range(2, 14):
        P[5, y] = OUT
        P[10, y] = OUT
    P[5, 1] = P[10, 1] = P[5, 14] = P[10, 14] = T
    # верхняя крышка
    P[6, 1] = P[9, 1] = SHELL_D
    P[7, 1] = P[8, 1] = ORANGE_D

    # --- стекло x6..9, y2..13
    for x in range(6, 10):
        for y in range(2, 14):
            P[x, y] = GLASS
    for y in range(2, 14):
        P[6, y] = GLASS_L   # блик слева
        P[9, y] = GLASS_D   # тень справа

    # --- силуэт BB: голова 2x2 + тело
    for x in (7, 8):
        P[x, 5] = BABY
        P[x, 6] = BABY
        P[x, 8] = BABY
        P[x, 9] = BABY
    P[7, 7] = BABY_D
    P[8, 7] = BABY
    P[7, 10] = P[8, 10] = BABY_D
    P[7, 4] = GLASS_L  # макушка-блик

    # --- скан-линия (движется по кадрам) + шлейф
    for x in range(6, 10):
        P[x, scan_y] = ORANGE
        if scan_y - 1 >= 2:
            P[x, scan_y - 1] = ORANGE_D
    P[9, scan_y] = ORANGE_B

    # --- нижняя панель + лампочка статуса BB
    for x in range(6, 10):
        P[x, 13] = SHELL_D
    P[7, 13] = P[8, 13] = status_color
    return px


def make_totem():
    statuses = [
        (96, 255, 150, 255),   # зелёный — BB спокоен
        (255, 235, 96, 255),   # жёлтый
        (255, 157, 46, 255),   # оранжевый
        (255, 72, 72, 255),    # красный — BB в стрессе!
    ]
    strip = Image.new("RGBA", (16, 64), T)
    for f in range(4):
        frame = bb_frame(statuses[f], scan_y=3 + f * 3)
        strip.paste(frame, (0, f * 16))
    out = os.path.join(ITEM, "totem_of_undying.png")
    strip.save(out)
    print("wrote", out, strip.size)


# ------------------------------------------------------- хиралиевый кристалл
def chiral_frame(sparkle):
    px = Image.new("RGBA", (16, 16), T)
    P = px.load()
    EDGE = (58, 44, 18, 255)
    GOLD_L = (255, 208, 110, 255)
    GOLD = (232, 164, 60, 255)
    GOLD_D = (160, 104, 32, 255)
    TAR = (12, 10, 16, 255)  # смоляное основание

    # маска кристалла (ромбовидный осколок)
    rows = {
        1: (7, 8),
        2: (7, 8),
        3: (6, 9),
        4: (6, 9),
        5: (5, 10),
        6: (5, 10),
        7: (5, 10),
        8: (6, 9),
        9: (6, 9),
        10: (7, 8),
        11: (7, 8),
    }
    mask = set()
    for y, (x0, x1) in rows.items():
        for x in range(x0, x1 + 1):
            mask.add((x, y))
    for (x, y) in mask:
        # край = контур
        if (x - 1, y) not in mask or (x + 1, y) not in mask \
                or (x, y - 1) not in mask or (x, y + 1) not in mask:
            P[x, y] = EDGE
        elif x <= 7:
            P[x, y] = GOLD_L if y < 6 else GOLD
        else:
            P[x, y] = GOLD if y < 7 else GOLD_D
    # блик-грань
    P[6, 5] = P[6, 6] = (255, 236, 190, 255)

    # смоляная капля снизу (BT-смола)
    for x, y in ((6, 12), (7, 12), (8, 12), (9, 12),
                 (7, 13), (8, 13), (7, 14)):
        P[x, y] = TAR
    P[8, 12] = (40, 32, 52, 255)  # блик смолы

    # переливающаяся искра (позиция зависит от кадра)
    sx, sy = sparkle
    for dx, dy in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1)):
        P[sx + dx, sy + dy] = (255, 255, 255, 255)
    P[sx, sy] = (255, 246, 220, 255)
    return px


def make_chiral():
    spots = [(3, 3), (12, 4), (3, 11), (12, 10)]
    strip = Image.new("RGBA", (16, 64), T)
    for f in range(4):
        strip.paste(chiral_frame(spots[f]), (0, f * 16))
    out = os.path.join(ITEM, "echo_shard.png")
    strip.save(out)
    print("wrote", out, strip.size)


# ------------------------------------------------------------- иконка пака
def make_pack_icon():
    S = 64
    img = Image.new("RGBA", (S, S), (11, 14, 24, 255))
    P = img.load()
    # вертикальный градиент фона
    for y in range(S):
        t = y / (S - 1)
        P_bg = (int(11 + 14 * t), int(14 + 16 * t),
                int(24 + 26 * t), 255)
        for x in range(S):
            P[x, y] = P_bg
    GOLD = (232, 178, 70, 255)
    GOLD_D = (120, 88, 30, 255)
    ORANGE = (255, 157, 46, 255)
    # диагональная "нить" (strand) с тёмной окантовкой
    for i in range(-S, 2 * S):
        x = i
        y = S - 1 - i
        for w in range(-3, 4):
            xx, yy = x + w, y
            if 0 <= xx < S and 0 <= yy < S:
                P[xx, yy] = GOLD_D if abs(w) == 3 else GOLD
    # тонкая оранжевая параллель
    for i in range(-S, 2 * S):
        x = i
        y = S - 9 - i
        if 0 <= x < S and 0 <= y < S:
            P[x, y] = ORANGE
    # 4 лампочки статуса BB внизу слева
    dots = [(96, 255, 150, 255), (255, 235, 96, 255),
            (255, 157, 46, 255), (255, 72, 72, 255)]
    for i, c in enumerate(dots):
        for dx in range(4):
            for dy in range(4):
                P[6 + i * 7 + dx, 54 + dy] = c
    out = os.path.join(RP, "pack.png")
    img.save(out)
    print("wrote", out, img.size)


if __name__ == "__main__":
    os.makedirs(ITEM, exist_ok=True)
    make_totem()
    make_chiral()
    make_pack_icon()
    print("done")
