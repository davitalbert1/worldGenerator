#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <algorithm>
#include <map>
#include "IniConfig.h"
#include "ImageCanvas.h"
#include "BiomeDef.h"
#include "SimpleJson.h"
#include "PerlinNoise.h"

namespace fs = std::filesystem;

static const double PI = 3.14159265358979323846;

struct ClimateParams {
    // Parâmetros Gerais
    double temp_media_global = 15.0; // Temperatura média global (°C)
    double temp_equador = 30.0; // Temperatura média no equador (°C)
    double temp_polos = -20.0; // Temperatura média nos polos (°C)
    double variacao_sazonal = 10.0; // Variação sazonal (°C)

    // Efeitos de Altitude e Oceano
    double taxa_lapse_altitude = 6.5; // Resfriamento por altitude (°C a cada 1000m ou taxa de elevação)
    double moderacao_termica_oceano = 0.35; // Oceano modera temperaturas extremas

    // Ventos
    double forca_vento_base = 25.0; // km/h
    double influencia_coriolis = 0.8; // Efeito da rotação planetária nos ventos
    int num_passos_simulacao_vento = 12; // Passos de advecção atmosférica

    // Umidade e Chuvas
    double evaporacao_oceano = 1.0; // Taxa de evaporação sobre água
    double taxa_precipitacao_orografica = 0.85; // Chuva orográfica ao bater em montanhas
    double chuva_minima_mm = 50.0; // mm / ano
    double chuva_maxima_mm = 3500.0; // mm / ano

    // Nível do Mar
    double nivel_do_mar = 0.20; // Altura abaixo da qual é considerado oceano

    void loadFromIni(const IniConfig& ini) {
        temp_media_global = ini.getDouble("temperatura", "temperatura_media_global", temp_media_global);
        temp_equador = ini.getDouble("temperatura", "temperatura_equador", temp_equador);
        temp_polos = ini.getDouble("temperatura", "temperatura_polos", temp_polos);
        variacao_sazonal = ini.getDouble("temperatura", "variacao_sazonal", variacao_sazonal);
        taxa_lapse_altitude = ini.getDouble("temperatura", "taxa_lapse_altitude", taxa_lapse_altitude);
        moderacao_termica_oceano = ini.getDouble("temperatura", "moderacao_termica_oceano", moderacao_termica_oceano);

        forca_vento_base = ini.getDouble("ventos", "forca_vento_base", forca_vento_base);
        influencia_coriolis = ini.getDouble("ventos", "influencia_coriolis", influencia_coriolis);
        num_passos_simulacao_vento = ini.getInt("ventos", "passos_simulacao", num_passos_simulacao_vento);

        evaporacao_oceano = ini.getDouble("chuvas", "evaporacao_oceano", evaporacao_oceano);
        taxa_precipitacao_orografica = ini.getDouble("chuvas", "taxa_orografica", taxa_precipitacao_orografica);
        chuva_minima_mm = ini.getDouble("chuvas", "chuva_minima_mm", chuva_minima_mm);
        chuva_maxima_mm = ini.getDouble("chuvas", "chuva_maxima_mm", chuva_maxima_mm);

        nivel_do_mar = ini.getDouble("mapa", "nivel_do_mar", nivel_do_mar);
    }
};

struct ClimateCell {
    double elevation = 0.0; // 0.0 a 1.0
    bool isOcean = false;
    double temperature = 0.0; // °C
    double windU = 0.0; // Vento componente X (Oeste -> Leste)
    double windV = 0.0; // Vento componente Y (Sul -> Norte)
    double windSpeed = 0.0; // Magnitude do vento
    double humidity = 0.0; // 0.0 a 1.0
    double rainfall = 0.0; // mm / ano
    std::string climateZone; // Polar, Temperado, Subtropical, Tropical, Equatorial, etc.
    std::string computedBiome;// Bioma baseado nos dados climáticos
};

struct ClimateGrid {
    int width = 100;
    int height = 100;
    std::vector<ClimateCell> cells;

