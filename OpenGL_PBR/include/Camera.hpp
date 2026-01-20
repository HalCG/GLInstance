#ifndef CAMERA_HPP
#define CAMERA_HPP

#define GLM_ENABLE_EXPERIMENTAL
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>

/**
 * @brief 相机旋转模式枚举
 */
enum class CameraMode {
    Trackball = 0, // 360 度自由轨迹球模式 (无锁、无限旋转、无角度跳变，默认模式)
    Turntable = 1  // 保持 Y 轴直立的经典转盘模式
};

/**
 * @brief Camera 3D 轨道交互式视角相机 (Orbit & Arcball Camera)
 * 
 * 支持 Trackball (四元数全方向无限旋转) 与 Turntable (Y-Up 经典转盘) 两种模式，
 * 彻底消除 Gimbal Lock 视线倒转与角度突变跳变问题。
 */
class Camera {
public:
    glm::vec3 Target = glm::vec3(0.0f, 0.0f, 0.0f); // 观察中心焦点
    float Distance = 5.0f;                          // 相机到 Focal Point 的距离 (球半径)
    float Yaw = 0.0f;                               // 偏航角 (Turntable 模式)
    float Pitch = 0.0f;                             // 俯仰角 (Turntable 模式)
    float Fov = 45.0f;                              // 视场角 (Field of View)
    bool AutoRotate = false;                        // 是否开启自动旋转模式
    float AutoRotateSpeed = 30.0f;                  // 自动旋转速度 (度/秒)

    CameraMode Mode = CameraMode::Trackball;        // 默认使用 360 度自由轨迹球

    // 四元数表示摄像机在 Trackball 模式下的姿态
    glm::quat Orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

    Camera(glm::vec3 target = glm::vec3(0.0f, 0.0f, 0.0f), float distance = 5.0f, float yaw = 0.0f, float pitch = 0.0f)
        : Target(target), Distance(distance), Yaw(yaw), Pitch(pitch), Fov(45.0f), Mode(CameraMode::Trackball) {
        syncQuaternionFromEuler();
    }

    /**
     * @brief 从欧拉角同步四元数姿态
     */
    void syncQuaternionFromEuler() {
        glm::quat qYaw = glm::angleAxis(glm::radians(Yaw), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::quat qPitch = glm::angleAxis(glm::radians(Pitch), glm::vec3(1.0f, 0.0f, 0.0f));
        Orientation = glm::normalize(qYaw * qPitch);
    }

    /**
     * @brief 逐帧逻辑更新
     */
    void update(float deltaTime) {
        if (AutoRotate) {
            float deltaAngle = AutoRotateSpeed * deltaTime;
            if (Mode == CameraMode::Trackball) {
                glm::quat qRot = glm::angleAxis(glm::radians(deltaAngle), glm::vec3(0.0f, 1.0f, 0.0f));
                Orientation = glm::normalize(qRot * Orientation);
            } else {
                Yaw += deltaAngle;
                if (Yaw > 360.0f) Yaw -= 360.0f;
            }
        }
    }

    /**
     * @brief 计算相机的 3D 世界空间位置 (Position)
     */
    glm::vec3 getPosition() const {
        if (Mode == CameraMode::Trackball) {
            glm::vec3 frontDir = Orientation * glm::vec3(0.0f, 0.0f, 1.0f);
            return Target + frontDir * Distance;
        } else {
            float radYaw = glm::radians(Yaw);
            float radPitch = glm::radians(Pitch);
            float x = Target.x + Distance * cos(radPitch) * sin(radYaw);
            float y = Target.y + Distance * sin(radPitch);
            float z = Target.z + Distance * cos(radPitch) * cos(radYaw);
            return glm::vec3(x, y, z);
        }
    }

    /**
     * @brief 获取 View 观察矩阵 (World -> Camera 观察空间)
     */
    glm::mat4 getViewMatrix() const {
        glm::vec3 pos = getPosition();
        if (Mode == CameraMode::Trackball) {
            glm::vec3 up = Orientation * glm::vec3(0.0f, 1.0f, 0.0f);
            return glm::lookAt(pos, Target, up);
        } else {
            // Turntable 模式使用解析法推导解析 Up 向量，确保平滑无跳变
            float radYaw = glm::radians(Yaw);
            float radPitch = glm::radians(Pitch);
            glm::vec3 up = glm::vec3(-sin(radPitch) * sin(radYaw), cos(radPitch), -sin(radPitch) * cos(radYaw));
            return glm::lookAt(pos, Target, up);
        }
    }

    float getFov() const { return Fov; }
    void setFov(float fov) { Fov = glm::clamp(fov, 1.0f, 90.0f); }

    /**
     * @brief 鼠标左键拖拽响应 (旋转)
     */
    void processMouseMovement(float xoffset, float yoffset) {
        float sensitivity = 0.25f;

        if (Mode == CameraMode::Trackball) {
            float deltaYaw   = -xoffset * sensitivity;
            float deltaPitch =  yoffset * sensitivity;

            // 获取摄像机当前的 Right 与 Up 轴，支持任意方向无限旋转
            glm::vec3 cameraRight = Orientation * glm::vec3(1.0f, 0.0f, 0.0f);
            glm::vec3 cameraUp    = Orientation * glm::vec3(0.0f, 1.0f, 0.0f);

            glm::quat qYaw   = glm::angleAxis(glm::radians(deltaYaw), cameraUp);
            glm::quat qPitch = glm::angleAxis(glm::radians(deltaPitch), cameraRight);

            Orientation = glm::normalize(qYaw * qPitch * Orientation);
        } else {
            Yaw   -= xoffset * sensitivity;
            Pitch += yoffset * sensitivity;
            Pitch = glm::clamp(Pitch, -89.5f, 89.5f);
        }
    }

    /**
     * @brief 鼠标滚轮响应 (缩放距离 Distance)
     */
    void processMouseScroll(float yoffset) {
        Distance -= yoffset * 0.4f;
        Distance = glm::clamp(Distance, 0.5f, 50.0f);
    }

    /**
     * @brief 鼠标右键/Shift+左键拖拽响应 (平移 Focal Point Target)
     */
    void processPan(float xoffset, float yoffset) {
        float panSpeed = 0.003f * Distance;

        if (Mode == CameraMode::Trackball) {
            glm::vec3 right = Orientation * glm::vec3(1.0f, 0.0f, 0.0f);
            glm::vec3 up    = Orientation * glm::vec3(0.0f, 1.0f, 0.0f);
            Target -= right * (xoffset * panSpeed);
            Target += up    * (yoffset * panSpeed);
        } else {
            glm::vec3 pos = getPosition();
            glm::vec3 front = glm::normalize(Target - pos);
            glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));
            glm::vec3 up    = glm::normalize(glm::cross(right, front));
            Target -= right * (xoffset * panSpeed);
            Target += up    * (yoffset * panSpeed);
        }
    }

    /**
     * @brief 重置相机参数到初始状态
     */
    void reset() {
        Target = glm::vec3(0.0f, 0.0f, 0.0f);
        Distance = 5.0f;
        Yaw = 0.0f;
        Pitch = 0.0f;
        Fov = 45.0f;
        AutoRotate = false;
        Mode = CameraMode::Trackball;
        Orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
};

#endif // CAMERA_HPP
