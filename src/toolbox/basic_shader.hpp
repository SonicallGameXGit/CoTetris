#pragma once
#include <vector>
#include <glm/mat4x4.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "../core/gl.hpp"

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