    inline ClimateCell& at(int y, int x) {
        return cells[y * width + x];
    }
    inline const ClimateCell& at(int y, int x) const {
        return cells[y * width + x];
    }
};

// Carrega elevação a partir de um JSON exportado pelo gerador ou cria sintetizado
bool loadElevationFromJson(const std::string& jsonPath, ClimateGrid& grid) {
    auto root = SimpleJson::parseFile(jsonPath);
    if (!root.isObject()) return false;

    int w = root["parameters"]["width"].asInt(100);
    int h = root["parameters"]["height"].asInt(100);
    grid.width = w;
    grid.height = h;
    grid.cells.resize(w * h);

    const auto& elevGrid = root["elevation_grid"];
    if (elevGrid.isArray() && elevGrid.arr.size() == static_cast<size_t>(h)) {
        for (int y = 0; y < h; ++y) {
            const auto& row = elevGrid.arr[y];
            for (int x = 0; x < w; ++x) grid.at(y, x).elevation = row.arr[x].asDouble(0.0);
        }
        return true;
    }

    // Se o JSON não tiver a grade pura, gera um relevo proporcional ou usa ruído coerente
    return false;
}

// Determinação da zona climática térmica e de aridez
std::string determineClimateZone(double temp, double rain) {
    if (temp < -5.0) return "Glacial / Polar";
    if (temp < 5.0)  return "Boreal / Subpolar";
    if (temp < 15.0) {
        if (rain < 300) return "Temperado Árido (Estepe Fria)";
        if (rain < 800) return "Temperado Continental";
        return "Temperado Oceânico";
    }
    if (temp < 23.0) {
        if (rain < 250) return "Subtropical Árido (Deserto)";
        if (rain < 600) return "Subtropical Semiárido (Mediterrâneo)";
        return "Subtropical Úmido";
    }
    // Tropical / Equatorial
    if (rain < 250) return "Tropical Árido (Deserto Quente)";
    if (rain < 800) return "Tropical Semiárido (Savana/Caatinga)";
    if (rain < 1800) return "Tropical Monçônico";
    return "Equatorial Superúmido";
}

// Determinação do bioma de Whittaker expandido
std::string classifyWhittakerBiome(double temp, double rain, double elevation, bool isOcean) {
    if (isOcean) {
        if (elevation < 0.10) return "Oceano Profundo";
        return "Oceano Raso";
    }
    if (elevation >= 0.85) {
        if (temp < 0.0) return "Montanha Árida";
        if (rain > 1000.0) return "Montanha com Floresta";
        return "Montanha";
    }
    if (elevation <= 0.24) {
        if (temp > 20.0 && rain > 1200.0) return "Mangue Tropical";
        return "Praia";
    }

    if (temp < -5.0) return "Tundra";
    if (temp < 3.0) {
        if (rain < 400.0) return "Tundra";
        return "Taiga";
    }
    if (temp < 15.0) {
        if (rain < 300.0) return "Planície Árida";
        if (rain < 700.0) return "Planície";
        if (rain < 1400.0) return "Floresta Temperada";
        return "Pantanal";
    }
    // Climas quentes
    if (rain < 250.0) return "Deserto";
    if (rain < 500.0) return "Caatinga";
    if (rain < 1000.0) return "Cerrado";
    if (rain < 1600.0) return "Floresta Tropical";
    return "Floresta Tropical";
}

