#ifndef APP_CONFIG_HPP
#define APP_CONFIG_HPP

#include <glm/glm.hpp>
#include <string>

namespace AppConfig {
constexpr unsigned int kInitialWidth = 1280;
constexpr unsigned int kInitialHeight = 720;

constexpr const char *kResourceRoot = "resources/";
constexpr const char *kWindowTitle = "OpenGL Anti-Aliasing Demo";

constexpr float kFovDegrees = 45.0f;
constexpr float kNearPlane = 0.1f;
constexpr float kFarPlane = 100.0f;
constexpr float kInitialOrbitAngle = 35.0f;
constexpr float kInitialOrbitPitch = 18.0f;
constexpr float kMinOrbitRadius = 2.5f;
constexpr float kMaxOrbitRadius = 20.0f;
constexpr float kMouseOrbitFullWidthDegrees = 100.0f;
constexpr float kPanFactor = 2.0f;
constexpr float kDollyFactor = 3.0f;
constexpr float kScrollZoomSensitivity = 0.8f;

// 与 Rendering_Paths 类似的模型展陈相机
constexpr float kCameraOrbitRadius = 6.5f;
constexpr float kCameraTargetY = 1.0f;
constexpr float kHeroHelmetScale = 1.35f;
constexpr float kSideHelmetScale = 0.85f;
constexpr float kFloorHalfExtent = 8.0f;

constexpr int kMsaaSamplesPresets[] = {2, 4};
constexpr int kMsaaPresetCount = 2;
constexpr int kDefaultMsaaPresetIndex = 1;

constexpr float kTaaBlendSettled = 0.1f;
constexpr float kTaaBlendDragging = 0.9f;

static const glm::vec3 &materialCoeffs() {
    static const glm::vec3 v(0.16f, 0.82f, 0.22f);
    return v;
}

static std::string resourcePath(const std::string &relative) {
    return std::string(kResourceRoot) + relative;
}

static std::string shaderPath(const std::string &name) {
    return resourcePath("shaders/" + name);
}
} // namespace AppConfig

#endif
