#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <random>
#include <chrono>
#include <filesystem>
#include <queue>
#include <tuple>
#include <iomanip>
#include <sstream>
#include "IniConfig.h"
#include "ImageCanvas.h"
#include "BiomeDef.h"
#include "PerlinNoise.h"
#include "SimpleJson.h"
#include "config.h"

namespace fs = std::filesystem;

struct MapConfig {
    int width = 100;
    int height = 100;
    double hex_size = 0.10;
    int num_mapas = 5;
    double angulo_luz_deg = 135.0;
    double brightness = 1.0;
    int min_cidades = 5;
    int max_cidades = 9;
    int min_vilas = 20;
    int max_vilas = 35;
    int render_scale = 14; // pixels por hexágono para renderização de alta qualidade
};

struct Point2D {
    int y = 0;
    int x = 0;
    bool operator==(const Point2D& o) const {
        return y == o.y && x == o.x;
    }
    bool operator<(const Point2D& o) const {
        return std::tie(y, x) < std::tie(o.y, o.x);
    }
};

struct WorldMap {
    int width = 100;
    int height = 100;
    uint32_t seed = 0;
    std::string timestamp;

    std::vector<double> elevation; // world_height [0, 1]
    std::vector<double> moisture; // world_rain [0, 1]
    std::vector<double> rivers; // river_map [0, 1]
    std::vector<std::string> biomes;

    std::vector<Point2D> cities;
    std::vector<Point2D> villages;
    std::vector<std::vector<Point2D>> roads;

    inline double getElev(int y, int x) const {
        return elevation[y * width + x];
    }
    inline double getMoist(int y, int x) const {
        return moisture[y * width + x];
    }
    inline double getRiver(int y, int x) const {
        return rivers[y * width + x];
    }
    inline const std::string& getBiome(int y, int x) const {
        return biomes[y * width + x];
    }
};

// Conversão Hexágono para Coordenadas Contínuas (como no Python hex_to_plot)
inline std::pair<double, double> hexToPlot(int y, int x, double hex_size) {
    double px = hex_size * 1.5 * x;
    double py = hex_size * std::sqrt(3.0) * (y + 0.5 * (x % 2));
    return {px, py};
}

// Filtro Modal (Mode Filter) 3x3 para RGB
void modeFilterRGB(std::vector<ColorRGB>& img, int width, int height) {
    std::vector<ColorRGB> out = img;
    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            std::map<uint32_t, int> counts;
            std::map<uint32_t, ColorRGB> colMap;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    ColorRGB c = img[(y + dy) * width + (x + dx)];
                    uint32_t key = (static_cast<uint32_t>(c.r) << 16) | (static_cast<uint32_t>(c.g) << 8) | c.b;
                    counts[key]++;
                    colMap[key] = c;
                }
            }
            int bestCount = -1;
            uint32_t bestKey = 0;
            for (const auto& kv : counts) {
                if (kv.second > bestCount) {
                    bestCount = kv.second;
                    bestKey = kv.first;
                }
            }
            out[y * width + x] = colMap[bestKey];
        }
    }
    img = std::move(out);
}

// Filtro Blur Suave Gaussiano 3x3
void smallBlurRGB(std::vector<ColorRGB>& img, int width, int height) {
    std::vector<ColorRGB> out = img;
    // kernel 1 2 1 / 2 4 2 / 1 2 1 / 16
    const double k[3][3] = {
        {1.0 / 16.0, 2.0 / 16.0, 1.0 / 16.0},
        {2.0 / 16.0, 4.0 / 16.0, 2.0 / 16.0},
        {1.0 / 16.0, 2.0 / 16.0, 1.0 / 16.0}
    };
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            double rSum = 0, gSum = 0, bSum = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                int ny = std::clamp(y + dy, 0, height - 1);
                for (int dx = -1; dx <= 1; ++dx) {
                    int nx = std::clamp(x + dx, 0, width - 1);
                    double w = k[dy + 1][dx + 1];
                    ColorRGB c = img[ny * width + nx];
                    rSum += c.r * w;
                    gSum += c.g * w;
                    bSum += c.b * w;
                }
            }
            out[y * width + x] = {
                static_cast<uint8_t>(std::clamp(rSum, 0.0, 255.0)),
                static_cast<uint8_t>(std::clamp(gSum, 0.0, 255.0)),
                static_cast<uint8_t>(std::clamp(bSum, 0.0, 255.0))
            };
        }
    }
    img = std::move(out);
}

