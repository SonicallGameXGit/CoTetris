#include "gl.hpp"

namespace gl {
    VertexArrayObject::VertexArrayObject() : object(0) {
        glGenVertexArrays(1, &this->object);
    }
    VertexArrayObject::~VertexArrayObject() {
        glDeleteVertexArrays(1, &this->object);
    }
    VertexArrayObject::VertexArrayObject(VertexArrayObject &&other) noexcept : object(other.object) {
        other.object = 0;
    }
    VertexArrayObject &VertexArrayObject::operator=(VertexArrayObject &&other) noexcept {
        if (this != &other) {
            glDeleteVertexArrays(1, &this->object);
            this->object = other.object;
            other.object = 0;
        }
        return *this;
    }
    GLuint VertexArrayObject::get() const { return this->object; }

    VertexBufferObject::VertexBufferObject() : object(0)
     {
        glGenBuffers(1, &this->object);
    }
    VertexBufferObject::~VertexBufferObject() {
        glDeleteBuffers(1, &this->object);
    }
    VertexBufferObject::VertexBufferObject(VertexBufferObject &&other) noexcept : object(other.object) {
        other.object = 0;
    }
    VertexBufferObject &VertexBufferObject::operator=(VertexBufferObject &&other) noexcept {
        if (this != &other) {
            glDeleteBuffers(1, &this->object);
            this->object = other.object;
            other.object = 0;
        }
        return *this;
    }
    GLuint VertexBufferObject::get() const { return this->object; }

    ShaderProgram::ShaderProgram() : program(0) {
        this->program = glCreateProgram();
    }
    ShaderProgram::~ShaderProgram() {
        glDeleteProgram(this->program);
    }
    ShaderProgram::ShaderProgram(ShaderProgram &&other) noexcept : program(other.program) {
        other.program = 0;
    }
    ShaderProgram &ShaderProgram::operator=(ShaderProgram &&other) noexcept {
        if (this != &other) {
            glDeleteProgram(this->program);
            this->program = other.program;
            other.program = 0;
        }
        return *this;
    }
    GLuint ShaderProgram::get() const { return this->program; }

    Shader::Shader(GLenum type) : shader(0) {
        this->shader = glCreateShader(type);
    }
    Shader::~Shader() {
        glDeleteShader(this->shader);
    }
    Shader::Shader(Shader &&other) noexcept : shader(other.shader) {
        other.shader = 0;
    }
    Shader &Shader::operator=(Shader &&other) noexcept {
        if (this != &other) {
            glDeleteShader(this->shader);
            this->shader = other.shader;
            other.shader = 0;
        }
        return *this;
    }
    GLuint Shader::get() const { return this->shader; }

    Texture::Texture() : texture(0) {
        glGenTextures(1, &this->texture);
    }
    Texture::~Texture() {
        glDeleteTextures(1, &this->texture);
    }
    Texture::Texture(Texture &&other) noexcept : texture(other.texture) {
        other.texture = 0;
    }
    Texture &Texture::operator=(Texture &&other) noexcept {
        if (this != &other) {
            glDeleteTextures(1, &this->texture);
            this->texture = other.texture;
            other.texture = 0;
        }
        return *this;
    }
    GLuint Texture::get() const { return this->texture; }
}
