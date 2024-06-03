#ifndef APP_CONFIG_HPP
#define APP_CONFIG_HPP

#include <glm/glm.hpp>
#include <string>

/** 应用级常量：窗口、资源路径、相机与光照默认值 */
namespace AppConfig {
constexpr unsigned int kInitialWidth = 800;
constexpr unsigned int kInitialHeight = 600;

constexpr const char *kResourceRoot = "resources/";
constexpr const char *kWindowTitle = "LearnOpenGL";

constexpr float kFovDegrees = 90.0f;
constexpr float kNearPlane = 0.1f;
constexpr float kFarPlane = 100.0f;
constexpr float kCameraOrbitRadius = 2.0f;
constexpr float kInitialOrbitAngle = 45.0f;

constexpr int kMaxDepthPeelLayers = 10;

static const glm::vec3 &cameraPosition() {
  static const glm::vec3 v(0.0f, 0.0f, 2.0f);
  return v;
}
static const glm::vec3 &lightPosition() {
  static const glm::vec3 v(2.0f, 2.0f, 0.0f);
  return v;
}
/** k.x=环境光, k.y=漫反射, k.z=高光 */
static const glm::vec3 &materialCoeffs() {
  static const glm::vec3 v(0.4f, 0.4f, 0.2f);
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
