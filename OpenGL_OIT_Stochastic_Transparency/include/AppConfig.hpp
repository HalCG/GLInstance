#ifndef APP_CONFIG_HPP
#define APP_CONFIG_HPP

#include <glm/glm.hpp>

/** 应用级常量：窗口、资源路径、相机默认值 */
namespace AppConfig {
constexpr unsigned int kInitialWidth = 800;
constexpr unsigned int kInitialHeight = 600;

constexpr const char *kResourceRoot = "resources/";
constexpr const char *kWindowTitle = "OpenGL OIT - Stochastic Transparency";

constexpr float kFovDegrees = 90.0f;
constexpr float kNearPlane = 0.1f;
constexpr float kFarPlane = 100.0f;
constexpr float kInitialOrbitAngle = 45.0f;
} // namespace AppConfig

#endif
