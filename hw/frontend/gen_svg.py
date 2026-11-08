#!/usr/bin/env python3
"""Генератор схемы входной платы-переходника анализатора S9 (SVG, без KiCad).

    python3 hw/frontend/gen_svg.py  →  hw/frontend/s9la_frontend.svg
"""
import os

W, H = 1700, 1160
out = []
FONT = "font-family='DejaVu Sans, Noto Sans, sans-serif'"
MONO = "font-family='DejaVu Sans Mono, Noto Sans Mono, monospace'"
INK, RED, BLUE, GRN, GREY = "#1a1a1a", "#c0392b", "#1f5fa8", "#1e7d3a", "#777"


def t(x, y, s, size=13, anchor="start", color=INK, weight="normal", mono=False):
    s = s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    out.append(f"<text x='{x}' y='{y}' font-size='{size}' text-anchor='{anchor}' fill='{color}' "
               f"font-weight='{weight}' {MONO if mono else FONT}>{s}</text>")


def ln(x1, y1, x2, y2, color=INK, w=1.6, dash=None):
    d = f" stroke-dasharray='{dash}'" if dash else ""
    out.append(f"<line x1='{x1}' y1='{y1}' x2='{x2}' y2='{y2}' stroke='{color}' stroke-width='{w}'{d}/>")


def poly(pts, color=INK, w=1.6):
    p = " ".join(f"{x},{y}" for x, y in pts)
    out.append(f"<polyline points='{p}' fill='none' stroke='{color}' stroke-width='{w}'/>")


def box(x, y, w, h, color=INK, fill="#fff", w_=1.8, rx=3):
    out.append(f"<rect x='{x}' y='{y}' width='{w}' height='{h}' rx='{rx}' fill='{fill}' "
               f"stroke='{color}' stroke-width='{w_}'/>")


def dot(x, y, color=INK):
    out.append(f"<circle cx='{x}' cy='{y}' r='3.2' fill='{color}'/>")


def res_h(x, y, label, val, color=INK):
    """Горизонтальный резистор (прямоугольник 36×12) с центром слева x..x+36."""
    box(x, y - 6, 36, 12, color, "#fff", 1.5, 1)
    t(x + 18, y - 10, f"{label} {val}", 10, "middle", GREY)


def res_v(x, y, label, val):
    """Вертикальный резистор от y до y+30."""
    box(x - 6, y, 12, 30, INK, "#fff", 1.5, 1)
    t(x + 10, y + 19, f"{label} {val}", 10, "start", GREY)


def gnd(x, y):
    ln(x, y, x, y + 6)
    ln(x - 9, y + 6, x + 9, y + 6, w=2)
    ln(x - 6, y + 10, x + 6, y + 10, w=1.6)
    ln(x - 3, y + 14, x + 3, y + 14, w=1.4)


def vcc(x, y, name="+3.3V"):
    ln(x, y, x, y - 8)
    ln(x - 8, y - 8, x + 8, y - 8, color=RED, w=2)
    t(x, y - 13, name, 11, "middle", RED, "bold")


out.append(f"<svg xmlns='http://www.w3.org/2000/svg' width='{W}' height='{H}' viewBox='0 0 {W} {H}'>")
out.append(f"<rect width='{W}' height='{H}' fill='#fbfbf8'/>")

# ---------------- Заголовок ----------------
t(40, 42, "S9LA — входная плата-переходник (одна группа из четырёх: 8 каналов)", 22, weight="bold")
t(40, 66, "Панель → защита (100 Ом + TVS + 100 кОм) → буфер SN74LVC245A (3.3 В, входы терпят 5 В) → шлейф IDC → разъёмы хешплат S9. "
           "Группы 2–4 одинаковые, см. таблицу внизу.", 13, color=GREY)

# ---------------- Геометрия рядов ----------------
Y0, DY = 175, 58
GAP = 70   # промежуток между четвёрками (разъёмы J1 и J2)
rows = [Y0 + i * DY + (GAP if i >= 4 else 0) for i in range(8)]
X_P, X_R, X_N, X_ESD, X_U = 70, 215, 300, 360, 560
U_W = 170

