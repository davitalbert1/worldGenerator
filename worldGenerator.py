import numpy as np
import matplotlib.pyplot as plt
from matplotlib.patches import RegularPolygon
from matplotlib.collections import PatchCollection
from matplotlib.lines import Line2D
import matplotlib.colors as mcolors
import os
import heapq
import json
from collections import Counter
from datetime import datetime

#  PARÂMETROS DO MAPA
width = 100
height = 100
hex_size = 0.10
NUM_MAPAS  = 5
angulo_luz = np.radians(135)

BRIGHTNESS = 1.0 # 0.5 = escuro · 1.0 = padrão · 1.5 = claro

NUM_CIDADES = (5, 9)
NUM_VILAS   = (20, 35)

os.makedirs("mapas", exist_ok=True)
os.makedirs("json",  exist_ok=True)

#  BIOMAS
BIOMES = {
    "Oceano Profundo":      {"color": "#0c1e36", "h": (0.0,  0.2),  "r": (0.0,  0.4),  "score": -1.0, "cost": 999},
    "Oceano Raso":          {"color": "#1e4e72", "h": (0.0,  0.2),  "r": (0.4,  1.01), "score": -1.0, "cost": 999},
    "Praia":                {"color": "#b8a882", "h": (0.2,  0.25), "r": (0.0,  1.01), "score": 2.0,  "cost": 1},
    "Deserto":              {"color": "#a08050", "h": (0.3,  0.6),  "r": (0.0,  0.2),  "score": -1.0, "cost": 2},
    "Caatinga":             {"color": "#806040", "h": (0.3,  0.6),  "r": (0.2,  0.3),  "score": 1.0,  "cost": 1},
    "Cerrado":              {"color": "#6a7038", "h": (0.3,  0.6),  "r": (0.3,  0.45), "score": 3.0,  "cost": 1},
    "Planície":             {"color": "#507038", "h": (0.3,  0.6),  "r": (0.45, 0.6),  "score": 3.0,  "cost": 1},
    "Pantanal":             {"color": "#2e5048", "h": (0.3,  0.6),  "r": (0.6,  0.8),  "score": 1.0,  "cost": 3},
    "Floresta Tropical":    {"color": "#1e4820", "h": (0.3,  0.6),  "r": (0.8,  1.01), "score": 1.0,  "cost": 1},
    "Planalto Seco":        {"color": "#8a6840", "h": (0.6,  0.85), "r": (0.0,  0.2),  "score": 1.0,  "cost": 1},
    "Planície Árida":       {"color": "#705840", "h": (0.6,  0.85), "r": (0.2,  0.5),  "score": 1.0,  "cost": 1},
    "Floresta Temperada":   {"color": "#2e5430", "h": (0.6,  0.85), "r": (0.5,  0.7),  "score": 3.0,  "cost": 1},
    "Taiga":                {"color": "#243828", "h": (0.6,  0.85), "r": (0.7,  1.01), "score": 1.0,  "cost": 2},
    "Tundra":               {"color": "#7a9aaa", "h": (0,    1),    "r": (0,    1),    "score": -1.0, "cost": 2},
    "Montanha Árida":       {"color": "#505058", "h": (0.85, 1.01), "r": (0.0,  0.3),  "score": -1.0, "cost": 8},
    "Montanha":             {"color": "#606070", "h": (0.85, 1.01), "r": (0.3,  0.6),  "score": -1.0, "cost": 8},
    "Montanha com Floresta":{"color": "#3e5040", "h": (0.85, 1.01), "r": (0.6,  1.01), "score": -1.0, "cost": 8},
    "Mangue Tropical":      {"color": "#1e3028", "h": (0.25, 0.3),  "r": (0.6,  1.01), "score": 1.0,  "cost": 3},
}

BIOMES_HABITAVEIS = {name for name, data in BIOMES.items() if data["score"] > 0}
BIOMES_AGUA       = {name for name, data in BIOMES.items() if data["cost"] == 999}

#  FILTROS DE IMAGEM
def mode_filter_rgb(img):
    h, w, _ = img.shape
    out = np.zeros_like(img)
    for y in range(1, h - 1):
        for x in range(1, w - 1):
            region = img[y-1:y+2, x-1:x+2]
            tuples = [tuple(p) for p in region.reshape(-1, 3)]
            out[y, x] = np.array(Counter(tuples).most_common(1)[0][0])
    out[0] = out[1]; out[-1] = out[-2]
    out[:, 0] = out[:, 1]; out[:, -1] = out[:, -2]
    return out

