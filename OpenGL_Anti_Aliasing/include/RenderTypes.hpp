#ifndef RENDER_TYPES_HPP
#define RENDER_TYPES_HPP

#include <glm/glm.hpp>

// 抗锯齿模式：None=直出 / MSAA=硬件多重采样 / FXAA=后处理 / TAA=时间累积
enum class AAMode {
    None = 0,
    MSAA = 1,
    FXAA = 2,
    TAA = 3
};

// 单帧相机参数（view/projection 及 TAA 用逆矩阵、抖动）
struct FrameCamera {
    glm::mat4 view;
    glm::mat4 projection;              // 可能含 jitter（仅 Scene Pass 光栅化）
    glm::mat4 viewProjection;          // projection * view（可能 jittered）
    glm::mat4 unjitteredViewProjection = glm::mat4(1.0f); // TAA 重投影 / history 用
    glm::mat4 invUnjitteredViewProjection = glm::mat4(1.0f);
    glm::mat4 invViewProjection = glm::mat4(1.0f); // 兼容旧字段；TAA 应使用 invUnjittered*
    glm::vec3 eye;
    glm::vec2 jitterNdc = glm::vec2(0.0f);
};

// 每帧性能数据（fps/minFps/avgFrameMs 为 1s 窗统计）
struct FrameStats {
    float scenePassMs = 0.0f;
    float postPassMs = 0.0f;
    float totalFrameMs = 0.0f;
    float fps = 0.0f;
    float avgFrameMs = 0.0f;
    float minFps = 0.0f;
};

#endif
