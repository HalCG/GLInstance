#ifndef APP_CONFIG_HPP
#define APP_CONFIG_HPP

#include <glm/glm.hpp>
#include <string>

/** 应用级常量：窗口、资源路径、相机与光照默认值 */
namespace AppConfig {
constexpr unsigned int kInitialWidth = 800;
constexpr unsigned int kInitialHeight = 600;

constexpr const char *kResourceRoot = "resources/";
constexpr const char *kWindowTitle = "OpenGL OIT - Stochastic Transparency";

constexpr float kFovDegrees = 90.0f;
constexpr float kNearPlane = 0.1f;
constexpr float kFarPlane = 100.0f;
constexpr float kCameraOrbitRadius = 2.0f;
constexpr float kInitialOrbitAngle = 45.0f;

// 各向同性滤波层级
constexpr unsigned int kMaxNodes = kInitialWidth * kInitialHeight * 20;

static const glm::vec3 &cameraPosition() {
  static const glm::vec3 v(0.0f, 0.0f, 2.0f);
  return v;
}

static const glm::vec3 &backgroundColor() {
  static const glm::vec3 v(0.2f, 0.3f, 0.3f);
  return v;
}

static std::string resourcePath(const std::string &relative) {
  return std::string(kResourceRoot) + relative;
}
} // namespace AppConfig

#endif
