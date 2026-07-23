#include <vector>
#include <SDL3_image/SDL_image.h>
#include <glm/mat4x4.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "core/sdl.hpp"
#include "core/gl.hpp"

struct BasicShader {
private:
    gl::ShaderProgram program;
public:
    BasicShader() : program() {
        const gl::Shader vertexShader = gl::Shader(GL_VERTEX_SHADER);
        const char *vertexShaderSource = R"GLSL(
            #version 410
            layout(location = 0) in vec2 a_Position;
            layout(location = 0) out vec2 v_TexCoord;

            uniform mat4 u_ProjectionViewModelMatrix;

            void main() {
                gl_Position = u_ProjectionViewModelMatrix * vec4(a_Position, 0.0, 1.0);
                v_TexCoord = vec2(a_Position.x, 1.0 - a_Position.y);
            }
        )GLSL";
        glShaderSource(vertexShader.get(), 1, &vertexShaderSource, nullptr);
        glCompileShader(vertexShader.get());
        GLint success = 0;
        glGetShaderiv(vertexShader.get(), GL_COMPILE_STATUS, &success);
        if (!success) {
            std::vector<char> infoLog = std::vector<char>();
            glGetShaderiv(vertexShader.get(), GL_INFO_LOG_LENGTH, &success);
            infoLog.resize(success);
            glGetShaderInfoLog(vertexShader.get(), success, nullptr, infoLog.data());
            fprintf(stderr, "Vertex shader compilation failed: %s\n", infoLog.data());
            return;
        }

        const gl::Shader fragmentShader = gl::Shader(GL_FRAGMENT_SHADER);
        const char *fragmentShaderSource = R"GLSL(
            #version 410
            layout(location = 0) in vec2 v_TexCoord;
            layout(location = 0) out vec4 f_Color;

            uniform vec4 u_UV;
            uniform sampler2D u_Texture;

            void main() {
                f_Color = texture(u_Texture, v_TexCoord * u_UV.zw + u_UV.xy);
            }
        )GLSL";
        glShaderSource(fragmentShader.get(), 1, &fragmentShaderSource, nullptr);
        glCompileShader(fragmentShader.get());
        success = 0;
        glGetShaderiv(fragmentShader.get(), GL_COMPILE_STATUS, &success);
        if (!success) {
            std::vector<char> infoLog = std::vector<char>();
            glGetShaderiv(fragmentShader.get(), GL_INFO_LOG_LENGTH, &success);
            infoLog.resize(success);
            glGetShaderInfoLog(fragmentShader.get(), success, nullptr, infoLog.data());
            fprintf(stderr, "Fragment shader compilation failed: %s\n", infoLog.data());
            return;
        }

        glAttachShader(this->program.get(), vertexShader.get());
        glAttachShader(this->program.get(), fragmentShader.get());
        glLinkProgram(this->program.get());
        success = 0;
        glGetProgramiv(this->program.get(), GL_LINK_STATUS, &success);
        if (!success) {
            std::vector<char> infoLog = std::vector<char>();
            glGetProgramiv(this->program.get(), GL_INFO_LOG_LENGTH, &success);
            infoLog.resize(success);
            glGetProgramInfoLog(this->program.get(), success, nullptr, infoLog.data());
            fprintf(stderr, "Shader program linking failed: %s\n", infoLog.data());
            return;
        }

        this->bind();
        glUniform1i(glGetUniformLocation(this->program.get(), "u_Texture"), 0);
    }
    ~BasicShader() = default;
    static BasicShader &getInstance() {
        static BasicShader instance;
        return instance;
    }
    GLuint get() const { return program.get(); }

    void bind() const {
        glUseProgram(this->program.get());
    }
    void setProjectionViewModelMatrix(const glm::mat4 &value) const {
        glUniformMatrix4fv(glGetUniformLocation(this->program.get(), "u_ProjectionViewModelMatrix"), 1, false, glm::value_ptr(value));
    }
    void setUV(const glm::vec4 &value) const {
        glUniform4fv(glGetUniformLocation(this->program.get(), "u_UV"), 1, glm::value_ptr(value));
    }
};

bool loadTexture(const char *path, GLuint destination) {
    SDL_Surface *surface = IMG_Load(path);
    if (surface == nullptr) {
        fprintf(stderr, "Failed to load texture: %s\n", SDL_GetError());
        return false;
    }

    SDL_Surface *convertedSurface = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
    if (convertedSurface == nullptr) {
        fprintf(stderr, "Failed to convert surface: %s\n", SDL_GetError());
        SDL_DestroySurface(surface);
        return false;
    }

    glBindTexture(GL_TEXTURE_2D, destination);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, convertedSurface->w, convertedSurface->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, convertedSurface->pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    SDL_DestroySurface(convertedSurface);
    SDL_DestroySurface(surface);
    return true;
}