def small_blur_rgb(img):
    kernel = np.array([[1,2,1],[2,4,2],[1,2,1]], dtype=float) / 16.0
    padded = np.pad(img, ((1,1),(1,1),(0,0)), mode='edge')
    h, w, _ = img.shape
    out = np.zeros_like(img)
    for y in range(h):
        for x in range(w):
            out[y, x] = np.tensordot(padded[y:y+3, x:x+3], kernel, axes=([0,1],[0,1]))
    return out

#  RUÍDO
def smooth_noise(shape, scale=10):
    base = np.random.rand(shape[0]//scale + 2, shape[1]//scale + 2)
    x = np.linspace(0, base.shape[0]-2, shape[0])
    y = np.linspace(0, base.shape[1]-2, shape[1])
    x0, y0 = x.astype(int), y.astype(int)
    xf, yf = x - x0, y - y0
    smooth = np.zeros(shape)
    for i in range(shape[0]):
        v00 = base[x0[i],   y0];  v10 = base[x0[i]+1, y0]
        v01 = base[x0[i],   y0+1]; v11 = base[x0[i]+1, y0+1]
        smooth[i] = (v00*(1-xf[i])*(1-yf) + v10*xf[i]*(1-yf) +
                     v01*(1-xf[i])*yf      + v11*xf[i]*yf)
    return smooth

def smooth_biomes(biomes):
    h, w = biomes.shape
    out = biomes.copy()
    for y in range(1, h-1):
        for x in range(1, w-1):
            region = biomes[y-1:y+2, x-1:x+2].flatten()
            vals, counts = np.unique(region, return_counts=True)
            out[y, x] = vals[np.argmax(counts)]
    return out

#  BIOMAS
def get_biome(h, r):
    for name, data in BIOMES.items():
        if data["h"][0] <= h < data["h"][1] and data["r"][0] <= r < data["r"][1]:
            return name
    return "Planície"

def build_biome_map(world_height, world_rain):
    biomes = np.full((height, width), '', dtype=object)
    for y in range(height):
        for x in range(width):
            biomes[y, x] = get_biome(world_height[y, x], world_rain[y, x])
    lat  = np.linspace(0, 1, height).reshape(-1, 1)
    cold = (lat < 0.05) | (lat > 0.95)
    biomes[cold & (world_height > 0.5)] = "Tundra"
    return smooth_biomes(biomes)

#  ASSENTAMENTOS
def calc_scores(biomes, river_map):
    scores = np.full((height, width), -1.0)
    for y in range(1, height-1):
        for x in range(1, width-1):
            biome = biomes[y, x]
            s = BIOMES[biome]["score"]
            if s <= 0: continue
            if river_map[y, x] > 0.05: s += 2.5
            scores[y, x] = s
    return scores

def place_settlements(scores, n, min_dist):
    pool = sorted(
        [(scores[y, x], y, x) for y in range(height) for x in range(width) if scores[y, x] > 0],
        reverse=True
    )
    placed = []
    for _, y, x in pool:
        if len(placed) >= n: break
        if not any(abs(y-py) + abs(x-px) < min_dist for py, px in placed):
            placed.append((y, x))
    return placed

#  ESTRADAS — A*
def road_cost(y, x, biomes):
    return BIOMES[biomes[y, x]]["cost"]

def astar(start, end, biomes):
    sy, sx = start; ey, ex = end
    h_fn = lambda y, x: abs(y - ey) + abs(x - ex)
    open_set = [(h_fn(sy, sx), 0, sy, sx, [(sy, sx)])]
    visited  = {}
    while open_set:
        _, g, y, x, path = heapq.heappop(open_set)
        if (y, x) in visited: continue
        visited[(y, x)] = g
        if y == ey and x == ex: return path
        for dy, dx in [(-1,0),(1,0),(0,-1),(0,1),(-1,-1),(-1,1),(1,-1),(1,1)]:
            ny, nx = y+dy, x+dx
            if 0 <= ny < height and 0 <= nx < width:
                ng = g + road_cost(ny, nx, biomes)
                if (ny, nx) not in visited or visited[(ny, nx)] > ng:
                    heapq.heappush(open_set, (ng + h_fn(ny, nx), ng, ny, nx, path + [(ny, nx)]))
    return None

def build_roads(cities, villages, biomes):
    roads = []
    connected = {0}
    for i in range(1, len(cities)):
        best_len, best_j = float('inf'), 0
        for j in connected:
            d = abs(cities[i][0]-cities[j][0]) + abs(cities[i][1]-cities[j][1])
            if d < best_len: best_len, best_j = d, j
        path = astar(cities[i], cities[best_j], biomes)
        if path: roads.append(path)
        connected.add(i)
    for vy, vx in villages:
        nearest = min(cities, key=lambda c: abs(vy-c[0]) + abs(vx-c[1]))
        if abs(vy-nearest[0]) + abs(vx-nearest[1]) <= 25:
            path = astar((vy, vx), nearest, biomes)
            if path: roads.append(path)
    return roads

#  COORDENADAS
def hex_to_plot(y, x):
    return hex_size * 1.5 * x, hex_size * np.sqrt(3) * (y + 0.5 * (x % 2))

#  PIPELINE DE COR
def build_base_rgb(biomes):
    color_lookup = {name: np.array(mcolors.to_rgb(data["color"])) for name, data in BIOMES.items()}
    rgb = np.zeros((height, width, 3), dtype=float)
    for y in range(height):
        for x in range(width):
            rgb[y, x] = color_lookup.get(biomes[y, x], np.zeros(3))
    rgb = mode_filter_rgb(rgb)
    rgb = small_blur_rgb(rgb)
    return rgb

def apply_shading(rgb, world_height):
    gy, gx = np.gradient(world_height)
    light = gx * np.cos(angulo_luz) + gy * np.sin(angulo_luz)
    light = (light - light.min()) / (light.max() - light.min())
    light = 0.42 + 0.58 * light
    alt   = (world_height - world_height.min()) / (world_height.max() - world_height.min())
    scale = light * (0.78 + 0.32 * alt)
    return np.clip(rgb * scale[:, :, None], 0, 1)

def apply_river_color(rgb, river_map):
    mask = river_map > 0.12
    rgb[mask] = np.clip(rgb[mask] * (1 - river_map[mask, None] * 0.40), 0, 1)
    blue_boost = np.zeros_like(rgb)
    blue_boost[:, :, 2] = river_map * 0.65
    rgb = np.where(mask[:, :, None], rgb + blue_boost, rgb)
    return np.clip(rgb, 0, 1)

def apply_ocean_depth(rgb, biomes, world_height):
    ocean_mask = np.vectorize(lambda b: "Oceano" in b)(biomes)
    depth = np.clip(world_height / 0.2, 0, 1)
    scale = np.where(ocean_mask, 0.70 + 0.30 * depth, 1.0)
    return np.clip(rgb * scale[:, :, None], 0, 1)

def apply_vignette(rgb):
    cy, cx = height / 2, width / 2
    yy, xx = np.mgrid[0:height, 0:width]
    dist = np.sqrt(((yy - cy) / cy) ** 2 + ((xx - cx) / cx) ** 2)
    vignette = 1.0 - np.clip(dist * 0.30, 0, 0.28)
    return np.clip(rgb * vignette[:, :, None], 0, 1)

def build_color_map(biomes, world_height, river_map):
    rgb = build_base_rgb(biomes)
    rgb = apply_shading(rgb, world_height)
    rgb = apply_river_color(rgb, river_map)
    rgb = apply_ocean_depth(rgb, biomes, world_height)
    rgb = apply_vignette(rgb)
    rgb = np.clip(rgb * BRIGHTNESS, 0, 1)   # controle global de brilho
    return rgb

#  DESENHO — funções unificadas
def setup_figure():
    fig, ax = plt.subplots(figsize=(12, 12))
    ax.set_aspect("equal"); ax.axis("off")
    fig.patch.set_facecolor("#060608")
    ax.set_facecolor("#060608")
    return fig, ax

def draw_hexes(ax, rgb_map):
    xs_grid = hex_size * 1.5 * np.arange(width)
    ys_grid = hex_size * np.sqrt(3) * (np.arange(height)[:, None] + 0.5 * (np.arange(width) % 2))
    hexes, colors = [], []
    for y in range(height):
        for x in range(width):
            hexes.append(RegularPolygon(
                xy=(xs_grid[x], ys_grid[y, x]),
                numVertices=6, radius=hex_size, orientation=np.radians(30)
            ))
            colors.append(tuple(rgb_map[y, x]))
    pc = PatchCollection(hexes, match_original=True)
    pc.set_facecolor(colors)
    pc.set_edgecolor((0, 0, 0, 0.18))
    pc.set_linewidth(0.025)
    ax.add_collection(pc)

def draw_roads(ax, roads):
    for road in roads:
        if len(road) < 2: continue
        rx = [hex_to_plot(y, x)[0] for y, x in road]
        ry = [hex_to_plot(y, x)[1] for y, x in road]
        ax.plot(rx, ry, color="#1a0f00", linewidth=1.2, alpha=0.6, solid_capstyle="round", zorder=2)
        ax.plot(rx, ry, color="#8a6535", linewidth=0.55, alpha=0.80,
                solid_capstyle="round", zorder=3, linestyle=(0, (6, 2)))

def draw_villages(ax, villages):
    for vy, vx in villages:
        px, py = hex_to_plot(vy, vx)
        ax.plot(px, py, 'o', color="#c8902a", markersize=5.5, alpha=0.25, zorder=4)
        ax.plot(px, py, 'o', color="#b07820", markersize=3.0,
                markeredgecolor="#5a3a08", markeredgewidth=0.5, zorder=5, alpha=0.95)

def draw_cities(ax, cities):
    for cy, cx in cities:
        px, py = hex_to_plot(cy, cx)
        ax.plot(px, py, 'o', color="#8b0000", markersize=13, alpha=0.18, zorder=4)
        ax.plot(px, py, 'o', color="#5a1a00", markersize=9,  alpha=0.55, zorder=5)
        ax.plot(px, py, '*', color="#e8c87a", markersize=7,
                markeredgecolor="#3a1a00", markeredgewidth=0.6, zorder=6)
        ax.plot(px, py, '.', color="#fffae0", markersize=2.0, zorder=7)

def draw_legend(ax, cities, villages, roads):
    legend_elements = [
        Line2D([0], [0], marker='*', color='w', label=f'Cidade ({len(cities)})',
               markerfacecolor='#e8c87a', markeredgecolor='#3a1a00', markersize=9, linestyle='None'),
        Line2D([0], [0], marker='o', color='w', label=f'Vila ({len(villages)})',
               markerfacecolor='#b07820', markeredgecolor='#5a3a08', markersize=6, linestyle='None'),
        Line2D([0], [0], color='#8a6535', linewidth=1.5,
               linestyle=(0, (6, 2)), label=f'Estrada ({len(roads)})'),
    ]
    leg = ax.legend(handles=legend_elements, loc='lower right', framealpha=0.85,
                    facecolor='#0d0a07', edgecolor='#4a3010', labelcolor='#c8a870',
                    fontsize=7, title="— Legenda —", title_fontsize=7)
    leg.get_title().set_color('#8a6535')

def draw_border(ax):
    x0, x1 = -hex_size, hex_size * 1.5 * (width + 0.5)
    y0, y1 = -hex_size, hex_size * np.sqrt(3) * (height + 0.5)
    for lw, alpha, ins in [(2.5, 0.6, 0.0), (0.8, 0.9, 0.015)]:
        rect = plt.Polygon(
            [[x0+ins, y0+ins],[x1-ins, y0+ins],[x1-ins, y1-ins],[x0+ins, y1-ins]],
            closed=True, fill=False, edgecolor="#6a4a1a", linewidth=lw, alpha=alpha, zorder=10
        )
        ax.add_patch(rect)

def finalize_and_save(fig, ax, idx, timestamp):
    x0, x1 = -hex_size, hex_size * 1.5 * (width + 0.5)
    y0, y1 = -hex_size, hex_size * np.sqrt(3) * (height + 0.5)
    ax.set_xlim(x0, x1); ax.set_ylim(y0, y1)
    filename = f"mapas/mapa_{idx+1}_{timestamp}.png"
    plt.savefig(filename, dpi=300, bbox_inches="tight", facecolor=fig.get_facecolor())
    plt.close()
    return filename

#  EXPORTAÇÃO JSON
def save_map_json(filename_map, seed, cities, villages, roads, biomes, timestamp):
    biome_counts = {}
    for b in biomes.flatten():
        biome_counts[b] = biome_counts.get(b, 0) + 1

    data = {
        "timestamp": timestamp,
        "seed": int(seed),
        "parameters": {
            "width": width, "height": height,
            "hex_size": hex_size,
            "brightness": BRIGHTNESS,
            "angulo_luz_graus": float(np.degrees(angulo_luz)),
            "num_cidades_range": list(NUM_CIDADES),
            "num_vilas_range":   list(NUM_VILAS),
        },
        "cities": [[int(y), int(x)] for y, x in cities],
        "villages": [[int(y), int(x)] for y, x in villages],
        "roads": [[[int(y), int(x)] for y, x in road] for road in roads],
        "biome_distribution": {k: int(v) for k, v in sorted(biome_counts.items(), key=lambda i: -i[1])},
        "stats": {
            "n_cities": len(cities),
            "n_villages": len(villages),
            "n_roads": len(roads),
            "n_road_cells": sum(len(r) for r in roads),
        }
    }

    json_path = "json/" + os.path.splitext(os.path.basename(filename_map))[0] + ".json"
    with open(json_path, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
    return json_path

#  GERAÇÃO DO MUNDO
for i in range(NUM_MAPAS):
    seed = np.random.randint(0, 2**31)
    np.random.seed(seed)
    print(f"Gerando mapa {i+1}/{NUM_MAPAS}  (seed={seed})...")

    # ── Ruído base ──────────────────────────────
    world_height = smooth_noise((height, width), scale=10)
    world_rain   = smooth_noise((height, width), scale=15)

    for arr in (world_height, world_rain):
        arr[:] = (arr - arr.min()) / (arr.max() - arr.min())

    continent_noise = smooth_noise((height, width), scale=40)
    continent_noise = (continent_noise - continent_noise.min()) / (continent_noise.max() - continent_noise.min())

    world_height = 0.65 * world_height + 0.35 * continent_noise
    world_height = np.clip(world_height ** 1.35, 0, 1)
    world_height = 0.7 * world_height + 0.3 * (
        0.25 * np.roll(world_height, 1, 0) + 0.25 * np.roll(world_height,-1, 0) +
        0.25 * np.roll(world_height, 1, 1) + 0.25 * np.roll(world_height,-1, 1)
    )

    # Suavização costeira — vetorizada
    coast = (world_height > 0.18) & (world_height < 0.26)
    world_height[coast] *= 0.92
    world_rain[coast]   *= 1.04

    # ── Rios ────────────────────────────────────
    river_map = np.zeros((height, width))
    for _ in range(15000):
        y = np.random.randint(1, height-1)
        x = np.random.randint(1, width-1)
        for _ in range(60):
            ymin, xmin, hmin = y, x, world_height[y, x]
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    ny, nx = y+dy, x+dx
                    if 0 <= ny < height and 0 <= nx < width and world_height[ny, nx] < hmin:
                        ymin, xmin, hmin = ny, nx, world_height[ny, nx]
            if ymin == y and xmin == x: break
            y, x = ymin, xmin
            river_map[y, x] += 1
    if river_map.max() > 0:
        river_map /= river_map.max()

    # ── Biomas ──────────────────────────────────
    biomes = build_biome_map(world_height, world_rain)

    # ── Assentamentos & Estradas ─────────────────
    print("  Calculando assentamentos e estradas...")
    scores = calc_scores(biomes, river_map)
    n_cidades = np.random.randint(*NUM_CIDADES)
    n_vilas = np.random.randint(*NUM_VILAS)
    cities = place_settlements(scores, n_cidades, min_dist=14)
    village_candidates = place_settlements(scores, n_vilas + n_cidades, min_dist=5)
    villages = [v for v in village_candidates if v not in cities][:n_vilas]
    roads = build_roads(cities, villages, biomes) if len(cities) >= 2 else []

    # ── Pipeline de cor ──────────────────────────
    print("  Renderizando cores...")
    rgb_map = build_color_map(biomes, world_height, river_map)

    # ── Desenho ──────────────────────────────────
    fig, ax = setup_figure()
    draw_hexes(ax, rgb_map)
    draw_roads(ax, roads)
    draw_villages(ax, villages)
    draw_cities(ax, cities)
    draw_legend(ax, cities, villages, roads)
    draw_border(ax)

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    filename  = finalize_and_save(fig, ax, i, timestamp)

    json_path = save_map_json(filename, seed, cities, villages, roads, biomes, timestamp)

    print(f"  ✓ Mapa salvo em {filename}")
    print(f"  ✓ JSON salvo em {json_path}")
    print(f"     {len(cities)} cidades · {len(villages)} vilas · {len(roads)} estradas")
