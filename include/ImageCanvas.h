#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <fstream>
#include <cmath>
#include <algorithm>

struct ColorRGB {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    static ColorRGB fromHex(const std::string& hexStr) {
        std::string s = hexStr;
        if (!s.empty() && s[0] == '#') s = s.substr(1);
        if (s.size() != 6) return {0, 0, 0};
        unsigned int val = 0;
        try {
            val = std::stoul(s, nullptr, 16);
        } catch (...) {
            return {0, 0, 0};
        }
        return {
            static_cast<uint8_t>((val >> 16) & 0xFF),
            static_cast<uint8_t>((val >> 8) & 0xFF),
            static_cast<uint8_t>(val & 0xFF)
        };
    }

    static ColorRGB fromNormalized(double rf, double gf, double bf) {
        rf = std::clamp(rf, 0.0, 1.0);
        gf = std::clamp(gf, 0.0, 1.0);
        bf = std::clamp(bf, 0.0, 1.0);
        return {
            static_cast<uint8_t>(std::round(rf * 255.0)),
            static_cast<uint8_t>(std::round(gf * 255.0)),
            static_cast<uint8_t>(std::round(bf * 255.0))
        };
    }
};

class ImageCanvas {
public:
    int width = 0;
    int height = 0;
    std::vector<ColorRGB> pixels;

    ImageCanvas() = default;
    ImageCanvas(int w, int h, ColorRGB bg = {6, 6, 8}) : width(w), height(h), pixels(w * h, bg) {}

    void setPixel(int x, int y, ColorRGB c) {
        if (x < 0 || x >= width || y < 0 || y >= height) return;
        pixels[y * width + x] = c;
    }

    ColorRGB getPixel(int x, int y) const {
        if (x < 0 || x >= width || y < 0 || y >= height) return {0, 0, 0};
        return pixels[y * width + x];
    }

    void blendPixel(int x, int y, ColorRGB c, double alpha) {
        if (x < 0 || x >= width || y < 0 || y >= height) return;
        alpha = std::clamp(alpha, 0.0, 1.0);
        ColorRGB cur = pixels[y * width + x];
        uint8_t nr = static_cast<uint8_t>(cur.r * (1.0 - alpha) + c.r * alpha);
        uint8_t ng = static_cast<uint8_t>(cur.g * (1.0 - alpha) + c.g * alpha);
        uint8_t nb = static_cast<uint8_t>(cur.b * (1.0 - alpha) + c.b * alpha);
        pixels[y * width + x] = {nr, ng, nb};
    }

