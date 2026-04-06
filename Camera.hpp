#pragma once

#include "vkStart.h"

// camera 只负责观察和投影，不管理模型本身的变换。
// 第一版先做一个自动轨道相机：相机围绕 target 匀速旋转，便于验证 view/proj 链路。
class camera {
public:
    glm::vec3 target = glm::vec3(0.0f, 0.0f, 0.0f);
    float radius = 2.8f;
    float height = 1.4f;
    float yawSpeed = glm::radians(35.0f);
    float fovY = glm::radians(45.0f);
    float nearPlane = 0.1f;
    float farPlane = 10.0f;

    glm::mat4 View(float timeSeconds) const {
        const float angle = timeSeconds * yawSpeed;
        const glm::vec3 eye = target + glm::vec3(
            std::cos(angle) * radius,
            height,
            std::sin(angle) * radius);
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
};