// Simulação Física Climática Completa
void calculateClimate(ClimateGrid& grid, const ClimateParams& params) {
    int w = grid.width;
    int h = grid.height;

    // 1. Identificar oceanos
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            ClimateCell& c = grid.at(y, x);
            c.isOcean = (c.elevation <= params.nivel_do_mar);
        }
    }

    // 2. Cálculo da Temperatura Base por Latitude + Altitude (Gradiente Adiabático)
    for (int y = 0; y < h; ++y) {
        // Latitude varia de -90° (Sul/y=0) até +90° (Norte/y=h-1). Equador no centro (y = h/2).
        double latNorm = std::abs((y - (h / 2.0)) / (h / 2.0)); // 0.0 no equador, 1.0 nos polos
        // Gradiente de insolação solar (seno / cosseno solar)
        double insolation = std::cos(latNorm * (PI / 2.0)); // 1.0 no equador, 0.0 no polo
        double baseTemp = params.temp_polos + (params.temp_equador - params.temp_polos) * insolation;

        for (int x = 0; x < w; ++x) {
            ClimateCell& c = grid.at(y, x);
            // Elevação relativa acima do nível do mar (0 a 1)
            double altAcimaMar = std::max(0.0, c.elevation - params.nivel_do_mar) / (1.0 - params.nivel_do_mar);
            // Queda térmica de altitude (~6.5°C por unidade normalizada de montanha)
            double lapseCooling = altAcimaMar * params.taxa_lapse_altitude * 3.5;

            double temp = baseTemp - lapseCooling;

            // Moderação térmica da água do mar
            if (c.isOcean) {
                temp = temp * (1.0 - params.moderacao_termica_oceano) + params.temp_media_global * params.moderacao_termica_oceano;
            }

            c.temperature = temp;
        }
    }

    // 3. Cálculo de Ventos Planetários (Células de Hadley, Ferrel, Polares + Força de Coriolis)
    for (int y = 0; y < h; ++y) {
        double latRel = (static_cast<double>(y) / (h - 1)) * 2.0 - 1.0; // -1 (Sul) a +1 (Norte)
        double absLat = std::abs(latRel);

        // Padrão de ventos zonais da Terra:
        // 0.0 - 0.33 (Alísios de Leste para Oeste -> U < 0)
        // 0.33 - 0.66 (Westerlies de Oeste para Leste -> U > 0)
        // 0.66 - 1.0 (Polares de Leste para Oeste -> U < 0)
        double uWind = 0.0;
        if (absLat < 0.33) {
            uWind = -std::sin(absLat / 0.33 * PI); // Alísios soprando para oeste
        } else if (absLat < 0.66) {
            uWind = std::sin((absLat - 0.33) / 0.33 * PI); // Ventos do oeste
        } else {
            uWind = -std::sin((absLat - 0.66) / 0.34 * PI); // Ventos polares
        }

        // Vento meridional (norte-sul) devido a convecção
        double vWind = (latRel >= 0) ? -0.3 * std::sin(absLat * PI) : 0.3 * std::sin(absLat * PI);

        for (int x = 0; x < w; ++x) {
            ClimateCell& c = grid.at(y, x);

            // Deflexão por montanhas (gradiente de elevação)
            int prevX = std::max(0, x - 1);
            int nextX = std::min(w - 1, x + 1);
            int prevY = std::max(0, y - 1);
            int nextY = std::min(h - 1, y + 1);

            double gradX = grid.at(y, nextX).elevation - grid.at(y, prevX).elevation;
            double gradY = grid.at(nextY, x).elevation - grid.at(prevY, x).elevation;

            // O vento tende a contornar montanhas altas
            double localU = uWind * params.forca_vento_base - gradX * 15.0;
            double localV = vWind * params.forca_vento_base - gradY * 15.0;

            c.windU = localU;
            c.windV = localV;
            c.windSpeed = std::sqrt(localU * localU + localV * localV);
        }
    }

    // 4. Ciclo de Umidade, Transporte de Massas de Ar e Chuvas Orográficas
    // Começa com alta umidade no oceano
    std::vector<double> moistureGrid(w * h, 0.0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (grid.at(y, x).isOcean) {
                // Água mais quente evapora mais
                double t = grid.at(y, x).temperature;
                double evap = params.evaporacao_oceano * std::clamp(1.0 + (t - 15.0) * 0.03, 0.4, 1.8);
                moistureGrid[y * w + x] = evap;
            }
        }
    }

    // Simulação de Advecção e Precipitação ao longo dos ventos
    std::vector<double> rainGrid(w * h, 0.0);

    for (int step = 0; step < params.num_passos_simulacao_vento; ++step) {
        std::vector<double> nextMoist = moistureGrid;

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const ClimateCell& c = grid.at(y, x);
                double m = moistureGrid[y * w + x];
                if (m <= 0.001) continue;

                // Deslocamento pelo vetor de vento (vento sopra com força variável)
                double speed = std::max(0.1, c.windSpeed);
                double dirX = (c.windU / speed) * 1.5;
                double dirY = (c.windV / speed) * 1.5;

                int targetX = std::clamp(x + static_cast<int>(std::round(dirX)), 0, w - 1);
                int targetY = std::clamp(y + static_cast<int>(std::round(dirY)), 0, h - 1);

                double targetElev = grid.at(targetY, targetX).elevation;
                double currentElev = c.elevation;

                // Precipitação: mais alta ao subir relevo (chuva orográfica), mas também existe precipitação convectiva
                double deltaElev = targetElev - currentElev;
                double precip = 0.0;

                if (deltaElev > 0.01) {
                    precip = m * (0.15 + params.taxa_precipitacao_orografica * deltaElev * 2.5);
                } else {
                    // Precipitação convectiva sobre terra e oceano
                    precip = m * 0.12;
                }

                precip = std::clamp(precip, 0.0, m);
                rainGrid[targetY * w + targetX] += precip;

                // Transporte de umidade
                double remaining = (m - precip) * 0.88;
                nextMoist[targetY * w + targetX] += remaining * 0.70;

                // Evaporação contínua e recarga nos oceanos
                if (c.isOcean) {
                    double evap = params.evaporacao_oceano * std::clamp(1.0 + (c.temperature - 15.0) * 0.03, 0.5, 2.0);
                    nextMoist[y * w + x] = std::max(nextMoist[y * w + x], evap);
                }
            }
        }
        moistureGrid = std::move(nextMoist);
    }

    // Normalização e escala das chuvas com percentil / saturação suave para distribuição natural
    std::vector<double> sortedRain = rainGrid;
    std::sort(sortedRain.begin(), sortedRain.end());
    double p95Rain = sortedRain[static_cast<size_t>(sortedRain.size() * 0.95)];
    if (p95Rain < 1e-4) p95Rain = 1.0;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            ClimateCell& c = grid.at(y, x);
            double normR = std::clamp(rainGrid[y * w + x] / p95Rain, 0.0, 1.0);
            normR = std::pow(normR, 0.75); // Curva suave para preencher biomas médios

            c.humidity = std::clamp(moistureGrid[y * w + x] / 1.5, 0.0, 1.0);
            c.rainfall = params.chuva_minima_mm + normR * (params.chuva_maxima_mm - params.chuva_minima_mm);

            // Zonas e Biomas
            c.climateZone = determineClimateZone(c.temperature, c.rainfall);
            c.computedBiome = classifyWhittakerBiome(c.temperature, c.rainfall, c.elevation, c.isOcean);
        }
    }
}