// Suavização dos Biomas com filtro modal de vizinhança
void smoothBiomes(std::vector<std::string>& biomes, int width, int height) {
    std::vector<std::string> out = biomes;
    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            std::map<std::string, int> counts;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    counts[biomes[(y + dy) * width + (x + dx)]]++;
                }
            }
            int maxC = -1;
            std::string best = biomes[y * width + x];
            for (const auto& kv : counts) {
                if (kv.second > maxC) {
                    maxC = kv.second;
                    best = kv.first;
                }
            }
            out[y * width + x] = best;
        }
    }
    biomes = std::move(out);
}

// Constrói o mapa de biomas
std::vector<std::string> buildBiomeMap(const std::vector<double>& elevation, const std::vector<double>& moisture, int width, int height) {
    std::vector<std::string> biomes(height * width);
    for (int y = 0; y < height; ++y) {
        double latNorm = static_cast<double>(y) / (height - 1);
        bool cold = (latNorm < 0.05 || latNorm > 0.95);

        for (int x = 0; x < width; ++x) {
            int idx = y * width + x;
            double h = elevation[idx];
            double r = moisture[idx];
            if (cold && h > 0.5) {
                biomes[idx] = "Tundra";
            } else {
                biomes[idx] = determineBiome(h, r);
            }
        }
    }
    smoothBiomes(biomes, width, height);
    return biomes;
}

// Cálculo de scores para cidades e vilas
std::vector<double> calcScores(const std::vector<std::string>& biomes, const std::vector<double>& rivers, int width, int height) {
    const auto& bDefs = getBiomesDefinition();
    std::vector<double> scores(height * width, -1.0);
    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            int idx = y * width + x;
            const std::string& b = biomes[idx];
            auto it = bDefs.find(b);
            if (it == bDefs.end()) continue;
            double s = it->second.score;
            if (s <= 0.0) continue;
            if (rivers[idx] > 0.05) s += 2.5;
            scores[idx] = s;
        }
    }
    return scores;
}

// Posicionamento de assentamentos respeitando distância mínima
std::vector<Point2D> placeSettlements(const std::vector<double>& scores, int width, int height, int n, int minDist) {
    struct Candidate {
        double score;
        int y, x;
        bool operator>(const Candidate& o) const {
            return score > o.score;
        }
    };
    std::vector<Candidate> pool;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            double s = scores[y * width + x];
            if (s > 0.0) pool.push_back({s, y, x});
        }
    }
    std::sort(pool.begin(), pool.end(), [](const Candidate& a, const Candidate& b) {
        return a.score > b.score;
    });

    std::vector<Point2D> placed;
    for (const auto& c : pool) {
        if (static_cast<int>(placed.size()) >= n) break;
        bool tooClose = false;
        for (const auto& p : placed) {
            if (std::abs(c.y - p.y) + std::abs(c.x - p.x) < minDist) {
                tooClose = true;
                break;
            }
        }
        if (!tooClose) placed.push_back({c.y, c.x});
    }
    return placed;
}

// Custo de locomoção na célula
int roadCost(int y, int x, int width, const std::vector<std::string>& biomes) {
    const auto& bDefs = getBiomesDefinition();
    auto it = bDefs.find(biomes[y * width + x]);
    if (it != bDefs.end()) return it->second.cost;
    return 1;
}

