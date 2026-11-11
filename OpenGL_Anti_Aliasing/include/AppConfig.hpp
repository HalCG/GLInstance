#ifndef APP_CONFIG_HPP
#define APP_CONFIG_HPP

#include <glm/glm.hpp>
#include <string>

// Anti-Aliasing Demo 运行时常量：窗口 / 相机 / 场景 / MSAA / TAA
namespace AppConfig {

// ---------- 窗口与资源 ----------
constexpr unsigned int kInitialWidth = 1280;   // 启动窗口宽（像素）
constexpr unsigned int kInitialHeight = 720;   // 启动窗口高（像素）
constexpr const char *kResourceRoot = "resources/"; // 相对 exe 的资源根目录
constexpr const char *kWindowTitle = "OpenGL Anti-Aliasing Demo";

// ---------- 投影 ----------
constexpr float kFovDegrees = 45.0f; // 垂直 FOV（度）
constexpr float kNearPlane = 0.1f;   // 近裁剪面
constexpr float kFarPlane = 100.0f;  // 远裁剪面

// ---------- 轨道球相机（VTK 风格：yaw/pitch/radius 绕 target）----------
constexpr float kInitialOrbitAngle = 35.0f; // 初始水平角 yaw（度）
constexpr float kInitialOrbitPitch = 18.0f; // 初始俯仰角 pitch（度）
constexpr float kCameraOrbitRadius = 6.5f;  // 初始相机到 target 的距离
constexpr float kCameraTargetY = 1.0f;      // 注视点世界 Y（对齐头盔中心高度）
constexpr float kMinOrbitRadius = 2.5f;     // 滚轮/中键拉近下限
constexpr float kMaxOrbitRadius = 20.0f;    // 拉远上限
// 鼠标横跨整个窗口宽度时，yaw 大约转过的度数（越大越灵敏）
constexpr float kMouseOrbitFullWidthDegrees = 100.0f;
constexpr float kPanFactor = 2.0f;              // 右键平移灵敏度系数
constexpr float kDollyFactor = 3.0f;            // 中键推拉（改 radius）灵敏度
constexpr float kScrollZoomSensitivity = 0.8f; // 滚轮每次滚动对 radius 的步进

// ---------- 测试场景（棋盘地板 + DamagedHelmet）----------
constexpr float kHeroHelmetScale = 1.35f; // 中央主头盔缩放
constexpr float kSideHelmetScale = 0.85f; // 左右侧头盔缩放
constexpr float kFloorHalfExtent = 8.0f;  // 棋盘地板半边长（世界单位，总宽 16）

// ---------- MSAA 采样档位（UI `[` `]` / 面板 2x·4x）----------
constexpr int kMsaaSamplesPresets[] = {2, 4}; // 可选多重采样数
constexpr int kMsaaPresetCount = 2;           // 上表元素个数
constexpr int kDefaultMsaaPresetIndex = 1;    // 默认用 4x（presets[1]）

// ---------- TAA 历史混合权重（越大越信当前帧，拖影越少、收敛越慢反向）----------
constexpr float kTaaBlendSettled = 0.1f;  // 静止：约 10% 当前 + 90% history
constexpr float kTaaBlendDragging = 0.9f; // 拖相机：加大当前帧占比，减轻鬼影

// Blinn-Phong 材质系数 (Ka, Kd, Ks)，与其它 Demo 对齐便于对比
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