// Mapas de Cores Térmicos e de Chuva
ColorRGB getTemperatureColor(double temp) {
    // Escala: -30°C (Azul/Roxo escuro) -> 0°C (Ciano) -> 15°C (Verde) -> 25°C (Amarelo) -> 40°C+ (Vermelho escuro)
    double tNorm = std::clamp((temp + 25.0) / 65.0, 0.0, 1.0);
    if (tNorm < 0.25) {
        double f = tNorm / 0.25;
        return {static_cast<uint8_t>(30 + f * 40), static_cast<uint8_t>(30 + f * 100), static_cast<uint8_t>(180 + f * 75)};
    } else if (tNorm < 0.5) {
        double f = (tNorm - 0.25) / 0.25;
        return {static_cast<uint8_t>(70 + f * 30), static_cast<uint8_t>(130 + f * 90), static_cast<uint8_t>(255 - f * 155)};
    } else if (tNorm < 0.75) {
        double f = (tNorm - 0.5) / 0.25;
        return {static_cast<uint8_t>(100 + f * 150), static_cast<uint8_t>(220 - f * 20), static_cast<uint8_t>(100 - f * 80)};
    } else {
        double f = (tNorm - 0.75) / 0.25;
        return {static_cast<uint8_t>(250), static_cast<uint8_t>(200 - f * 160), static_cast<uint8_t>(20)};
    }
}