// Algoritmo A* para conexão das estradas
std::vector<Point2D> astarRoad(Point2D start, Point2D end, int width, int height, const std::vector<std::string>& biomes) {
    auto heuristic = [](Point2D a, Point2D b) {
        return std::abs(a.y - b.y) + std::abs(a.x - b.x);
    };

    struct Node {
        Point2D pt;
        int g;
        int f;
        bool operator>(const Node& o) const {
            return f > o.f;
        }
    };

    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> openSet;
    std::vector<int> gScore(width * height, 1e9);
    std::vector<Point2D> parent(width * height, {-1, -1});

    int startIdx = start.y * width + start.x;
    gScore[startIdx] = 0;
    openSet.push({start, 0, heuristic(start, end)});

    const int dy[8] = {-1, 1, 0, 0, -1, -1, 1, 1};
    const int dx[8] = {0, 0, -1, 1, -1, 1, -1, 1};

    while (!openSet.empty()) {
        Node cur = openSet.top();
        openSet.pop();

        if (cur.pt == end) {
            std::vector<Point2D> path;
            Point2D curr = end;
            while (!(curr == start)) {
                path.push_back(curr);
                curr = parent[curr.y * width + curr.x];
            }
            path.push_back(start);
            std::reverse(path.begin(), path.end());
            return path;
        }

        int curIdx = cur.pt.y * width + cur.pt.x;
        if (cur.g > gScore[curIdx]) continue;

        for (int i = 0; i < 8; ++i) {
            int ny = cur.pt.y + dy[i];
            int nx = cur.pt.x + dx[i];
            if (ny >= 0 && ny < height && nx >= 0 && nx < width) {
                int nIdx = ny * width + nx;
                int cost = roadCost(ny, nx, width, biomes);
                if (cost >= 900) continue; // evita oceano se possível
                int newG = cur.g + cost;
                if (newG < gScore[nIdx]) {
                    gScore[nIdx] = newG;
                    parent[nIdx] = cur.pt;
                    openSet.push({{ny, nx}, newG, newG + heuristic({ny, nx}, end)});
                }
            }
        }
    }
    return {};
}

// Constrói a malha viária
std::vector<std::vector<Point2D>> buildRoads(const std::vector<Point2D>& cities, const std::vector<Point2D>& villages,
                                             int width, int height, const std::vector<std::string>& biomes) {
    std::vector<std::vector<Point2D>> roads;
    std::vector<int> connected = {0};

    for (size_t i = 1; i < cities.size(); ++i) {
        int bestLen = 1e9;
        int bestJ = 0;
        for (int j : connected) {
            int d = std::abs(cities[i].y - cities[j].y) + std::abs(cities[i].x - cities[j].x);
            if (d < bestLen) {
                bestLen = d;
                bestJ = j;
            }
        }
        auto path = astarRoad(cities[i], cities[bestJ], width, height, biomes);
        if (!path.empty()) roads.push_back(path);
        connected.push_back(static_cast<int>(i));
    }

    for (const auto& v : villages) {
        int nearestDist = 1e9;
        Point2D nearestCity = cities[0];
        for (const auto& c : cities) {
            int d = std::abs(v.y - c.y) + std::abs(v.x - c.x);
            if (d < nearestDist) {
                nearestDist = d;
                nearestCity = c;
            }
        }
        if (nearestDist <= 25) {
            auto path = astarRoad(v, nearestCity, width, height, biomes);
            if (!path.empty()) roads.push_back(path);
        }
    }
    return roads;
}

