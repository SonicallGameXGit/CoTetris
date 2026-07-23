#include <vector>
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

            uniform sampler2D u_Texture;

            void main() {
                f_Color = texture(u_Texture, v_TexCoord);
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
                default: {
                    break;
                }
            }
        }

        Uint64 currentTime = SDL_GetTicksNS();
        const float deltaTime = static_cast<float>(currentTime - lastTime) / 1e9f;
        lastTime = currentTime;

        const float aspectRatio = windowWidth / windowHeight;
        const glm::mat4 projectionViewMatrix = glm::ortho(-aspectRatio, aspectRatio, -1.0f, 1.0f);

        glClear(GL_COLOR_BUFFER_BIT);
        basicShader.bind();
        basicShader.setProjectionViewModelMatrix(projectionViewMatrix);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, whiteTexture.get());
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

        SDL_GL_SwapWindow(window.get());
    }
}