ColorRGB getRainfallColor(double rain) {
    // Escala: 0mm (Marrom claro seco) -> 500mm (Amarelo) -> 1200mm (Verde) -> 2500mm+ (Azul marinho escuro)
    double rNorm = std::clamp(rain / 3000.0, 0.0, 1.0);
    if (rNorm < 0.2) {
        double f = rNorm / 0.2;
        return {static_cast<uint8_t>(190 - f * 30), static_cast<uint8_t>(160 - f * 10), static_cast<uint8_t>(110 - f * 50)};
    } else if (rNorm < 0.5) {
        double f = (rNorm - 0.2) / 0.3;
        return {static_cast<uint8_t>(160 - f * 110), static_cast<uint8_t>(150 + f * 50), static_cast<uint8_t>(60 + f * 40)};
    } else {
        double f = (rNorm - 0.5) / 0.5;
        return {static_cast<uint8_t>(50 - f * 40), static_cast<uint8_t>(200 - f * 120), static_cast<uint8_t>(100 + f * 140)};
    }
}

// Renderização das Camadas Climáticas
void renderClimateMaps(const ClimateGrid& grid, const std::string& prefix) {
    int w = grid.width;
    int h = grid.height;
    int scale = 8;
    int imgW = w * scale;
    int imgH = h * scale;

    ImageCanvas tempCanvas(imgW, imgH);
    ImageCanvas rainCanvas(imgW, imgH);
    ImageCanvas windCanvas(imgW, imgH);
    ImageCanvas biomeCanvas(imgW, imgH);

    const auto& bDefs = getBiomesDefinition();

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const auto& c = grid.at(y, x);
            ColorRGB tCol = getTemperatureColor(c.temperature);
            ColorRGB rCol = getRainfallColor(c.rainfall);

            ColorRGB bCol = {100, 100, 100};
            auto it = bDefs.find(c.computedBiome);
            if (it != bDefs.end()) {
                bCol = ColorRGB::fromHex(it->second.colorHex);
            }

            // Vento intensidade em tons de ciano/azul claro
            double wNorm = std::clamp(c.windSpeed / 50.0, 0.0, 1.0);
            ColorRGB wCol = {
                static_cast<uint8_t>(20 + wNorm * 180),
                static_cast<uint8_t>(30 + wNorm * 210),
                static_cast<uint8_t>(60 + wNorm * 190)
            };

            for (int dy = 0; dy < scale; ++dy) {
                for (int dx = 0; dx < scale; ++dx) {
                    int px = x * scale + dx;
                    int py = (h - 1 - y) * scale + dy;
                    tempCanvas.setPixel(px, py, tCol);
                    rainCanvas.setPixel(px, py, rCol);
                    windCanvas.setPixel(px, py, wCol);
                    biomeCanvas.setPixel(px, py, bCol);
                }
            }
        }
    }

    // Desenhar vetores de vento discretos no mapa de vento
    int step = 5;
    for (int y = 2; y < h; y += step) {
        for (int x = 2; x < w; x += step) {
            const auto& c = grid.at(y, x);
            double cx = x * scale + scale * 0.5;
            double cy = (h - 1 - y) * scale + scale * 0.5;
            double vx = (c.windU / (c.windSpeed + 0.001)) * (scale * 2.5);
            double vy = -(c.windV / (c.windSpeed + 0.001)) * (scale * 2.5);
            windCanvas.drawLine(cx, cy, cx + vx, cy + vy, {255, 255, 255}, 1.5, 0.85);
            windCanvas.drawFilledCircle(cx + vx, cy + vy, 2.0, {255, 200, 50}, 0.9);
        }
    }

    fs::create_directories("clima_output");
    std::string pathTemp = "clima_output/" + prefix + "_temperatura.bmp";
    std::string pathRain = "clima_output/" + prefix + "_chuva.bmp";
    std::string pathWind = "clima_output/" + prefix + "_ventos.bmp";
    std::string pathBiome = "clima_output/" + prefix + "_biomas_climaticos.bmp";

    tempCanvas.saveBMP(pathTemp);
    rainCanvas.saveBMP(pathRain);
    windCanvas.saveBMP(pathWind);
    biomeCanvas.saveBMP(pathBiome);

    std::cout << "✓ Camadas climáticas salvas com sucesso em clima_output/:\n";
    std::cout << "  - " << pathTemp << "\n";
    std::cout << "  - " << pathRain << "\n";
    std::cout << "  - " << pathWind << "\n";
    std::cout << "  - " << pathBiome << "\n";
}

