#pragma once
#include <glad/gl.h>

namespace gl {
    struct VertexArrayObject {
    private:
        GLuint object;
    public:
        VertexArrayObject();
        ~VertexArrayObject();
        VertexArrayObject(const VertexArrayObject &) = delete;
        VertexArrayObject &operator=(const VertexArrayObject &) = delete;
        VertexArrayObject(VertexArrayObject &&other) noexcept;
        VertexArrayObject &operator=(VertexArrayObject &&other) noexcept;
        GLuint get() const;
    };
    struct VertexBufferObject {
    private:
        GLuint object;
    public:
        VertexBufferObject();
        ~VertexBufferObject();
        VertexBufferObject(const VertexBufferObject &) = delete;
        VertexBufferObject &operator=(const VertexBufferObject &) = delete;
        VertexBufferObject(VertexBufferObject &&other) noexcept;
        VertexBufferObject &operator=(VertexBufferObject &&other) noexcept;

        GLuint get() const;
    };
    struct ShaderProgram {
    private:
        GLuint program;
    public:
        ShaderProgram();
        ~ShaderProgram();
        ShaderProgram(const ShaderProgram &) = delete;
        ShaderProgram &operator=(const ShaderProgram &) = delete;
        ShaderProgram(ShaderProgram &&other) noexcept;
        ShaderProgram &operator=(ShaderProgram &&other) noexcept;
        GLuint get() const;
    };
    struct Shader {
    private:
        GLuint shader;
    public:
        Shader(GLenum type);
        ~Shader();
        Shader(const Shader &) = delete;
        Shader &operator=(const Shader &) = delete;
        Shader(Shader &&other) noexcept;
        Shader &operator=(Shader &&other) noexcept;
        GLuint get() const;
    };
    struct Texture {
    private:
        GLuint texture;
    public:
        Texture();
        ~Texture();
        Texture(const Texture &) = delete;
        Texture &operator=(const Texture &) = delete;
        Texture(Texture &&other) noexcept;
        Texture &operator=(Texture &&other) noexcept;
        GLuint get() const;
    };
}
