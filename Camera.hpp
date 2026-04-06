#pragma once

#include "vkStart.h"
#include <GLFW/glfw3.h>

// camera 只负责观察和投影，不管理模型本身的变换。
// 自由相机：左键拖拽观察，WASD 按当前朝向移动, 滚轮调整fov
class camera {
public:
    glm::vec3 position = glm::vec3(0.0f, 1.2f, 3.2f);
    float yaw = glm::radians(-90.0f); // -90 度让初始 front 指向 -Z
    float pitch = glm::radians(-18.0f);
    float fovY = glm::radians(45.0f); // 垂直视野角（Field of View）
    float nearPlane = 0.1f;
    float farPlane = 10.0f;
    float moveSpeed = 2.5f;
    float rotateSensitivity = 0.004f;
    float zoomSensitivity = glm::radians(2.5f);

    void AttachToWindow(GLFWwindow* window) {
        glfwSetWindowUserPointer(window, this);
        glfwSetScrollCallback(window, ScrollCallback);
    }

    // 每帧更新，适合持续的操作
    void UpdateFromInput(GLFWwindow* window, float deltaTime) {
        UpdateMouseInput(window);
        UpdateKeyboardInput(window, deltaTime);
        UpdateZoomInput();
    }

    glm::mat4 View() const {
        const glm::vec3 front = Front();
        return glm::lookAt(position, position + front, WorldUp());
    }

    glm::mat4 Projection(VkExtent2D extent) const {
        glm::mat4 proj = glm::perspective(
            fovY,
            float(extent.width) / float(extent.height),
            nearPlane,
            farPlane);

        // GLM 默认按 OpenGL 习惯生成投影矩阵，Y 方向需要翻一下才符合 Vulkan 屏幕空间。
        proj[1][1] *= -1.0f;
        return proj;
    }

private:
    bool dragging = false;
    double lastCursorX = 0.0;
    double lastCursorY = 0.0;
    float pendingScroll = 0.0f;

    static glm::vec3 WorldUp() {
        return glm::vec3(0.0f, 1.0f, 0.0f);
    }

    glm::vec3 Front() const {
        return glm::normalize(glm::vec3(
            std::cos(yaw) * std::cos(pitch),
            std::sin(pitch),
            std::sin(yaw) * std::cos(pitch)));
    }

    void UpdateMouseInput(GLFWwindow* window) {
        const int leftButtonDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);
        double cursorX = 0.0;
        double cursorY = 0.0;
        glfwGetCursorPos(window, &cursorX, &cursorY);

        if (leftButtonDown == GLFW_PRESS) {
            // 第一次按下左键时只记录起点，避免视角瞬间跳变。
            if (!dragging) {
                dragging = true;
                lastCursorX = cursorX;
                lastCursorY = cursorY;
            }
            else {
                const float deltaX = static_cast<float>(cursorX - lastCursorX);
                const float deltaY = static_cast<float>(cursorY - lastCursorY);
                yaw += deltaX * rotateSensitivity;
                pitch += deltaY * rotateSensitivity;
                if (pitch < glm::radians(-80.0f))
                    pitch = glm::radians(-80.0f);
                else if (pitch > glm::radians(80.0f))
                    pitch = glm::radians(80.0f);
                lastCursorX = cursorX;
                lastCursorY = cursorY;
            }
        }
        else {
            dragging = false;
        }
    }

    void UpdateKeyboardInput(GLFWwindow* window, float deltaTime) {
        const float moveStep = moveSpeed * deltaTime;
        const glm::vec3 front = Front();
        const glm::vec3 right = glm::normalize(glm::cross(front, WorldUp()));

        // 完全自由相机：
        // W/S 沿当前完整前向前进后退，A/D 沿当前完整右向左右平移。
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            position += front * moveStep;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            position -= front * moveStep;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            position -= right * moveStep;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            position += right * moveStep;
    }

    void UpdateZoomInput() {
        if (pendingScroll == 0.0f)
            return;

        // 滚轮在自由相机里改成调节 FOV：值越小，镜头越“长焦”。
        fovY -= pendingScroll * zoomSensitivity;
        if (fovY < glm::radians(20.0f))
            fovY = glm::radians(20.0f);
        else if (fovY > glm::radians(90.0f))
            fovY = glm::radians(90.0f);
        pendingScroll = 0.0f;
    }

    static void ScrollCallback(GLFWwindow* window, double, double yoffset) {
        if (auto* pCamera = static_cast<camera*>(glfwGetWindowUserPointer(window)))
            pCamera->pendingScroll += static_cast<float>(yoffset);
    }
};