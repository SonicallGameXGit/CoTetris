#pragma once
#include <vector>
#include <glm/mat4x4.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "../core/gl.hpp"

struct HSV {
    float h, s, v;
    HSV(float h, float s, float v) : h(h), s(s), v(v) {}
    ~HSV() = default;
};

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

            uniform vec3 u_HSV;
            uniform vec4 u_UV;
            uniform sampler2D u_Texture;

            // Source: https://gist.github.com/983/e170a24ae8eba2cd174f
            vec3 rgb2hsv(in vec3 c) {
                vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
                vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
                vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));

                float d = q.x - min(q.w, q.y);
                float e = 1.0e-10;
                return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
            }
            vec3 hsv2rgb(in vec3 c) {
                vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
                vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
                return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
            }

            void main() {
                f_Color = texture(u_Texture, v_TexCoord * u_UV.zw + u_UV.xy);
                vec3 hsv = rgb2hsv(f_Color.rgb);
                f_Color.rgb = hsv2rgb(vec3(mod(hsv.x + u_HSV.x / 360.0, 1.0), hsv.y + u_HSV.y, hsv.z + u_HSV.z));
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
    void setHSV(const HSV &value) const {
        glUniform3f(glGetUniformLocation(this->program.get(), "u_HSV"), value.h, value.s, value.v);
    }
};