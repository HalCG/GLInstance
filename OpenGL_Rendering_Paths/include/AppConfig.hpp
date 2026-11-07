#ifndef APP_CONFIG_HPP
#define APP_CONFIG_HPP

#include <glm/glm.hpp>
#include <string>

namespace AppConfig {
constexpr unsigned int kInitialWidth = 1280;
constexpr unsigned int kInitialHeight = 720;

constexpr const char *kResourceRoot = "resources/";
constexpr const char *kWindowTitle = "OpenGL Rendering Paths Demo";

constexpr float kFovDegrees = 45.0f;
constexpr float kNearPlane = 0.1f;
constexpr float kFarPlane = 100.0f;
constexpr float kInitialOrbitAngle = 45.0f;
constexpr float kInitialOrbitPitch = 22.0f;
constexpr float kMinOrbitRadius = 4.0f;
constexpr float kMaxOrbitRadius = 28.0f;
constexpr float kMouseOrbitFullWidthDegrees = 100.0f;
constexpr float kPanFactor = 2.0f;
constexpr float kDollyFactor = 3.0f;
constexpr float kScrollZoomSensitivity = 0.9f;

constexpr float kHeroHelmetScale = 1.15f;
constexpr float kSideHelmetScale = 0.78f;
constexpr float kFloorHalfExtent = 10.0f;
constexpr float kCameraOrbitRadius = 11.0f;
constexpr float kCameraTargetY = 1.0f;

constexpr int kMaxLights = 512;
constexpr int kTileSize = 16;
// 过小会在 Stress（大半径）下很快顶满，导致 64→256→512 时 Forward+ Shading 几乎不变
constexpr int kMaxLightsPerTile = 256;

constexpr int kLightCountPresets[] = {32, 64, 128, 256, 512};
constexpr int kLightPresetCount = 5;

// Ka / Kd / Ks：降低环境与高光，避免多灯叠加过曝
static const glm::vec3 &materialCoeffs() {
    static const glm::vec3 v(0.06f, 0.78f, 0.18f);
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