// Pipeline de cor completo (Shading, Rios, Profundidade, Vinheta, Brilho)
std::vector<ColorRGB> buildColorMap(const WorldMap& map, const MapConfig& cfg) {
    const auto& bDefs = getBiomesDefinition();
    int w = map.width;
    int h = map.height;

    std::vector<ColorRGB> baseRgb(w * h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int idx = y * w + x;
            auto it = bDefs.find(map.biomes[idx]);
            if (it != bDefs.end()) {
                baseRgb[idx] = ColorRGB::fromHex(it->second.colorHex);
            } else {
                baseRgb[idx] = ColorRGB::fromHex("#507038");
            }
        }
    }

    modeFilterRGB(baseRgb, w, h);
    smallBlurRGB(baseRgb, w, h);

    // Gradiente e Shading
    double radAng = cfg.angulo_luz_deg * (Config::PI / 180.0);
    double cosA = std::cos(radAng);
    double sinA = std::sin(radAng);

    std::vector<double> lightVal(w * h, 0.0);
    double minL = 1e9, maxL = -1e9;
    double minH = 1e9, maxH = -1e9;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int idx = y * w + x;
            double elev = map.elevation[idx];
            if (elev < minH) minH = elev;
            if (elev > maxH) maxH = elev;

            int prevX = std::max(0, x - 1);
            int nextX = std::min(w - 1, x + 1);
            int prevY = std::max(0, y - 1);
            int nextY = std::min(h - 1, y + 1);

            double gx = (map.elevation[y * w + nextX] - map.elevation[y * w + prevX]) * 0.5;
            double gy = (map.elevation[nextY * w + x] - map.elevation[prevY * w + x]) * 0.5;

            double l = gx * cosA + gy * sinA;
            lightVal[idx] = l;
            if (l < minL) minL = l;
            if (l > maxL) maxL = l;
        }
    }

    double rangeL = (maxL - minL > 1e-6) ? (maxL - minL) : 1.0;
    double rangeH = (maxH - minH > 1e-6) ? (maxH - minH) : 1.0;

    double cy = h / 2.0;
    double cx = w / 2.0;

    std::vector<ColorRGB> finalRgb(w * h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int idx = y * w + x;
            double normL = (lightVal[idx] - minL) / rangeL;
            double lFactor = 0.42 + 0.58 * normL;

            double normH = (map.elevation[idx] - minH) / rangeH;
            double scale = lFactor * (0.78 + 0.32 * normH);

            double rf = (baseRgb[idx].r / 255.0) * scale;
            double gf = (baseRgb[idx].g / 255.0) * scale;
            double bf = (baseRgb[idx].b / 255.0) * scale;

            // Rios
            double riv = map.rivers[idx];
            if (riv > 0.12) {
                rf = rf * (1.0 - riv * 0.40);
                gf = gf * (1.0 - riv * 0.40);
                bf = bf * (1.0 - riv * 0.40) + riv * 0.65;
            }

            // Oceano Profundidade
            if (map.biomes[idx].find("Oceano") != std::string::npos) {
                double depth = std::clamp(map.elevation[idx] / 0.20, 0.0, 1.0);
                double oceanScale = 0.70 + 0.30 * depth;
                rf *= oceanScale;
                gf *= oceanScale;
                bf *= oceanScale;
            }

            // Vignette
            double dyNorm = (y - cy) / cy;
            double dxNorm = (x - cx) / cx;
            double dist = std::sqrt(dyNorm * dyNorm + dxNorm * dxNorm);
            double vig = 1.0 - std::clamp(dist * 0.30, 0.0, 0.28);
            rf *= vig;
            gf *= vig;
            bf *= vig;

            // Brilho global
            rf = std::clamp(rf * cfg.brightness, 0.0, 1.0);
            gf = std::clamp(gf * cfg.brightness, 0.0, 1.0);
            bf = std::clamp(bf * cfg.brightness, 0.0, 1.0);

            finalRgb[idx] = ColorRGB::fromNormalized(rf, gf, bf);
        }
    }
    return finalRgb;
}