# Разъём панели P1 (IDC 2×5)
box(X_P - 40, Y0 - 45, 80, rows[-1] - Y0 + 95, BLUE, "#eef4fb")
t(X_P, Y0 - 55, "P1 — на панели", 13, "middle", BLUE, "bold")
t(X_P, Y0 - 30, "IDC 2×5, 2.54 мм", 11, "middle", BLUE)
for i, y in enumerate(rows):
    t(X_P - 30, y + 4, f"{i + 1}", 12, mono=True, color=BLUE)
    t(X_P - 12, y + 4, f"CH{i}", 12, mono=True)
    ln(X_P + 40, y, X_R, y)
yg = rows[-1] + 40
t(X_P - 30, yg + 4, "9,10", 12, mono=True, color=BLUE)
t(X_P + 8, yg + 4, "GND", 12, mono=True)
ln(X_P + 40, yg, X_P + 70, yg)
gnd(X_P + 70, yg)

# Последовательные резисторы, узлы, подтяжки
for i, y in enumerate(rows):
    res_h(X_R, y, f"R{i + 1}", "100", )
    ln(X_R + 36, y, X_U, y)
    dot(X_N, y)
    if i == 0:
        # подробно — только у CH0, остальные такие же
        ln(X_N, y, X_N, y + 8)
        res_v(X_N, y + 8, "R9", "100k")
        gnd(X_N, y + 38)
    dot(X_ESD + 40, y)

t(X_R - 20, rows[-1] + 40, "R10…R16 (100k на землю) —", 10, color=GREY)
t(X_R - 20, rows[-1] + 53, "у каждого канала, как R9 у CH0", 10, color=GREY)

# TVS-сборки (по 4 линии)
for k in range(2):
    ytop = rows[4 * k] - 18
    hh = 3 * DY + 36
    bx = X_ESD + 70
    box(bx, ytop, 60, hh, GRN, "#eefaf1")
    t(bx + 30, ytop + hh / 2 - 6, f"U{2 + k}", 13, "middle", GRN, "bold")
    t(bx + 30, ytop + hh / 2 + 10, "TPD4E1U06", 9, "middle", GRN)
    t(bx + 30, ytop + hh / 2 + 22, "(TVS ×4)", 9, "middle", GRN)
    for j in range(4):
        y = rows[4 * k + j]
        ln(X_ESD + 40, y, X_ESD + 40, y)  # точка уже стоит
        poly([(X_ESD + 40, y), (X_ESD + 40, y + 12), (bx, y + 12)], GRN, 1.2)
    ln(bx + 30, ytop + hh, bx + 30, ytop + hh + 10, GRN)
    gnd(bx + 30, ytop + hh + 10)

# 74LVC245
uy = Y0 - 40
uh = rows[-1] - Y0 + 135
box(X_U, uy, U_W, uh, INK, "#fffdf2", 2.2)
t(X_U + U_W / 2, uy - 12, "U1  SN74LVC245A", 14, "middle", weight="bold")
for i, y in enumerate(rows):
    t(X_U + 8, y + 4, f"A{i + 1}", 12, mono=True)
    t(X_U - 6, y - 4, f"{i + 2}", 9, "end", GREY, mono=True)
    t(X_U + U_W - 8, y + 4, f"B{i + 1}", 12, "end", mono=True)
    t(X_U + U_W + 6, y - 4, f"{18 - i}", 9, "start", GREY, mono=True)
