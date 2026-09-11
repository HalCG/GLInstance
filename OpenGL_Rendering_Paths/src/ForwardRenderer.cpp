#include "ForwardRenderer.hpp"

#include "AppConfig.hpp"

#include <glad/glad.h>

bool ForwardRenderer::init() {
    // 编译前向渲染（Forward Rendering）着色器：mesh.vert + forward.frag
    shader_ = std::make_unique<Shader>(AppConfig::shaderPath("mesh.vert").c_str(),
                                       AppConfig::shaderPath("forward.frag").c_str());
    return shader_ && shader_->ID != 0;
}

void ForwardRenderer::shutdown() {
    shader_.reset();
}

// 经典前向渲染（Forward Rendering）：
// 针对场景中的每个几何体对象，光栅化后在 Fragment Shader 中循环遍历全量光源进行着色计算。
// 复杂度：O(Objects × Lights)，在光源数量极大时性能下降明显。
void ForwardRenderer::render(const Scene &scene, LightManager &lights, const FrameCamera &camera, int /*width*/,
                             int /*height*/, PerfStats &stats) {
    stats.forwardPass.begin();

    // 设置默认屏幕缓冲区的清屏颜色并清除颜色/深度缓冲区
    glClearColor(0.08f, 0.09f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 开启深度测试，采用默认 GL_LESS 比较规则（只有距离相机更近的片源才能通过测试并写入帧缓冲区）
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    // 1. 将 CPU 维护的点光源结构体数组上传至 GPU 的 SSBO (Shader Storage Buffer Object)
    lights.uploadToGpu();

    // 2. 【SSBO 绑定点机制】：将包含点光源数据的 SSBO 绑定到 OpenGL 状态机的绑定点 0 (binding = 0)
    //    它与 GLSL 中的 `layout(std430, binding = 0) readonly buffer LightBuffer` 隐式链接关联。
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, lights.lightBuffer());

    // 3. 激活 Forward 着色器并设置 Uniform 变量
    shader_->use();
    shader_->setMat4("view", camera.view);
    shader_->setMat4("projection", camera.projection);
    shader_->setVec3("uCameraPos", camera.eye);
    shader_->setVec3("uMaterialK", AppConfig::materialCoeffs());
    shader_->setInt("uLightCount", lights.activeCount());

    // 4. 绘制场景中的网格物体（地面 + Spot 模型）
    scene.drawFloor(*shader_);
    scene.drawSpotMeshes(*shader_);

    // 5. 【状态机解绑清理】：解绑 SSBO 绑定点 0，将 GL_SHADER_STORAGE_BUFFER 状态置回 0，防止影响后续渲染 Pass
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);

    stats.frameStats().forwardPassMs = stats.forwardPass.endMs();
}