// Renderiza o mapa em alta resolução hexágono por hexágono como o matplotlib
ImageCanvas renderWorldMap(const WorldMap& map, const std::vector<ColorRGB>& rgbMap, const MapConfig& cfg) {
    // Dimensões do canvas baseadas nas coordenadas dos hexágonos
    double maxPx = 0, maxPy = 0;
    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            auto pt = hexToPlot(y, x, cfg.hex_size);
            if (pt.first > maxPx) maxPx = pt.first;
            if (pt.second > maxPy) maxPy = pt.second;
        }
    }

    int imgWidth = static_cast<int>(std::ceil(maxPx * cfg.render_scale / cfg.hex_size)) + 80;
    int imgHeight = static_cast<int>(std::ceil(maxPy * cfg.render_scale / cfg.hex_size)) + 80;

    ImageCanvas canvas(imgWidth, imgHeight, {6, 6, 8});

    auto worldToCanvas = [&](double wx, double wy) -> std::pair<double, double> {
        double cx = 40.0 + (wx / cfg.hex_size) * cfg.render_scale;
        double cy = imgHeight - (40.0 + (wy / cfg.hex_size) * cfg.render_scale);
        return {cx, cy};
    };

    double radiusCanvas = cfg.render_scale * 1.05;

    // 1. Desenhar hexágonos com preenchimento e bordas
    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            auto [wx, wy] = hexToPlot(y, x, cfg.hex_size);
            auto [cx, cy] = worldToCanvas(wx, wy);

            std::vector<std::pair<double, double>> poly;
            for (int k = 0; k < 6; ++k) {
                double a = (30.0 + k * 60.0) * (Config::PI / 180.0);
                poly.push_back({cx + radiusCanvas * std::cos(a), cy + radiusCanvas * std::sin(a)});
            }
            ColorRGB cellCol = rgbMap[y * map.width + x];
            canvas.drawFilledPolygon(poly, cellCol, 1.0);
            canvas.drawPolygonOutline(poly, {0, 0, 0}, 1.0, 0.18);
        }
    }

    // 2. Desenhar estradas
    for (const auto& road : map.roads) {
        if (road.size() < 2) continue;
        for (size_t i = 0; i + 1 < road.size(); ++i) {
            auto [wx0, wy0] = hexToPlot(road[i].y, road[i].x, cfg.hex_size);
            auto [wx1, wy1] = hexToPlot(road[i + 1].y, road[i + 1].x, cfg.hex_size);
            auto [cx0, cy0] = worldToCanvas(wx0, wy0);
            auto [cx1, cy1] = worldToCanvas(wx1, wy1);

            // Camada de base escura
            canvas.drawLine(cx0, cy0, cx1, cy1, {26, 15, 0}, 3.5, 0.6);
            // Camada dourada / marrom
            canvas.drawLine(cx0, cy0, cx1, cy1, {138, 101, 53}, 1.5, 0.85);
        }
    }

    // 3. Desenhar vilas
    for (const auto& v : map.villages) {
        auto [wx, wy] = hexToPlot(v.y, v.x, cfg.hex_size);
        auto [cx, cy] = worldToCanvas(wx, wy);
        canvas.drawFilledCircle(cx, cy, 7.0, {200, 144, 42}, 0.35);
        canvas.drawFilledCircle(cx, cy, 4.0, {90, 58, 8}, 0.95);
        canvas.drawFilledCircle(cx, cy, 2.5, {176, 120, 32}, 1.0);
    }

    // 4. Desenhar cidades
    for (const auto& c : map.cities) {
        auto [wx, wy] = hexToPlot(c.y, c.x, cfg.hex_size);
        auto [cx, cy] = worldToCanvas(wx, wy);
        canvas.drawFilledCircle(cx, cy, 14.0, {139, 0, 0}, 0.25);
        canvas.drawFilledCircle(cx, cy, 9.0, {90, 26, 0}, 0.65);
        canvas.drawStar(cx, cy, 7.5, 3.5, 5, {232, 200, 122}, 1.0);
        canvas.drawFilledCircle(cx, cy, 2.0, {255, 250, 224}, 1.0);
    }

    // 5. Moldura artística / borda dupla
    std::vector<std::pair<double, double>> borderBox1 = {
        {15, 15}, {imgWidth - 15.0, 15}, {imgWidth - 15.0, imgHeight - 15.0}, {15, imgHeight - 15.0}
    };
    std::vector<std::pair<double, double>> borderBox2 = {
        {20, 20}, {imgWidth - 20.0, 20}, {imgWidth - 20.0, imgHeight - 20.0}, {20, imgHeight - 20.0}
    };
    canvas.drawPolygonOutline(borderBox1, {106, 74, 26}, 3.0, 0.7);
    canvas.drawPolygonOutline(borderBox2, {106, 74, 26}, 1.5, 0.9);

    return canvas;
}