# DIR, /OE, VCC, GND
yd = uy + uh - 34
t(X_U + 8, yd + 4, "DIR", 12, mono=True)
t(X_U - 6, yd - 4, "1", 9, "end", GREY, mono=True)
poly([(X_U, yd), (X_U - 30, yd), (X_U - 30, yd - 18)], RED)
vcc(X_U - 30, yd - 18)
t(X_U - 40, yd + 4, "A→B", 10, "end", RED)
yo = uy + uh - 12
t(X_U + 8, yo + 4, "/OE", 12, mono=True)
t(X_U - 6, yo - 4, "19", 9, "end", GREY, mono=True)
ln(X_U, yo, X_U - 70, yo)
gnd(X_U - 70, yo)
# питание
xv = X_U + U_W / 2
t(xv - 30, uy + 18, "VCC 20", 11, "middle", RED, mono=True)
ln(xv - 30, uy, xv - 30, uy - 40, RED)
vcc(xv - 30, uy - 40)
t(xv + 35, uy + uh - 6, "GND 10", 11, "middle", mono=True)
ln(xv + 35, uy + uh, xv + 35, uy + uh + 8)
gnd(xv + 35, uy + uh + 8)
# конденсаторы
cx = X_U + U_W + 60
poly([(xv - 30, uy - 40), (cx + 130, uy - 40)], RED)
for k, (lab, val) in enumerate((("C1", "100n"), ("C2", "10µ"))):
    x = cx + 70 * k + 40
    ln(x, uy - 40, x, uy - 30, RED)
    ln(x - 9, uy - 30, x + 9, uy - 30, w=2.2)
    ln(x - 9, uy - 24, x + 9, uy - 24, w=2.2)
    ln(x, uy - 24, x, uy - 18)
    gnd(x, uy - 18)
    t(x, uy - 46, f"{lab} {val}", 10, "middle", GREY)
t(cx + 140, uy - 28, "C1 — вплотную к выв. 20", 10, "start", GREY)

# ---------------- Шлейфы к S9 ----------------
X_J = 1080
sig = [("RXD", 12), ("TXD", 11), ("RST", 15), ("PLUG", 5)]
for g in range(2):
    jy = rows[4 * g] - 36
    jh = 3 * DY + 100
    box(X_J, jy, 200, jh, BLUE, "#eef4fb")
    t(X_J + 100, jy - 10, f"J{'AB'[g]} → S9 J{g + 1} (шлейф IDC 18)", 13, "middle", BLUE, "bold")
    t(X_J + 100, jy + 15, "IDC 2×9, 2.54 мм", 10, "middle", BLUE)
    for j in range(4):
        i = 4 * g + j
        y = rows[i]
        name, pin = sig[j]
        poly([(X_U + U_W, y), (X_J, y)])
        t(X_J + 12, y + 4, f"{pin:>2}", 12, mono=True, color=BLUE)
        t(X_J + 40, y + 4, f"{name}", 12, mono=True)
        t(X_J + 104, y + 4, f"→ J{g + 1}_{name.replace('D', '') if name != 'RXD' else 'RX'}", 11, mono=True, color=GREY)
        t((X_U + U_W + X_J) / 2 + 40, y - 5, f"CH{i}", 10, "middle", GREY, mono=True)
    yv = rows[4 * g + 3] + 32
    t(X_J + 12, yv + 4, "16", 12, mono=True, color=BLUE)
    if g == 0:
        t(X_J + 40, yv + 4, "VCC +3.3V", 12, mono=True, color=RED)
    else:
        t(X_J + 40, yv + 4, "VCC — не подкл.", 12, mono=True, color=GREY)
    t(X_J + 12, yv + 24, "1,2,9,10,13,14", 11, mono=True, color=BLUE)
    t(X_J + 126, yv + 24, "GND", 12, mono=True)
    if g == 0:
        ln(X_J + 200, yv, X_J + 240, yv, RED)
        vcc(X_J + 240, yv)
    ln(X_J + 200, yv + 20, X_J + 260, yv + 20)
    gnd(X_J + 260, yv + 20)