struct Piece {
public:
    static constexpr size_t width = 4;
    static constexpr size_t height = 4;
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
    const glm::u8vec4 &getBounds() const {
        return this->bounds;
    }
};
struct Map {
public:
    static constexpr size_t width = 10;
    static constexpr size_t height = 20;
private:
    std::array<std::array<uint8_t, Map::width>, Map::height> grid;
    std::optional<Piece> piece;

    void resolvePieceCollisions() {
        // TODO: For now it just checks the map bounds, but it should also check for collisions with other pieces.
        if (!this->piece.has_value()) { return; }
        const glm::u8vec4 &bounds = this->piece->getBounds();
        if (this->piece->x + bounds.x < 0) {
            this->piece->x = -bounds.x;
        }
        if (this->piece->x + bounds.z >= Map::width) {
            this->piece->x = Map::width - 1 - bounds.z;
        }
        if (this->piece->y + bounds.y < 0) {
            this->piece->y = -bounds.y;
        }
        if (this->piece->y + bounds.w >= Map::height) {
            this->piece->y = Map::height - 1 - bounds.w;
        }
    }
public:
    Map() : grid(), piece(std::nullopt) {}
    ~Map() = default;

    void draw(const BasicShader &shader, GLuint atlasTexture, const glm::mat4 &projectionViewMatrix, float aspectRatio) const {
        shader.bind();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, atlasTexture);

        const float cellSize = 2.0f / static_cast<float>(Map::height);
        const float gridX = (aspectRatio * 2.0f - static_cast<float>(Map::width) * cellSize) * 0.5f;
        const float uvSize = 1.0f / 3.0f;
        for (size_t row = 0; row < Map::height; row++) {
            for (size_t col = 0; col < Map::width; col++) {
                const float x = -aspectRatio + gridX + col * cellSize;
                const float y = -1.0f + row * cellSize;
                const glm::mat4 modelMatrix = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(x, y, 0.0f)), glm::vec3(cellSize, cellSize, 1.0f));
                shader.setProjectionViewModelMatrix(projectionViewMatrix * modelMatrix);

                const float uvX = std::fmodf(static_cast<float>(this->grid[row][col]), 3.0f) * uvSize;
                const float uvY = std::floorf(static_cast<float>(this->grid[row][col]) / 3.0f) * uvSize;
                shader.setUV(glm::vec4(uvX, uvY, uvSize, uvSize));

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
                shader.setProjectionViewModelMatrix(projectionViewMatrix * modelMatrix);

                const float uvX = std::fmodf(static_cast<float>(this->piece->getCell(row, col)), 3.0f) * uvSize;
                const float uvY = std::floorf(static_cast<float>(this->piece->getCell(row, col)) / 3.0f) * uvSize;
                shader.setUV(glm::vec4(uvX, uvY, uvSize, uvSize));

                glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
            }
        }
    }

    void showPiece(const std::array<std::array<uint8_t, Piece::width>, Piece::height> &prefab) {
        this->piece = Piece(Map::width / 2 - Piece::width / 2, Map::height - Piece::height, /* rand() % 4 */ 0);
        this->piece->copy(prefab);
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
        if (!this->piece.has_value()) { return; }
        this->piece->y--;
        this->resolvePieceCollisions();
    }
};