// Salva o arquivo JSON com a estrutura do mapa
std::string saveMapJson(const WorldMap& map, const MapConfig& cfg, const std::string& baseName) {
    SimpleJson::Node root;
    root.type = SimpleJson::Node::Object;

    root.obj["timestamp"] = SimpleJson::Node(map.timestamp);
    root.obj["seed"] = SimpleJson::Node(static_cast<double>(map.seed));

    SimpleJson::Node params;
    params.type = SimpleJson::Node::Object;
    params.obj["width"] = SimpleJson::Node(cfg.width);
    params.obj["height"] = SimpleJson::Node(cfg.height);
    params.obj["hex_size"] = SimpleJson::Node(cfg.hex_size);
    params.obj["brightness"] = SimpleJson::Node(cfg.brightness);
    params.obj["angulo_luz_graus"] = SimpleJson::Node(cfg.angulo_luz_deg);

    SimpleJson::Node cRange;
    cRange.type = SimpleJson::Node::Array;
    cRange.arr.push_back(SimpleJson::Node(cfg.min_cidades));
    cRange.arr.push_back(SimpleJson::Node(cfg.max_cidades));
    params.obj["num_cidades_range"] = cRange;

    SimpleJson::Node vRange;
    vRange.type = SimpleJson::Node::Array;
    vRange.arr.push_back(SimpleJson::Node(cfg.min_vilas));
    vRange.arr.push_back(SimpleJson::Node(cfg.max_vilas));
    params.obj["num_vilas_range"] = vRange;

    root.obj["parameters"] = params;

    // Cities
    SimpleJson::Node citiesArr;
    citiesArr.type = SimpleJson::Node::Array;
    for (const auto& c : map.cities) {
        SimpleJson::Node pt;
        pt.type = SimpleJson::Node::Array;
        pt.arr.push_back(SimpleJson::Node(c.y));
        pt.arr.push_back(SimpleJson::Node(c.x));
        citiesArr.arr.push_back(pt);
    }
    root.obj["cities"] = citiesArr;

    // Villages
    SimpleJson::Node vilArr;
    vilArr.type = SimpleJson::Node::Array;
    for (const auto& v : map.villages) {
        SimpleJson::Node pt;
        pt.type = SimpleJson::Node::Array;
        pt.arr.push_back(SimpleJson::Node(v.y));
        pt.arr.push_back(SimpleJson::Node(v.x));
        vilArr.arr.push_back(pt);
    }
    root.obj["villages"] = vilArr;

    // Roads
    SimpleJson::Node roadArr;
    roadArr.type = SimpleJson::Node::Array;
    int totalRoadCells = 0;
    for (const auto& r : map.roads) {
        SimpleJson::Node rSub;
        rSub.type = SimpleJson::Node::Array;
        for (const auto& pt : r) {
            SimpleJson::Node p;
            p.type = SimpleJson::Node::Array;
            p.arr.push_back(SimpleJson::Node(pt.y));
            p.arr.push_back(SimpleJson::Node(pt.x));
            rSub.arr.push_back(p);
            totalRoadCells++;
        }
        roadArr.arr.push_back(rSub);
    }
    root.obj["roads"] = roadArr;

    // Biome distribution
    std::map<std::string, int> bCounts;
    for (const auto& b : map.biomes) bCounts[b]++;

    SimpleJson::Node bDist;
    bDist.type = SimpleJson::Node::Object;
    for (const auto& kv : bCounts) {
        bDist.obj[kv.first] = SimpleJson::Node(kv.second);
    }
    root.obj["biome_distribution"] = bDist;

    // Stats
    SimpleJson::Node stats;
    stats.type = SimpleJson::Node::Object;
    stats.obj["n_cities"] = SimpleJson::Node(static_cast<int>(map.cities.size()));
    stats.obj["n_villages"] = SimpleJson::Node(static_cast<int>(map.villages.size()));
    stats.obj["n_roads"] = SimpleJson::Node(static_cast<int>(map.roads.size()));
    stats.obj["n_road_cells"] = SimpleJson::Node(totalRoadCells);
    root.obj["stats"] = stats;

    // Elevação e Umidade completas salvas em grade para cálculos climáticos
    SimpleJson::Node gridElev;
    gridElev.type = SimpleJson::Node::Array;
    for (int y = 0; y < map.height; ++y) {
        SimpleJson::Node row;
        row.type = SimpleJson::Node::Array;
        for (int x = 0; x < map.width; ++x) {
            row.arr.push_back(SimpleJson::Node(map.elevation[y * map.width + x]));
        }
        gridElev.arr.push_back(row);
    }
    root.obj["elevation_grid"] = gridElev;

    SimpleJson::Node gridMoist;
    gridMoist.type = SimpleJson::Node::Array;
    for (int y = 0; y < map.height; ++y) {
        SimpleJson::Node row;
        row.type = SimpleJson::Node::Array;
        for (int x = 0; x < map.width; ++x) {
            row.arr.push_back(SimpleJson::Node(map.moisture[y * map.width + x]));
        }
        gridMoist.arr.push_back(row);
    }
    root.obj["moisture_grid"] = gridMoist;

    std::string jsonPath = "json/" + baseName + ".json";
    std::ofstream f(jsonPath);
    if (f.is_open()) {
        f << root.serialize(2, 0);
    }
    return jsonPath;
}

