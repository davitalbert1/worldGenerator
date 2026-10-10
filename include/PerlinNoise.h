#pragma once

#include <vector>
#include <random>
#include <cmath>
#include <algorithm>

class PerlinNoise {
private:
    std::vector<int> p;

    static double fade(double t) {
        return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
    }

    static double lerp(double t, double a, double b) {
        return a + t * (b - a);
    }

    static double grad(int hash, double x, double y) {
        int h = hash & 7;
        double u = h < 4 ? x : y;
        double v = h < 4 ? y : x;
        return ((h & 1) ? -u : u) + ((h & 2) ? -2.0 * v : 2.0 * v);
    }

public:
    PerlinNoise(uint32_t seed = 0) {
        reseed(seed);
    }

    void reseed(uint32_t seed) {
        p.resize(256);
        for (int i = 0; i < 256; ++i) p[i] = i;
        std::mt19937 g(seed);
        std::shuffle(p.begin(), p.end(), g);
        p.insert(p.end(), p.begin(), p.end());
    }

    double noise2D(double x, double y) const {
        int X = static_cast<int>(std::floor(x)) & 255;
        int Y = static_cast<int>(std::floor(y)) & 255;

        x -= std::floor(x);
        y -= std::floor(y);

        double u = fade(x);
        double v = fade(y);

        int A = p[X] + Y;
        int B = p[X + 1] + Y;

        return lerp(v,
                    lerp(u, grad(p[A], x, y), grad(p[B], x - 1, y)),
                    lerp(u, grad(p[A + 1], x, y - 1), grad(p[B + 1], x - 1, y - 1)));
    }

    // Geração de ruído suave 2D equivalente ao smooth_noise da versão Python
    static std::vector<double> generateSmoothNoise(int height, int width, int scale, uint32_t seed) {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<double> dist(0.0, 1.0);

        int gridH = height / scale + 3;
        int gridW = width / scale + 3;
        std::vector<double> base(gridH * gridW);
        for (int i = 0; i < gridH * gridW; ++i) {
            base[i] = dist(rng);
        }

        std::vector<double> out(height * width, 0.0);

        double maxH = static_cast<double>(gridH - 2);
        double maxW = static_cast<double>(gridW - 2);

        for (int y = 0; y < height; ++y) {
            double fy = (height > 1) ? (static_cast<double>(y) / (height - 1) * maxH) : 0.0;
            int y0 = static_cast<int>(std::floor(fy));
            int y1 = std::min(y0 + 1, gridH - 1);
            double fracY = fy - y0;

            for (int x = 0; x < width; ++x) {
                double fx = (width > 1) ? (static_cast<double>(x) / (width - 1) * maxW) : 0.0;
                int x0 = static_cast<int>(std::floor(fx));
                int x1 = std::min(x0 + 1, gridW - 1);
                double fracX = fx - x0;

                double v00 = base[y0 * gridW + x0];
                double v10 = base[y0 * gridW + x1];
                double v01 = base[y1 * gridW + x0];
                double v11 = base[y1 * gridW + x1];

                double val = v00 * (1.0 - fracX) * (1.0 - fracY) +
                             v10 * fracX * (1.0 - fracY) +
                             v01 * (1.0 - fracX) * fracY +
                             v11 * fracX * fracY;

                out[y * width + x] = val;
            }
        }
        return out;
    }
};