// Exportar Relatório Climático JSON
void saveClimateReportJson(const ClimateGrid& grid, const ClimateParams& params, const std::string& prefix) {
    SimpleJson::Node root;
    root.type = SimpleJson::Node::Object;

    SimpleJson::Node cfg;
    cfg.type = SimpleJson::Node::Object;
    cfg.obj["temperatura_media_global"] = SimpleJson::Node(params.temp_media_global);
    cfg.obj["temperatura_equador"] = SimpleJson::Node(params.temp_equador);
    cfg.obj["temperatura_polos"] = SimpleJson::Node(params.temp_polos);
    cfg.obj["forca_vento_base"] = SimpleJson::Node(params.forca_vento_base);
    cfg.obj["taxa_orografica"] = SimpleJson::Node(params.taxa_precipitacao_orografica);
    root.obj["parameters"] = cfg;

    // Estatísticas Globais
    double sumTemp = 0, sumRain = 0, sumWind = 0;
    double minT = 1e9, maxT = -1e9;
    double minR = 1e9, maxR = -1e9;
    std::map<std::string, int> zoneCount;
    std::map<std::string, int> biomeCount;

    int total = grid.width * grid.height;
    for (const auto& c : grid.cells) {
        sumTemp += c.temperature;
        sumRain += c.rainfall;
        sumWind += c.windSpeed;
        if (c.temperature < minT) minT = c.temperature;
        if (c.temperature > maxT) maxT = c.temperature;
        if (c.rainfall < minR) minR = c.rainfall;
        if (c.rainfall > maxR) maxR = c.rainfall;
        zoneCount[c.climateZone]++;
        biomeCount[c.computedBiome]++;
    }

    SimpleJson::Node stats;
    stats.type = SimpleJson::Node::Object;
    stats.obj["temperatura_media_calculada"] = SimpleJson::Node(sumTemp / total);
    stats.obj["temperatura_minima"] = SimpleJson::Node(minT);
    stats.obj["temperatura_maxima"] = SimpleJson::Node(maxT);
    stats.obj["chuva_media_anual_mm"] = SimpleJson::Node(sumRain / total);
    stats.obj["chuva_minima_mm"] = SimpleJson::Node(minR);
    stats.obj["chuva_maxima_mm"] = SimpleJson::Node(maxR);
    stats.obj["vento_medio_kmh"] = SimpleJson::Node(sumWind / total);
    root.obj["stats"] = stats;

    SimpleJson::Node zObj;
    zObj.type = SimpleJson::Node::Object;
    for (const auto& kv : zoneCount) zObj.obj[kv.first] = SimpleJson::Node(kv.second);
    root.obj["zonas_climaticas"] = zObj;

    SimpleJson::Node bObj;
    bObj.type = SimpleJson::Node::Object;
    for (const auto& kv : biomeCount) bObj.obj[kv.first] = SimpleJson::Node(kv.second);
    root.obj["biomas_resultantes"] = bObj;

    std::string outPath = "clima_output/" + prefix + "_relatorio.json";
    std::ofstream f(outPath);
    if (f.is_open()) {
        f << root.serialize(2, 0);
    }
    std::cout << "✓ Relatório estatístico salvo em " << outPath << "\n";
}