t(X_J + 250, rows[0] - 2, "+3.3 В берём с S9", 11, color=RED)
t(X_J + 250, rows[0] + 13, "(выв. 16 разъёма хешплаты,", 11, color=RED)
t(X_J + 250, rows[0] + 28, "соединить только с ОДНОГО", 11, color=RED)
t(X_J + 250, rows[0] + 43, "шлейфа на группу)", 11, color=RED)
t(X_J + 250, rows[5] - 2, "Пины A0/A1/A2/EN/SCL/SDA", 11, color=GREY)
t(X_J + 250, rows[5] + 13, "шлейфа не подключать", 11, color=GREY)

# ---------------- Нижние таблицы ----------------
TY = rows[-1] + 190
ln(30, TY - 30, W - 30, TY - 30, GREY, 1, "4 4")


def table(x, y, title, head, rows_, colw):
    t(x, y, title, 15, weight="bold")
    y += 12
    rh = 21
    tw = sum(colw)
    box(x, y, tw, rh * (len(rows_) + 1), GREY, "#fff", 1, 2)
    out.append(f"<rect x='{x}' y='{y}' width='{tw}' height='{rh}' fill='#ececec'/>")
    for r in range(len(rows_) + 1):
        if r:
            ln(x, y + rh * r, x + tw, y + rh * r, "#ccc", 1)
        cx_ = x
        cells = head if r == 0 else rows_[r - 1]
        for c, wcol in enumerate(colw):
            t(cx_ + 6, y + rh * r + 15, str(cells[c]), 11.5, weight="bold" if r == 0 else "normal",
              mono=(r > 0 and c > 0))
            cx_ += wcol
    return y + rh * (len(rows_) + 1)


table(40, TY, "Группы", ["Разъём на панели", "Каналы", "Шлейфы к S9"],
      [["P1", "CH0–CH7", "J1, J2"], ["P2", "CH8–CH15", "J3, J4"],
       ["P3", "CH16–CH23", "J5, J6"], ["P4", "CH24–CH31", "J7, J8"],
       ["P5 (тест)", "меандр", "J9: TXD (11) через 100 Ом, GND"]],
      [150, 110, 260])

table(600, TY, "Перечень на всю плату (4 группы)", ["Поз.", "Кол-во", "Что"],
      [["U1 (×4)", "4", "SN74LVC245APWR (TSSOP-20) / …ADWR (SOIC-20)"],
       ["U2, U3 (×4)", "8", "TPD4E1U06DBVR (SOT-23-6) или 2× USBLC6-2SC6"],
       ["R1–R8 (×4)", "32", "100 Ом, 0603"],
       ["R9–R16 (×4)", "32", "100 кОм, 0603"],
       ["C1, C2 (×4)", "4+4", "100 нФ и 10 мкФ, 0603/0805"],
       ["P1–P4", "4", "IDC 2×5 «вилка» в корпусе (на панель)"],
       ["JA, JB (×4)", "8", "IDC 2×9 «вилка» в корпусе + шлейфы 18 жил"],
       ["P5", "1", "гнездо/штырь 2 конт. (тестовый выход + GND)"]],
      [130, 70, 380])

ny = TY + 4
t(1230, ny, "Зачем и как", 15, weight="bold")
notes = [
    "• 74LVC245 при VCC = 3.3 В: входы терпят до 5.5 В —",
    "  можно щупать 5-вольтовую логику напрямую.",
    "• Сгорит дешёвый буфер, а не Zynq: на S9 входы",
    "  идут на ПЛИС без защиты.",
    "• 100 кОм: неподключённый канал = 0, а не шум.",
    "• Задержка буфера ~4–6 нс, разброс между",
    "  каналами < 1 нс — для 100 МГц не важно.",
    "• Шлейфы до S9 — как можно короче (< 20 см),",
    "  земли шлейфа соединить все.",
    "• Не подавать > 5.5 В и < −0.5 В на входы:",
    "  для 12 В нужен делитель.",
]
for k, s in enumerate(notes):
    t(1230, ny + 24 + k * 19, s, 12)

out.append("</svg>")
here = os.path.dirname(os.path.abspath(__file__))
path = os.path.join(here, "s9la_frontend.svg")
open(path, "w").write("\n".join(out))
print(path)
