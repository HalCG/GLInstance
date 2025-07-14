#ifndef PBR_APP_HPP
#define PBR_APP_HPP

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

#include "Camera.hpp"
#include "Shader.hpp"
#include "StaticMesh.hpp"
#include "FrameBuffer.hpp"
#include "IblPrecomputation.hpp"

/**
 * @brief 渲染模型类型枚举
 */
enum class RenderModel {
    DamagedHelmet = 0, // 战损头盔 (丰富材质贴图场景)
    SphereGrid = 1,    // 7x7 球体网格 (物理金属性/粗糙度参数化测试阵列)
    Gun = 2            // 科幻枪械模型 (大面积金属与细节各向异性材质)
};

/**
 * @brief PbrApp 核心 OpenGL 4.3 PBR 渲染应用程序类
 * 
 * 管理窗口系统 (GLFW)、渲染循环、交互相机、纹理/网格资源管理、
 * IBL 离屏预计算管线、HDR 离屏 FrameBuffer 渲染 Pass、色调映射 Pass 以及 ImGui UI 绘制。
 */
class PbrApp {
public:
    PbrApp(int width = 1280, int height = 720, const std::string& title = "OpenGL PBR Renderer");
    ~PbrApp();

    /**
     * @brief 初始化 GLFW 窗口、GLAD 指针、ImGui 上下文以及 PBR/IBL 全部资源
     * @return 初始化成功返回 true
     */
    bool init();

    /**
     * @brief 启动主渲染循环 (Main Event Loop)
     */
    void run();

private:
    void processInput();
    void update(float deltaTime);
    void renderScene();
    void renderImGui();

    // GLFW 事件回调句柄
    static void framebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void mouseCallback(GLFWwindow* window, double xpos, double ypos);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);

    // 窗口与系统属性
    GLFWwindow* window_ = nullptr;
    int width_ = 1280;
    int height_ = 720;
    std::string title_;

    // 交互式轨道相机控制
    Camera camera_;
    bool firstMouse_ = true;
    double lastX_ = 640.0;
    double lastY_ = 360.0;
    bool isMousePressed_ = false;

    // 着色器对象 (Shader Objects)
    std::unique_ptr<Shader> pbrMeshShader_;     // 带 PBR 贴图网格着色器
    std::unique_ptr<Shader> pbrSphereShader_;   // 参数化 7x7 球体阵列着色器
    std::unique_ptr<Shader> skyboxShader_;      // 背景天空盒着色器
    std::unique_ptr<Shader> toneMappingShader_; // 色调映射与 Gamma 校正后处理着色器

    // 模型网格 (Static Meshes)
    std::unique_ptr<StaticMesh> helmetMesh_;
    std::unique_ptr<StaticMesh> sphereMesh_;
    std::unique_ptr<StaticMesh> gunMesh_;

    // IBL 环境光预计算纹理句柄
    GLuint envCubemap_ = 0;     // 原始 3D 环境 Cubemap (512x512)
    GLuint irradianceMap_ = 0; // 漫反射辐照度 Cubemap (32x32)
    GLuint prefilterMap_ = 0;  // 镜面反射预过滤 Cubemap (128x128 5-level Mipmaps)
    GLuint brdfLUT_ = 0;       // 2D BRDF LUT 查找表 (512x512 GL_RG16F)

    // 战损头盔模型 PBR 贴图
    GLuint helmetAlbedo_ = 0;
    GLuint helmetNormal_ = 0;
    GLuint helmetEmissive_ = 0;
    GLuint helmetAO_ = 0;
    GLuint helmetMetallic_ = 0;
    GLuint helmetRoughness_ = 0;

    // 枪械模型 PBR 贴图
    GLuint gunAlbedo_ = 0;
    GLuint gunNormal_ = 0;
    GLuint gunEmissive_ = 0;
    GLuint gunAO_ = 0;
    GLuint gunMetallic_ = 0;
    GLuint gunRoughness_ = 0;

    GLuint dummyBlackTex_ = 0; // 缺失贴图时的回退 1x1 纯黑纹理

    // 离屏 HDR 帧缓冲区对象 (GL_RGBA16F 浮点颜色附着)
    FrameBufferEx hdrFbo_;
    // IBL 离屏预计算管线对象
    IblPrecomputation iblPipeline_;

    // 渲染模式与物理光照参数状态
    RenderModel currentModel_ = RenderModel::DamagedHelmet;
    float exposure_ = 1.0f;
    float gamma_ = 2.2f;
    int toneMappingMode_ = 0; // 0: Reinhard, 1: ACES, 2: Uncharted2, 3: None
    float iblIntensity_ = 1.0f;
    bool renderSkybox_ = true;

    // 7x7 球体测试网格参数
    glm::vec3 sphereAlbedo_ = glm::vec3(1.0f, 0.0f, 0.0f);
    float sphereMetallic_ = 0.5f;
    float sphereRoughness_ = 0.5f;
    float sphereAO_ = 1.0f;
    bool useSphereGrid_ = true;

    // 点光源位置与颜色数组
    std::vector<glm::vec3> lightPositions_;
    std::vector<glm::vec3> lightColors_;
};

#endif // PBR_APP_HPP

