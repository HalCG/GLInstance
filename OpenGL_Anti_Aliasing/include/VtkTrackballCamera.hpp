#ifndef VTK_TRACKBALL_CAMERA_HPP
#define VTK_TRACKBALL_CAMERA_HPP

#include "AppConfig.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

enum class CameraDragMode { None, Rotate, Pan, Dolly };

// VTK vtkInteractorStyleTrackballCamera 风格轨道球相机
// 左键旋转，中键平移，右键推拉，滚轮缩放
struct VtkTrackballCamera {
    glm::vec3 target = glm::vec3(0.0f, AppConfig::kCameraTargetY, 0.0f);
    float yaw = AppConfig::kInitialOrbitAngle;
    float pitch = AppConfig::kInitialOrbitPitch;
    float radius = AppConfig::kCameraOrbitRadius;

    CameraDragMode dragMode = CameraDragMode::None;
    bool skipDragDelta = false; // 按下鼠标首帧跳过 delta，避免跳变
    double lastCursorX = 0.0;
    double lastCursorY = 0.0;

    // 由 yaw/pitch/radius 计算相机世界坐标
    glm::vec3 eye() const {
        const float yawRad = glm::radians(yaw);
        const float pitchRad = glm::radians(pitch);
        return target + glm::vec3(radius * glm::cos(pitchRad) * glm::sin(yawRad), radius * glm::sin(pitchRad),
                                  radius * glm::cos(pitchRad) * glm::cos(yawRad));
    }

    // 由 eye / target / up 构造观察矩阵
    glm::mat4 viewMatrix() const { return glm::lookAt(eye(), target, glm::vec3(0.0f, 1.0f, 0.0f)); }

    // 开始拖拽：记录模式与光标位置
    void beginDrag(CameraDragMode mode, double x, double y) {
        dragMode = mode;
        skipDragDelta = true;
        lastCursorX = x;
        lastCursorY = y;
    }

    // 结束拖拽
    void endDrag() {
        dragMode = CameraDragMode::None;
        skipDragDelta = false;
    }

    // 是否正在拖拽（用于主循环 poll 与性能统计开关）
    bool isDragging() const { return dragMode != CameraDragMode::None; }

    // 应用光标位移；返回 true 表示相机参数已变化，应 markCameraDirty
    bool applyCursorDelta(double x, double y, unsigned width, unsigned height) {
        if (dragMode == CameraDragMode::None) {
            return false;
        }
        if (skipDragDelta) {
            lastCursorX = x;
            lastCursorY = y;
            skipDragDelta = false;
            return false;
        }

        const double deltaX = x - lastCursorX;
        const double deltaY = y - lastCursorY;
        lastCursorX = x;
        lastCursorY = y;

        const float w = static_cast<float>(width > 0 ? width : AppConfig::kInitialWidth);
        const float h = static_cast<float>(height > 0 ? height : AppConfig::kInitialHeight);

        switch (dragMode) {
        case CameraDragMode::Rotate: {
            const float sensX = AppConfig::kMouseOrbitFullWidthDegrees / w;
            const float sensY = AppConfig::kMouseOrbitFullWidthDegrees / h;
            yaw -= static_cast<float>(deltaX) * sensX;
            pitch -= static_cast<float>(-deltaY) * sensY;
            pitch = glm::clamp(pitch, -85.0f, 85.0f);
            break;
        }
        case CameraDragMode::Pan: {
            const glm::vec3 e = eye();
            const glm::vec3 forward = glm::normalize(target - e);
            const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
            const glm::vec3 right = glm::normalize(glm::cross(forward, worldUp));
            const glm::vec3 up = glm::cross(right, forward);
            const float panScale = radius * AppConfig::kPanFactor / h;
            target -= right * static_cast<float>(deltaX) * panScale;
            target += up * static_cast<float>(deltaY) * panScale;
            break;
        }
        case CameraDragMode::Dolly: {
            const float dollyScale = AppConfig::kDollyFactor * radius / h;
            radius -= static_cast<float>(deltaY) * dollyScale;
            radius = glm::clamp(radius, AppConfig::kMinOrbitRadius, AppConfig::kMaxOrbitRadius);
            break;
        }
        default:
            break;
        }
        return true;
    }

    // 滚轮缩放（调整 orbit radius）
    void applyScroll(double yoffset) {
        radius -= static_cast<float>(yoffset) * AppConfig::kScrollZoomSensitivity;
        radius = glm::clamp(radius, AppConfig::kMinOrbitRadius, AppConfig::kMaxOrbitRadius);
    }
};

#endif
