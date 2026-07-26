#pragma once
// Lesenka Durachkov
#include <array>
#include <cstdint>
#include <algorithm>
#include <glm/vec4.hpp>

struct Piece {
public:
    static constexpr size_t width = 4;
    static constexpr size_t height = 4;
    // ! Every next line of data = +Y, not -Y, so shapes should be built upside down and flipped horizontally to be correct.
    static constexpr std::array<std::array<std::array<uint8_t, Piece::width>, Piece::height>, 7> defaultShapes = {{
        {{ // I
            { 0, 5, 0, 0 },
            { 0, 5, 0, 0 },
            { 0, 5, 0, 0 },
            { 0, 5, 0, 0 },
        }},
        {{ // J
            { 0, 6, 6, 0 },
            { 0, 6, 0, 0 },
            { 0, 6, 0, 0 },
            { 0, 0, 0, 0 },
        }},
        {{ // L
            { 0, 2, 2, 0 },
            { 0, 0, 2, 0 },
            { 0, 0, 2, 0 },
            { 0, 0, 0, 0 },
        }},
        {{ // O
            { 0, 0, 0, 0 },
            { 0, 3, 3, 0 },
            { 0, 3, 3, 0 },
            { 0, 0, 0, 0 },
        }},
        {{ // Z
            { 0, 0, 0, 0 },
            { 0, 4, 4, 0 },
            { 0, 0, 4, 4 },
            { 0, 0, 0, 0 },
        }},
        {{ // T
            { 0, 0, 0, 0 },
            { 0, 7, 7, 7 },
            { 0, 0, 7, 0 },
            { 0, 0, 0, 0 },
        }},
        {{ // S
            { 0, 0, 0, 0 },
            { 0, 0, 1, 1 },
            { 0, 1, 1, 0 },
            { 0, 0, 0, 0 },
        }}
    }};
private:
    std::array<std::array<uint8_t, Piece::width>, Piece::height> prefab, grid;
    glm::u8vec4 bounds;
public:
    int16_t x, y;
    uint8_t orientation;
    Piece(int16_t x, int16_t y, uint8_t orientation) : prefab(), grid(), x(x), y(y), orientation(orientation) {}
    ~Piece() = default;
    
    void rotateTo(uint8_t orientation) {
        this->orientation = orientation % 4;
        this->bounds = glm::u8vec4(Piece::width, Piece::height, 0, 0);
        for (size_t row = 0; row < Piece::height; row++) {
            for (size_t col = 0; col < Piece::width; col++) {
                switch (this->orientation) {
                    case 0: this->grid[row][col] = this->prefab[row][col]; break;
                    case 1: this->grid[row][col] = this->prefab[Piece::width - 1 - col][row]; break;
                    case 2: this->grid[row][col] = this->prefab[Piece::height - 1 - row][Piece::width - 1 - col]; break;
                    case 3: this->grid[row][col] = this->prefab[col][Piece::height - 1 - row]; break;
                }
                if (this->grid[row][col] != 0) {
                    this->bounds.x = std::min(this->bounds.x, static_cast<uint8_t>(col));
                    this->bounds.y = std::min(this->bounds.y, static_cast<uint8_t>(row));
                    this->bounds.z = std::max(this->bounds.z, static_cast<uint8_t>(col));
                    this->bounds.w = std::max(this->bounds.w, static_cast<uint8_t>(row));
                }
            }
        }
    }
    void copy(const std::array<std::array<uint8_t, Piece::width>, Piece::height> &prefab) {
        this->prefab = prefab;
        this->rotateTo(this->orientation);
    }
    uint8_t getCell(uint8_t row, uint8_t col) const {
        return this->grid[row][col];
    }
    uint8_t getPrefabCell(uint8_t row, uint8_t col) const {
        return this->prefab[row][col];
    }
    const glm::u8vec4 &getBounds() const {
        return this->bounds;
    }
};