#!/usr/bin/env python3
"""Генератор ассетов KOJIMA PACK (v0.3).

Рисует попиксельно (без внешних картинок — всё оригинальное, "по мотивам"):
  1. textures/item/totem_of_undying.png — BB-капсула: янтарное стекло,
     младенец внутри, металлический корпус с синей лампой и лампой статуса.
     16x16 x4 кадра (стрип 16x64). Кадры: пузырёк всплывает вверх,
     лампа статуса зелёный -> жёлтый -> оранжевый -> красный.
  2. textures/item/echo_shard.png — хиральная рука: 4 золотых
     пальца-кристалла на тёмном камне. 16x16 x4 кадра.
     Кадры: бегущий блик по пальцам + мерцающие искры.
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
def bb_frame(status_color, bubble):
    px = Image.new("RGBA", (16, 16), T)
    P = px.load()
    OUT_G = (70, 40, 12, 255)     # контур стекла
    AMBER_D = (190, 110, 30, 255)  # янтарь край
    AMBER = (245, 165, 60, 255)   # янтарь
    AMBER_L = (255, 205, 120, 255)  # янтарь свечение
    HIGH = (255, 235, 180, 255)   # блик стекла
    BABY = (255, 228, 195, 255)   # младенец
    BABY_D = (232, 175, 135, 255)  # младенец тень
    OUT_M = (45, 32, 20, 255)     # контур корпуса
    TAN = (198, 155, 100, 255)    # корпус
    TAN_D = (150, 112, 70, 255)
    TAN_L = (225, 185, 130, 255)
    BLUE = (150, 215, 255, 255)   # синяя лампа
    BLUE_W = (235, 250, 255, 255)
    BUB = (255, 242, 215, 255)    # пузырёк

    # --- янтарный купол: маска по строкам
    glass_rows = {
        1: (5, 10), 2: (4, 11),
        3: (3, 12), 4: (3, 12), 5: (3, 12),
        6: (3, 12), 7: (3, 12), 8: (3, 12), 9: (3, 12),
        10: (4, 11),
    }
    mask = set()
    for y, (x0, x1) in glass_rows.items():
        for x in range(x0, x1 + 1):
            mask.add((x, y))
    for (x, y) in mask:
        if (x - 1, y) not in mask or (x + 1, y) not in mask \
                or (x, y - 1) not in mask or (x, y + 1) not in mask:
            P[x, y] = OUT_G
        elif x <= 4 or x >= 11:
            P[x, y] = AMBER_D
        elif 7 <= x <= 9:
            P[x, y] = AMBER_L
        else:
            P[x, y] = AMBER
    # блик слева
    for y in range(3, 9):
        P[4, y] = HIGH
    P[5, 2] = HIGH

    # --- младенец: голова + тельце
    for x, y in ((7, 3), (8, 3), (7, 4), (8, 4)):
        P[x, y] = BABY
    P[8, 4] = BABY_D
    for x in range(6, 10):
        for y in (6, 7):
            P[x, y] = BABY
    P[6, 7] = P[9, 7] = BABY_D
    P[9, 5] = BABY_D  # ручка
    P[6, 8] = P[7, 8] = BABY_D  # ножки (поджаты)

    # --- пузырёк (всплывает по кадрам)
    bx, by = bubble
    P[bx, by] = BUB

    # --- металлический корпус x2..13, y11..14
    for x in range(2, 14):
        for y in range(11, 15):
            P[x, y] = TAN
    for x in range(2, 14):
        P[x, 11] = OUT_M if x in (2, 13) else TAN_L
        P[x, 14] = OUT_M
    for y in range(11, 15):
        P[2, y] = OUT_M
        P[13, y] = OUT_M
    # заклёпки
    for x, y in ((3, 12), (12, 12), (3, 13), (12, 13)):
        P[x, y] = OUT_M
    # тень корпуса
    for x in range(4, 12):
        P[x, 13] = TAN_D
    # синяя лампа 2x2
    P[4, 12] = BLUE_W
    P[5, 12] = BLUE
    P[4, 13] = BLUE
    P[5, 13] = BLUE
    # лампа статуса BB 2x2 (цвет зависит от кадра)
    for x in (10, 11):
        for y in (12, 13):
            P[x, y] = status_color
    return px


def make_totem():
    statuses = [
        (96, 255, 150, 255),   # зелёный — BB спокоен
        (255, 235, 96, 255),   # жёлтый
        (255, 157, 46, 255),   # оранжевый
        (255, 72, 72, 255),    # красный — BB в стрессе!
    ]
    bubbles = [(11, 9), (11, 7), (10, 5), (10, 3)]
    strip = Image.new("RGBA", (16, 64), T)
    for f in range(4):
        frame = bb_frame(statuses[f], bubbles[f])
        strip.paste(frame, (0, f * 16))
    out = os.path.join(ITEM, "totem_of_undying.png")
    strip.save(out)
    print("wrote", out, strip.size)


# ------------------------------------------------------------- хиральная рука
def chiral_frame(sparkle, glint, lit_finger):
    px = Image.new("RGBA", (16, 16), T)
    P = px.load()
    EDGE = (110, 75, 25, 255)
    TIP = (255, 240, 200, 255)
    ROCK = (55, 50, 58, 255)
    ROCK_L = (88, 80, 90, 255)
    WHITE = (255, 252, 240, 255)

    # пальцы-кристаллы (разрывы между ними — фон)
    fingers = [
        (2, 3, 5, 10),    # указательный
        (5, 6, 2, 10),    # средний (самый высокий)
        (8, 9, 4, 10),    # безымянный
        (11, 12, 6, 10),  # мизинец
    ]
    palm = (2, 12, 10, 12)
    mask = set()
    for (x0, x1, y0, y1) in fingers + [palm]:
        for x in range(x0, x1 + 1):
            for y in range(y0, y1 + 1):
                mask.add((x, y))

    def gold(x, y, left):
        if y <= 4:
            return (255, 220, 140, 255) if left else (235, 185, 90, 255)
        if y <= 8:
            return (240, 185, 85, 255) if left else (205, 145, 55, 255)
        return (210, 150, 60, 255) if left else (170, 115, 40, 255)

    # левая граница каждого пальца (для светотени)
    lefts = {2, 5, 8, 11}
    for (x, y) in mask:
        if (x, y - 1) not in mask and y <= 6:
            P[x, y] = TIP  # кончики кристаллов
        elif (x - 1, y) not in mask or (x + 1, y) not in mask \
                or (x, y - 1) not in mask or (x, y + 1) not in mask:
            P[x, y] = EDGE  # внешний контур (включая низ ладони)
        else:
            P[x, y] = gold(x, y, x in lefts)

    # бегущий блик: один палец за кадр светится ярче
    fx0, fx1, fy0, fy1 = fingers[lit_finger]
    for y in range(fy0, fy1 + 1):
        if P[fx0, y] != TIP:
            P[fx0, y] = (255, 235, 170, 255)

    # тёмное каменное основание
    for x in range(1, 14):
        P[x, 13] = ROCK
    for x in range(2, 13):
        P[x, 14] = ROCK
    for x in range(4, 11):
        P[x, 15] = ROCK
    for x, y in ((3, 13), (7, 13), (11, 13), (5, 14), (9, 14)):
        P[x, y] = ROCK_L

    # мерцающая искра-крест + одиночный блик
    sx, sy = sparkle
    for dx, dy in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1)):
        P[sx + dx, sy + dy] = WHITE
    gx, gy = glint
    P[gx, gy] = WHITE
    return px


def make_chiral():
    sparkles = [(6, 5), (9, 7), (3, 8), (6, 9)]
    glints = [(11, 9), (3, 6), (8, 6), (12, 11)]
    strip = Image.new("RGBA", (16, 64), T)
    for f in range(4):
        strip.paste(chiral_frame(sparkles[f], glints[f], f), (0, f * 16))
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