// Geração de um mapa completo
WorldMap generateWorld(const MapConfig& cfg, uint32_t seed, int /*mapIndex*/) {
    WorldMap map;
    map.width = cfg.width;
    map.height = cfg.height;
    map.seed = seed;

    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y%m%d_%H%M%S");
    map.timestamp = ss.str();

    std::mt19937 rng(seed);

    // Ruído base
    auto hNoise = PerlinNoise::generateSmoothNoise(cfg.height, cfg.width, 10, rng());
    auto rNoise = PerlinNoise::generateSmoothNoise(cfg.height, cfg.width, 15, rng());

    // Normaliza
    auto normalizeArr = [](std::vector<double>& arr) {
        double mn = arr[0], mx = arr[0];
        for (double v : arr) {
            if (v < mn) mn = v;
            if (v > mx) mx = v;
        }
        double range = (mx - mn > 1e-6) ? (mx - mn) : 1.0;
        for (double& v : arr) v = (v - mn) / range;
    };
    normalizeArr(hNoise);
    normalizeArr(rNoise);

    auto contNoise = PerlinNoise::generateSmoothNoise(cfg.height, cfg.width, 40, rng());
    normalizeArr(contNoise);

    map.elevation.resize(cfg.width * cfg.height);
    map.moisture = rNoise;

    for (int i = 0; i < cfg.width * cfg.height; ++i) {
        double h = 0.65 * hNoise[i] + 0.35 * contNoise[i];
        h = std::clamp(std::pow(h, 1.35), 0.0, 1.0);
        map.elevation[i] = h;
    }

    // Suavização com vizinhos (np.roll)
    std::vector<double> elevSmooth = map.elevation;
    for (int y = 0; y < cfg.height; ++y) {
        int yUp = (y - 1 + cfg.height) % cfg.height;
        int yDown = (y + 1) % cfg.height;
        for (int x = 0; x < cfg.width; ++x) {
            int xLeft = (x - 1 + cfg.width) % cfg.width;
            int xRight = (x + 1) % cfg.width;

            double vRoll = 0.25 * map.elevation[yDown * cfg.width + x] +
                           0.25 * map.elevation[yUp * cfg.width + x] +
                           0.25 * map.elevation[y * cfg.width + xRight] +
                           0.25 * map.elevation[y * cfg.width + xLeft];
            elevSmooth[y * cfg.width + x] = 0.70 * map.elevation[y * cfg.width + x] + 0.30 * vRoll;
        }
    }
    map.elevation = std::move(elevSmooth);

    // Suavização costeira
    for (int i = 0; i < cfg.width * cfg.height; ++i) {
        if (map.elevation[i] > 0.18 && map.elevation[i] < 0.26) {
            map.elevation[i] *= 0.92;
            map.moisture[i] *= 1.04;
        }
    }

    // Rios (15000 partículas simuladas descendo elevação)
    map.rivers.assign(cfg.width * cfg.height, 0.0);
    std::uniform_int_distribution<int> distY(1, cfg.height - 2);
    std::uniform_int_distribution<int> distX(1, cfg.width - 2);

    for (int it = 0; it < 15000; ++it) {
        int ry = distY(rng);
        int rx = distX(rng);
        for (int step = 0; step < 60; ++step) {
            int yMin = ry, xMin = rx;
            double hMin = map.elevation[ry * cfg.width + rx];

            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    int ny = ry + dy;
                    int nx = rx + dx;
                    if (ny >= 0 && ny < cfg.height && nx >= 0 && nx < cfg.width) {
                        double nh = map.elevation[ny * cfg.width + nx];
                        if (nh < hMin) {
                            hMin = nh;
                            yMin = ny;
                            xMin = nx;
                        }
                    }
                }
            }
            if (yMin == ry && xMin == rx) break;
            ry = yMin;
            rx = xMin;
            map.rivers[ry * cfg.width + rx] += 1.0;
        }
    }

    double maxRiv = 0.0;
    for (double r : map.rivers) if (r > maxRiv) maxRiv = r;
    if (maxRiv > 0.0) {
        for (double& r : map.rivers) r /= maxRiv;
    }

    // Biomas
    map.biomes = buildBiomeMap(map.elevation, map.moisture, cfg.width, cfg.height);

    // Assentamentos & Estradas
    std::vector<double> scores = calcScores(map.biomes, map.rivers, cfg.width, cfg.height);

    std::uniform_int_distribution<int> distC(cfg.min_cidades, cfg.max_cidades);
    std::uniform_int_distribution<int> distV(cfg.min_vilas, cfg.max_vilas);
    int nCities = distC(rng);
    int nVilas = distV(rng);

    map.cities = placeSettlements(scores, cfg.width, cfg.height, nCities, 14);
    auto villageCandidates = placeSettlements(scores, cfg.width, cfg.height, nVilas + nCities, 5);

    for (const auto& v : villageCandidates) {
        bool isCity = false;
        for (const auto& c : map.cities) {
            if (c == v) {
                isCity = true;
                break;
            }
        }
        if (!isCity && static_cast<int>(map.villages.size()) < nVilas) {
            map.villages.push_back(v);
        }
    }

    if (map.cities.size() >= 2) {
        map.roads = buildRoads(map.cities, map.villages, cfg.width, cfg.height, map.biomes);
    }

    return map;
}

