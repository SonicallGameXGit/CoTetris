#pragma once
#include "../core/gl.hpp"

struct MeshRegistry {
private:
    gl::VertexArrayObject quadVertexArrayObject;
    gl::VertexBufferObject quadVertexBufferObject;
public:
    MeshRegistry() : quadVertexArrayObject(), quadVertexBufferObject() {
        glBindVertexArray(quadVertexArrayObject.get());
        glBindBuffer(GL_ARRAY_BUFFER, quadVertexBufferObject.get());
        const float vertices[] = { 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f };
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    }
    ~MeshRegistry() = default;

    static MeshRegistry &getInstance() {
        static MeshRegistry instance;
        return instance;
    }

    GLuint getQuad() const {
        return this->quadVertexArrayObject.get();
    }
};