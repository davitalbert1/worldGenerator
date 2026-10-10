#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <cmath>
#include <algorithm>

struct BiomeData {
    std::string name;
    std::string colorHex;
    double hMin;
    double hMax;
    double rMin;
    double rMax;
    double score;
    int cost;
};

inline const std::map<std::string, BiomeData>& getBiomesDefinition() {
    static const std::map<std::string, BiomeData> BIOMES = {
        {"Oceano Profundo", {"Oceano Profundo", "#0c1e36", 0.0, 0.20, 0.0, 0.40, -1.0, 999}},
        {"Oceano Raso", {"Oceano Raso", "#1e4e72", 0.0, 0.20, 0.4, 1.01, -1.0, 999}},
        {"Praia", {"Praia", "#b8a882", 0.2, 0.25, 0.0, 1.01, 2.0, 1}},
        {"Deserto", {"Deserto", "#a08050", 0.3, 0.60, 0.0, 0.20, -1.0, 2}},
        {"Caatinga", {"Caatinga", "#806040", 0.3, 0.60, 0.2, 0.30, 1.0, 1}},
        {"Cerrado", {"Cerrado", "#6a7038", 0.3, 0.60, 0.3, 0.45, 3.0, 1}},
        {"Planície", {"Planície", "#507038", 0.3, 0.60, 0.45, 0.60, 3.0, 1}},
        {"Pantanal", {"Pantanal", "#2e5048", 0.3, 0.60, 0.6, 0.80, 1.0, 3}},
        {"Floresta Tropical", {"Floresta Tropical", "#1e4820", 0.3, 0.60, 0.8, 1.01, 1.0, 1}},
        {"Planalto Seco", {"Planalto Seco", "#8a6840", 0.6, 0.85, 0.0, 0.20, 1.0, 1}},
        {"Planície Árida", {"Planície Árida", "#705840", 0.6, 0.85, 0.2, 0.50, 1.0, 1}},
        {"Floresta Temperada", {"Floresta Temperada", "#2e5430", 0.6, 0.85, 0.5, 0.70, 3.0, 1}},
        {"Taiga", {"Taiga", "#243828", 0.6, 0.85, 0.7, 1.01, 1.0, 2}},
        {"Tundra", {"Tundra", "#7a9aaa", 0.0, 1.01, 0.0, 1.01, -1.0, 2}},
        {"Montanha Árida", {"Montanha Árida", "#505058", 0.85, 1.01, 0.0, 0.30, -1.0, 8}},
        {"Montanha", {"Montanha", "#606070", 0.85, 1.01, 0.3, 0.60, -1.0, 8}},
        {"Montanha com Floresta", {"Montanha com Floresta", "#3e5040", 0.85, 1.01, 0.6, 1.01, -1.0, 8}},
        {"Mangue Tropical", {"Mangue Tropical", "#1e3028", 0.25, 0.30, 0.6, 1.01, 1.0, 3}}
    };
    return BIOMES;
}

inline std::string determineBiome(double h, double r) {
    const auto& biomes = getBiomesDefinition();
    for (const auto& kv : biomes) {
        if (kv.first == "Tundra") continue; // Tundra é resolvida por latitude/temperatura
        if (h >= kv.second.hMin && h < kv.second.hMax && r >= kv.second.rMin && r < kv.second.rMax) {
            return kv.first;
        }
    }
    return "Planície";
}