int main(int argc, char* argv[]) {
    std::cout << "=========================================================\n";
    std::cout << "      World Generator C++ (Port Nativo de Alta Performance)\n";
    std::cout << "=========================================================\n\n";

    fs::create_directories("mapas");
    fs::create_directories("json");

    IniConfig ini;
    ini.load("clima.ini"); // Tenta carregar clima.ini se disponível para overrides opcionais

    MapConfig cfg;
    cfg.width = ini.getInt("mapa", "width", 100);
    cfg.height = ini.getInt("mapa", "height", 100);
    cfg.hex_size = ini.getDouble("mapa", "hex_size", 0.10);
    int defaultNumMapas = ini.getInt("gerador", "num_mapas", 5);
    cfg.num_mapas = (argc > 1 ? std::stoi(argv[1]) : defaultNumMapas);
    cfg.brightness = ini.getDouble("visual", "brightness", 1.0);
    cfg.angulo_luz_deg = ini.getDouble("visual", "angulo_luz_graus", 135.0);

    std::cout << "Configuração do Gerador:\n";
    std::cout << " - Resolução: " << cfg.width << "x" << cfg.height << "\n";
    std::cout << " - Quantidade de Mapas: " << cfg.num_mapas << "\n";
    std::cout << " - Tamanho Hexágono: " << cfg.hex_size << "\n";
    std::cout << " - Brilho: " << cfg.brightness << " | Ângulo de Luz: " << cfg.angulo_luz_deg << "°\n\n";

    std::random_device rd;

    for (int i = 0; i < cfg.num_mapas; ++i) {
        uint32_t seed = rd();
        std::cout << "Gerando mapa " << (i + 1) << "/" << cfg.num_mapas << " (seed=" << seed << ")...\n";

        WorldMap map = generateWorld(cfg, seed, i);

        std::cout << "  Renderizando cores e pipeline visual...\n";
        std::vector<ColorRGB> rgbMap = buildColorMap(map, cfg);

        std::cout << "  Desenhando hexágonos, estradas e cidades...\n";
        ImageCanvas canvas = renderWorldMap(map, rgbMap, cfg);

        std::string baseName = "mapa_" + std::to_string(i + 1) + "_" + map.timestamp;
        std::string bmpPath = "mapas/" + baseName + ".bmp";
        std::string ppmPath = "mapas/" + baseName + ".ppm";

        canvas.saveBMP(bmpPath);
        canvas.savePPM(ppmPath);

        std::string jsonPath = saveMapJson(map, cfg, baseName);

        std::cout << "✓ Mapa salvo em " << bmpPath << " e " << ppmPath << "\n";
        std::cout << "✓ JSON salvo em " << jsonPath << "\n";
        std::cout << "  " << map.cities.size() << " cidades · " << map.villages.size() << " vilas · " << map.roads.size() << " estradas\n\n";
    }

    std::cout << "Processamento concluído com sucesso!\n";
    return 0;
}