int main(int argc, char* argv[]) {
    std::cout << "=========================================================\n";
    std::cout << "   Climate Calculator C++ (Simulação Climática Automática)\n";
    std::cout << "=========================================================\n\n";

    std::string iniFile = "clima.ini";
    std::string inputMap = "";

    // Leitura dos argumentos CLI
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" || arg == "-c") {
            if (i + 1 < argc) iniFile = argv[++i];
        } else if (arg == "--mapa" || arg == "-m") {
            if (i + 1 < argc) inputMap = argv[++i];
        } else if (inputMap.empty() && arg.find(".json") != std::string::npos) {
            inputMap = arg;
        } else if (arg.find(".ini") != std::string::npos) {
            iniFile = arg;
        }
    }

    IniConfig ini;
    if (ini.load(iniFile)) {
        std::cout << "Arquivo de configuração carregado: " << iniFile << "\n";
    } else {
        std::cout << "Aviso: " << iniFile << " não encontrado ou vazio. Usando padrões físicos.\n";
    }

    ClimateParams params;
    params.loadFromIni(ini);

    std::cout << "\nParâmetros Físicos Ativos:\n";
    std::cout << "  - Temperatura Média Global : " << params.temp_media_global << " °C\n";
    std::cout << "  - Temperatura no Equador   : " << params.temp_equador << " °C\n";
    std::cout << "  - Temperatura nos Polos    : " << params.temp_polos << " °C\n";
    std::cout << "  - Taxa Lapse (Altitude)   : " << params.taxa_lapse_altitude << " °C/1000m\n";
    std::cout << "  - Vento Base Planetário    : " << params.forca_vento_base << " km/h\n";
    std::cout << "  - Evaporação Oceânica      : " << params.evaporacao_oceano << "\n";
    std::cout << "  - Precipitação Orográfica  : " << params.taxa_precipitacao_orografica << "\n\n";

    ClimateGrid grid;
    std::string basePrefix = "simulacao";

    // Se nenhum mapa foi especificado, procura mapas existentes na pasta json/
    if (inputMap.empty()) {
        if (fs::exists("json")) {
            for (const auto& entry : fs::directory_iterator("json")) {
                if (entry.path().extension() == ".json") {
                    inputMap = entry.path().string();
                    break;
                }
            }
        }
    }

    bool loaded = false;
    if (!inputMap.empty() && fs::exists(inputMap)) {
        std::cout << "Carregando topografia do mapa: " << inputMap << "\n";
        loaded = loadElevationFromJson(inputMap, grid);
        basePrefix = fs::path(inputMap).stem().string();
    }

    // Se ainda não tiver elevação carregada, sintetiza relevo com Perlin Noise
    if (!loaded) {
        std::cout << "Gerando terreno base procedural para cálculo...\n";
        grid.width = ini.getInt("mapa", "width", 100);
        grid.height = ini.getInt("mapa", "height", 100);
        grid.cells.resize(grid.width * grid.height);

        std::random_device rd;
        uint32_t s = rd();
        auto elevNoise = PerlinNoise::generateSmoothNoise(grid.height, grid.width, 12, s);
        for (int i = 0; i < grid.width * grid.height; ++i) {
            grid.cells[i].elevation = elevNoise[i];
        }
        basePrefix = "mapa_procedural";
    }

    std::cout << "Simulando atmosfera, ventos, convecção e chuvas...\n";
    calculateClimate(grid, params);

    std::cout << "Renderizando mapas temáticos de clima...\n";
    renderClimateMaps(grid, basePrefix);

    saveClimateReportJson(grid, params, basePrefix);

    std::cout << "\n=========================================================\n";
    std::cout << "   Simulação e Cálculo Climático Concluídos com Sucesso!\n";
    std::cout << "=========================================================\n";
    return 0;
}
