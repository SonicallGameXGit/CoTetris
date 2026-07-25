#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include "../toolbox/basic_shader.hpp"
#include "../toolbox/mesh.hpp"
#include "../toolbox/texture.hpp"
#include "piece.hpp"

struct Map {
public:
    static constexpr size_t width = 10;
    static constexpr size_t height = 20;
private:
    std::array<std::array<uint8_t, Map::width>, Map::height> grid;
    std::vector<std::array<std::array<uint8_t, Piece::width>, Piece::height>> prefabStack;
    std::optional<Piece> piece;
    bool pieceLanded = false;
    bool justSpawnedPiece = false;

    bool checkFuturePieceCollisions(const int16_t x, const int16_t y) {
        if (!this->piece.has_value()) { return false; }
        const glm::u8vec4 &bounds = this->piece->getBounds();
        
        if (
            x + bounds.x < 0 ||
            x + bounds.z >= Map::width ||
            y + bounds.y < 0 ||
            y + bounds.w >= Map::height
        ) { return true; }

        for (size_t row = bounds.y; row <= bounds.w; row++) {
            for (size_t col = bounds.x; col <= bounds.z; col++) {
                if (this->piece->getCell(row, col) == 0) { continue; }
                const size_t mapRow = y + row;
                const size_t mapCol = x + col;
                if (this->grid[mapRow][mapCol] != 0) { return true; }
            }
        }
        return false;
    }
    void resolvePieceCollisions() {
        if (!this->piece.has_value()) { return; }
        if (!this->checkFuturePieceCollisions(this->piece->x, this->piece->y)) { return; }

        const std::array<glm::i16vec2, 8> fixes = {{
            // First of all, try to move it left or right (the most common case).
            // ! It must stay first, because the user can't control the piece's vertical movement, but he can easily try to move it horizontally, while being able to move out vertically because of wrong algorithm.
            {  1,  0 },
            { -1,  0 },
            // Secondly, try to move it back up or bring down (the second most common case).
            {  0,  1 },
            {  0, -1 },
            // Thirdly, try to move it diagonally (the least common cases).
            // [Up]
            {  1,  1 },
            { -1,  1 },
            // [Down]
            {  1, -1 },
            { -1, -1 },
        }};

        for (uint8_t i = 1; i <= 2; i++) {
            for (const glm::i16vec2 &fix : fixes) {
                if (!this->checkFuturePieceCollisions(this->piece->x + fix.x * i, this->piece->y + fix.y * i)) {
                    this->piece->x += fix.x * i;
                    this->piece->y += fix.y * i;
                    return;
                }
            }
        }
    }
public:
    Map() : grid(), piece(std::nullopt), pieceLanded(false), justSpawnedPiece(false) {
        this->showPiece();
    }
    ~Map() = default;

    void draw(const glm::mat4 &projectionViewMatrix, float aspectRatio) const {
        BasicShader::getInstance().bind();
        glBindVertexArray(MeshRegistry::getInstance().getQuad());
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, TextureRegistry::getInstance().getAtlasTexture());

        const float cellSize = 2.0f / static_cast<float>(Map::height);
        const float gridX = (aspectRatio * 2.0f - static_cast<float>(Map::width) * cellSize) * 0.5f;
        const float uvSize = 1.0f / 3.0f;
        for (size_t row = 0; row < Map::height; row++) {
            for (size_t col = 0; col < Map::width; col++) {
                const float x = -aspectRatio + gridX + col * cellSize;
                const float y = -1.0f + row * cellSize;
                const glm::mat4 modelMatrix = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(x, y, 0.0f)), glm::vec3(cellSize, cellSize, 1.0f));
                BasicShader::getInstance().setProjectionViewModelMatrix(projectionViewMatrix * modelMatrix);

                const float uvX = std::fmodf(static_cast<float>(this->grid[row][col]), 3.0f) * uvSize;
                const float uvY = std::floorf(static_cast<float>(this->grid[row][col]) / 3.0f) * uvSize;
                BasicShader::getInstance().setUV(glm::vec4(uvX, uvY, uvSize, uvSize));

                glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
            }
        }
        if (!this->piece.has_value()) { return; }
        for (size_t row = 0; row < Piece::height; row++) {
            for (size_t col = 0; col < Piece::width; col++) {
                if (this->piece->getCell(row, col) == 0) { continue; }

                const float x = -aspectRatio + gridX + (col + this->piece->x) * cellSize;
                const float y = -1.0f + (row + this->piece->y) * cellSize;
                const glm::mat4 modelMatrix = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(x, y, 0.0f)), glm::vec3(cellSize, cellSize, 1.0f));
                BasicShader::getInstance().setProjectionViewModelMatrix(projectionViewMatrix * modelMatrix);

                const float uvX = std::fmodf(static_cast<float>(this->piece->getCell(row, col)), 3.0f) * uvSize;
                const float uvY = std::floorf(static_cast<float>(this->piece->getCell(row, col)) / 3.0f) * uvSize;
                BasicShader::getInstance().setUV(glm::vec4(uvX, uvY, uvSize, uvSize));

                glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
            }
        }
    }

    bool pushPrefab(const std::array<std::array<uint8_t, Piece::width>, Piece::height> &prefab) {
        // ! Let's for now keep one prefab at a time, maybe I'll do something fun with it later.
        if (this->prefabStack.size() >= 1) { return false; }
        this->prefabStack.push_back(prefab);
        return true;
    }
    void popPrefab() {
        if (this->prefabStack.empty()) { return; }
        this->prefabStack.pop_back();
    }
    void showPiece() {
        if (this->piece.has_value()) { return; }
        this->piece = Piece(Map::width / 2 - Piece::width / 2, Map::height - Piece::height, rand() % 4);
        this->piece->copy(this->prefabStack.empty() ? Piece::defaultShapes[rand() % Piece::defaultShapes.size()] : this->prefabStack.back());
        // this->resolvePieceCollisions(); // ! It makes no sense here, it automatically resolves the collisions and mostly avoids player to fail, by "clipping" out of the other pieces.
        this->justSpawnedPiece = true;
        this->popPrefab();
    }
    void hidePiece() {
        this->piece = std::nullopt;
    }
    void movePieceLeft() {
        if (!this->piece.has_value()) { return; }
        this->piece->x--;
        this->resolvePieceCollisions();
    }
    void movePieceRight() {
        if (!this->piece.has_value()) { return; }
        this->piece->x++;
        this->resolvePieceCollisions();
    }
    void rotatePiece() {
        if (!this->piece.has_value()) { return; }
        this->piece->rotateTo(this->piece->orientation + 1);
        this->resolvePieceCollisions();
    }
    void tick() {
        if (this->piece.has_value()) {
            int16_t oldPieceY = this->piece->y;
            this->piece->y--;
            if (this->checkFuturePieceCollisions(this->piece->x, this->piece->y)) {
                if (this->justSpawnedPiece) {
                    // ! Dead, for now, just reset the map.
                    this->grid.fill({});
                    this->hidePiece();
                    this->showPiece();
                    return;
                }
                this->piece->y++;
                this->pieceLanded = true;
            }
        }
        this->justSpawnedPiece = false;

        if (this->pieceLanded) {
            this->pieceLanded = false;
            for (size_t row = 0; row < Piece::height; row++) {
                for (size_t col = 0; col < Piece::width; col++) {
                    if (this->piece->getCell(row, col) == 0) { continue; }
                    const size_t mapRow = this->piece->y + row;
                    const size_t mapCol = this->piece->x + col;
                    this->grid[mapRow][mapCol] = this->piece->getCell(row, col);
                }
            }
            this->hidePiece();
            this->showPiece();
        }

        bool done = false;
        while (!done) {
            done = true;
            for (size_t row = 0; row < Map::height; row++) {
                uint8_t columnsFilled = 0;
                for (size_t col = 0; col < Map::width; col++) {
                    if (this->grid[row][col] != 0) {
                        columnsFilled++;
                    }
                }
                if (columnsFilled < Map::width) { continue; }
    
                for (size_t r = row; r < Map::height - 1; r++) {
                    this->grid[r] = this->grid[r + 1];
                }
                this->grid[Map::height - 1].fill(0);
                done = false; // If we've found one filled row, there's a chance of finding another one, but if we didn't found any, there's no chance of finding one, of course.
            }
        }
    }
};