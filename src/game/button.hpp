#pragma once
#include <functional>
#include <glm/vec2.hpp>
#include "../core/gl.hpp"
#include "../toolbox/basic_shader.hpp"
#include "../toolbox/mesh.hpp"

struct Button {
private:
    bool pressed = false;
public:
    glm::vec2 position, size;
    glm::vec4 uv;
    GLuint texture;
    std::function<void(Button &self)> onClick;

    Button(GLuint texture, std::function<void(Button &self)> onClick) :
        position(), size(1.0f), uv(0.0f, 0.0f, 1.0f, 1.0f),
        texture(texture),
        onClick(onClick)
    {}
    ~Button() = default;
    
    void update(float mouseX, float mouseY, const bool mousePressed) {
        mouseX -= this->position.x;
        mouseY -= this->position.y;

        if (mousePressed) {
            if (!this->pressed) {
                if (mouseX >= 0.0f && mouseX <= this->size.x && mouseY >= 0.0f && mouseY <= this->size.y) {
                    this->onClick(*this);
                }
            }
            this->pressed = true;
        } else { this->pressed = false; }
    }
    void draw(const glm::mat4 &projectionViewMatrix) const {
        BasicShader::getInstance().bind();
        glBindVertexArray(MeshRegistry::getInstance().getQuad());
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, this->texture);

        const glm::mat4 modelMatrix = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(this->position, 0.0f)), glm::vec3(this->size, 1.0f));
        BasicShader::getInstance().setProjectionViewModelMatrix(projectionViewMatrix * modelMatrix);
        BasicShader::getInstance().setUV(this->uv);

        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }
};
struct Toggle {
private:
    bool pressed = false;
public:
    bool state = false;
    glm::vec2 position, size;
    glm::vec4 uv;
    HSV hsv;
    GLuint texture;
    std::function<void(Toggle &self)> onToggle;

    Toggle(GLuint texture, std::function<void(Toggle &self)> onToggle) :
        position(), size(1.0f), uv(0.0f, 0.0f, 1.0f, 1.0f),
        hsv(0.0f, 0.0f, 1.0f), texture(texture),
        onToggle(onToggle)
    {}
    ~Toggle() = default;
    
    void update(float mouseX, float mouseY, const bool mousePressed) {
        mouseX -= this->position.x;
        mouseY -= this->position.y;

        if (mousePressed) {
            if (!this->pressed) {
                if (mouseX >= 0.0f && mouseX <= this->size.x && mouseY >= 0.0f && mouseY <= this->size.y) {
                    this->state = !this->state;
                    this->onToggle(*this);
                }
            }
            this->pressed = true;
        } else { this->pressed = false; }
    }
    void draw(const glm::mat4 &projectionViewMatrix) const {
        BasicShader::getInstance().bind();
        glBindVertexArray(MeshRegistry::getInstance().getQuad());
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, this->texture);

        const glm::mat4 modelMatrix = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(this->position, 0.0f)), glm::vec3(this->size, 1.0f));
        BasicShader::getInstance().setProjectionViewModelMatrix(projectionViewMatrix * modelMatrix);
        BasicShader::getInstance().setUV(this->uv);
        BasicShader::getInstance().setHSV(this->hsv);

        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }
};