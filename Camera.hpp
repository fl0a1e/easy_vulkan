#pragma once

#include "vkStart.h"
#include <GLFW/glfw3.h>

// camera 只负责观察和投影，不管理模型本身的变换。
// 这一版改成轨道相机：左键拖拽旋转，滚轮缩放。
class camera {
public:
    glm::vec3 target = glm::vec3(0.0f, 0.0f, 0.0f);
    float distance = 3.2f; // 相机到目标点的球面距离
    float yaw = glm::radians(0.0f); // 绕 Y 轴的水平旋转角
    float pitch = glm::radians(28.0f); // 绕目标点抬头/低头的俯仰角
    float fovY = glm::radians(45.0f); // 垂直视野角（Field of View）
    float nearPlane = 0.1f;
    float farPlane = 10.0f;
    float rotateSensitivity = 0.008f;
    float zoomSensitivity = 0.35f;
    float minDistance = 1.0f;
    float maxDistance = 8.0f;

    void AttachToWindow(GLFWwindow* window) {
        glfwSetWindowUserPointer(window, this);// 把 camera 对象bind到 GLFW 窗口对象上,让 GLFW能够调camera的函数
        glfwSetScrollCallback(window, ScrollCallback); // 注册滚轮回调函数（必须是普通函数或静态成员函数）
    }

    void UpdateFromInput(GLFWwindow* window) {
        const int leftButtonDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);
        double cursorX = 0.0;
        double cursorY = 0.0;
        glfwGetCursorPos(window, &cursorX, &cursorY);

        if (leftButtonDown == GLFW_PRESS) {
            // 只有按住左键时，鼠标位移才会转成轨道相机的 yaw/pitch 变化。
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
                if (pitch < glm::radians(-80.0f)) // 限制pitch，防止万向节死锁
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

        if (pendingScroll != 0.0f) {
            distance *= (1.0f - pendingScroll * zoomSensitivity * 0.1f);
            if (distance < minDistance)
                distance = minDistance;
            else if (distance > maxDistance)
                distance = maxDistance;
            pendingScroll = 0.0f;
        }
    }

    glm::mat4 View() const {
        const float cosPitch = std::cos(pitch);
        const glm::vec3 eye = target + glm::vec3(
            std::cos(yaw) * cosPitch,
            std::sin(pitch),
            std::sin(yaw) * cosPitch) * distance;
        return glm::lookAt(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
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

    // 必须是 static 才能作为 GLFW 回调
    static void ScrollCallback(GLFWwindow* window, double, double yoffset) {
        if (auto* pCamera = static_cast<camera*>(glfwGetWindowUserPointer(window)))
            pCamera->pendingScroll += static_cast<float>(yoffset);
    }
};