int main() {
    const sdl::Context context = sdl::Context(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    if (!context.isValid()) {
        printf("Failed to initialize SDL: %s\n", SDL_GetError());
        return -1;
    }
    float windowWidth = 1280, windowHeight = 720;
    const sdl::Window window = sdl::Window("CoTetris", static_cast<int>(windowWidth), static_cast<int>(windowHeight), SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window.isValid()) {
        printf("Failed to create window: %s\n", SDL_GetError());
        return -1;
    }
    {
        const float pixelDensity = SDL_GetWindowPixelDensity(window.get());
        windowWidth *= pixelDensity;
        windowHeight *= pixelDensity;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    const sdl::GLContext glContext = sdl::GLContext(window.get());
    if (!glContext.isValid()) {
        printf("Failed to create OpenGL context: %s\n", SDL_GetError());
        return -1;
    }
    if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress)) {
        printf("Failed to initialize GLAD\n");
        return -1;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    BasicShader basicShader = BasicShader();

    gl::Texture whiteTexture = gl::Texture();
    glBindTexture(GL_TEXTURE_2D, whiteTexture.get());
    {
        const uint8_t whitePixel[] = { 255, 255, 255, 255 };
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, whitePixel);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    gl::Texture atlasTexture = gl::Texture();
    if (!loadTexture("assets/atlas.png", atlasTexture.get())) {
        printf("Failed to load texture atlas\n");
        return -1;
    }

    gl::VertexArrayObject vertexArrayObject = gl::VertexArrayObject();
    glBindVertexArray(vertexArrayObject.get());
    gl::VertexBufferObject vertexBufferObject = gl::VertexBufferObject();
    glBindBuffer(GL_ARRAY_BUFFER, vertexBufferObject.get());
    {
        const float vertices[] = { 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f };
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    }
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

    const std::array<std::array<std::array<uint8_t, Piece::width>, Piece::height>, 7> defaultShapes = {{
        {{ // I
            { 0, 0, 1, 0 },
            { 0, 0, 1, 0 },
            { 0, 0, 1, 0 },
            { 0, 0, 1, 0 },
        }},
        {{ // J
            { 0, 0, 1, 0 },
            { 0, 0, 1, 0 },
            { 0, 1, 1, 0 },
            { 0, 0, 0, 0 },
        }},
        {{ // L
            { 0, 1, 0, 0 },
            { 0, 1, 0, 0 },
            { 0, 1, 1, 0 },
            { 0, 0, 0, 0 },
        }},
        {{ // O
            { 0, 0, 0, 0 },
            { 0, 1, 1, 0 },
            { 0, 1, 1, 0 },
            { 0, 0, 0, 0 },
        }},
        {{ // Z
            { 0, 0, 0, 0 },
            { 0, 1, 1, 0 },
            { 1, 1, 0, 0 },
            { 0, 0, 0, 0 },
        }},
        {{ // T
            { 0, 1, 0, 0 },
            { 1, 1, 1, 0 },
            { 0, 0, 0, 0 },
            { 0, 0, 0, 0 },
        }},
        {{ // S
            { 0, 0, 0, 0 },
            { 1, 1, 0, 0 },
            { 0, 1, 1, 0 },
            { 0, 0, 0, 0 },
        }}
    }};

    Map map = Map();
    srand(SDL_GetTicksNS());
    map.showPiece(defaultShapes[rand() % defaultShapes.size()]);

    const float tickRate = 1.0f / 2.0f; // 2 ticks per second
    float tickTimer = 0.0f;

    Uint64 lastTime = SDL_GetTicksNS();
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT: {
                    running = false;
                    break;
                }
                case SDL_EVENT_WINDOW_RESIZED: {
                    const float pixelDensity = SDL_GetWindowPixelDensity(window.get());
                    windowWidth = static_cast<float>(event.window.data1) * pixelDensity;
                    windowHeight = static_cast<float>(event.window.data2) * pixelDensity;
                    glViewport(0, 0, static_cast<int>(windowWidth), static_cast<int>(windowHeight));
                    break;
                }
                case SDL_EVENT_KEY_DOWN: {
                    switch (event.key.key) {
                        case SDLK_LEFT:
                        case SDLK_A: {
                            map.movePieceLeft();
                            break;
                        }
                        case SDLK_RIGHT:
                        case SDLK_D: {
                            map.movePieceRight();
                            break;
                        }
                        case SDLK_UP:
                        case SDLK_W:
                        case SDLK_SPACE:
                        case SDLK_R: {
                            map.rotatePiece();
                            break;
                        }
                        default: {
                            break;
                        }
                    }
                    break;
                }
                default: {
                    break;
                }
            }
        }

        Uint64 currentTime = SDL_GetTicksNS();
        const float deltaTime = static_cast<float>(currentTime - lastTime) / 1e9f;
        lastTime = currentTime;

        tickTimer += deltaTime;
        if (tickTimer >= tickRate) {
            tickTimer -= tickRate;
            map.tick();
        }

        const float aspectRatio = windowWidth / windowHeight;
        const glm::mat4 projectionViewMatrix = glm::ortho(-aspectRatio, aspectRatio, -1.0f, 1.0f);

        glClear(GL_COLOR_BUFFER_BIT);
        map.draw(basicShader, atlasTexture.get(), projectionViewMatrix, aspectRatio);

        SDL_GL_SwapWindow(window.get());
    }
}