    void drawFilledCircle(double cx, double cy, double radius, ColorRGB color, double alpha = 1.0) {
        int minX = std::max(0, static_cast<int>(std::floor(cx - radius - 1.0)));
        int maxX = std::min(width - 1, static_cast<int>(std::ceil(cx + radius + 1.0)));
        int minY = std::max(0, static_cast<int>(std::floor(cy - radius - 1.0)));
        int maxY = std::min(height - 1, static_cast<int>(std::ceil(cy + radius + 1.0)));

        double r2 = radius * radius;
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                double dx = x - cx;
                double dy = y - cy;
                double dist2 = dx * dx + dy * dy;
                if (dist2 <= r2) {
                    blendPixel(x, y, color, alpha);
                }
            }
        }
    }

    void drawLine(double x0, double y0, double x1, double y1, ColorRGB color, double thickness = 1.0, double alpha = 1.0) {
        double dx = x1 - x0;
        double dy = y1 - y0;
        double len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.001) {
            drawFilledCircle(x0, y0, thickness * 0.5, color, alpha);
            return;
        }

        int steps = static_cast<int>(std::ceil(len * 2.0));
        double rad = std::max(0.5, thickness * 0.5);
        for (int i = 0; i <= steps; ++i) {
            double t = static_cast<double>(i) / steps;
            double px = x0 + t * dx;
            double py = y0 + t * dy;
            drawFilledCircle(px, py, rad, color, alpha);
        }
    }

    void drawFilledPolygon(const std::vector<std::pair<double, double>>& poly, ColorRGB color, double alpha = 1.0) {
        if (poly.size() < 3) return;

        double minY = poly[0].second, maxY = poly[0].second;
        for (const auto& pt : poly) {
            if (pt.second < minY) minY = pt.second;
            if (pt.second > maxY) maxY = pt.second;
        }

        int startY = std::max(0, static_cast<int>(std::floor(minY)));
        int endY = std::min(height - 1, static_cast<int>(std::ceil(maxY)));

        for (int y = startY; y <= endY; ++y) {
            std::vector<double> nodeX;
            size_t j = poly.size() - 1;
            for (size_t i = 0; i < poly.size(); ++i) {
                if ((poly[i].second < y && poly[j].second >= y) || (poly[j].second < y && poly[i].second >= y)) {
                    double x = poly[i].first + (y - poly[i].second) / (poly[j].second - poly[i].second) * (poly[j].first - poly[i].first);
                    nodeX.push_back(x);
                }
                j = i;
            }
            std::sort(nodeX.begin(), nodeX.end());

            for (size_t i = 0; i + 1 < nodeX.size(); i += 2) {
                int startX = std::max(0, static_cast<int>(std::floor(nodeX[i])));
                int endX = std::min(width - 1, static_cast<int>(std::ceil(nodeX[i + 1])));
                for (int x = startX; x <= endX; ++x) {
                    blendPixel(x, y, color, alpha);
                }
            }
        }
    }

    void drawPolygonOutline(const std::vector<std::pair<double, double>>& poly, ColorRGB color, double thickness = 1.0, double alpha = 1.0) {
        for (size_t i = 0; i < poly.size(); ++i) {
            size_t next = (i + 1) % poly.size();
            drawLine(poly[i].first, poly[i].second, poly[next].first, poly[next].second, color, thickness, alpha);
        }
    }

    void drawStar(double cx, double cy, double rOuter, double rInner, int points, ColorRGB color, double alpha = 1.0) {
        std::vector<std::pair<double, double>> poly;
        double angleStep = 3.14159265358979323846 / points;
        for (int i = 0; i < 2 * points; ++i) {
            double r = (i % 2 == 0) ? rOuter : rInner;
            double a = i * angleStep - 3.14159265358979323846 / 2.0;
            poly.push_back({cx + r * std::cos(a), cy + r * std::sin(a)});
        }
        drawFilledPolygon(poly, color, alpha);
    }

    bool saveBMP(const std::string& filename) const {
        std::ofstream file(filename, std::ios::binary);
        if (!file.is_open()) return false;

        uint32_t rowSize = ((3 * width + 3) / 4) * 4;
        uint32_t imageSize = rowSize * height;
        uint32_t fileSize = 54 + imageSize;

        uint8_t header[54] = {
            'B', 'M',
            static_cast<uint8_t>(fileSize & 0xFF),
            static_cast<uint8_t>((fileSize >> 8) & 0xFF),
            static_cast<uint8_t>((fileSize >> 16) & 0xFF),
            static_cast<uint8_t>((fileSize >> 24) & 0xFF),
            0, 0, 0, 0,
            54, 0, 0, 0, // offset
            40, 0, 0, 0, // info header size
            static_cast<uint8_t>(width & 0xFF),
            static_cast<uint8_t>((width >> 8) & 0xFF),
            static_cast<uint8_t>((width >> 16) & 0xFF),
            static_cast<uint8_t>((width >> 24) & 0xFF),
            static_cast<uint8_t>(height & 0xFF),
            static_cast<uint8_t>((height >> 8) & 0xFF),
            static_cast<uint8_t>((height >> 16) & 0xFF),
            static_cast<uint8_t>((height >> 24) & 0xFF),
            1, 0, // planes
            24, 0, // bits per pixel
            0, 0, 0, 0, // compression
            static_cast<uint8_t>(imageSize & 0xFF),
            static_cast<uint8_t>((imageSize >> 8) & 0xFF),
            static_cast<uint8_t>((imageSize >> 16) & 0xFF),
            static_cast<uint8_t>((imageSize >> 24) & 0xFF),
            0x13, 0x0B, 0, 0, // 2835 ppm x
            0x13, 0x0B, 0, 0, // 2835 ppm y
            0, 0, 0, 0,
            0, 0, 0, 0
        };

        file.write(reinterpret_cast<const char*>(header), 54);

        std::vector<uint8_t> rowBuffer(rowSize, 0);
        for (int y = height - 1; y >= 0; --y) { // BMP armazena de baixo pra cima
            for (int x = 0; x < width; ++x) {
                ColorRGB c = pixels[y * width + x];
                rowBuffer[x * 3 + 0] = c.b;
                rowBuffer[x * 3 + 1] = c.g;
                rowBuffer[x * 3 + 2] = c.r;
            }
            file.write(reinterpret_cast<const char*>(rowBuffer.data()), rowSize);
        }
        return true;
    }

    bool savePPM(const std::string& filename) const {
        std::ofstream file(filename, std::ios::binary);
        if (!file.is_open()) return false;
        file << "P6\n" << width << " " << height << "\n255\n";
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                ColorRGB c = pixels[y * width + x];
                file.put(static_cast<char>(c.r));
                file.put(static_cast<char>(c.g));
                file.put(static_cast<char>(c.b));
            }
        }
        return true;
    }
};
