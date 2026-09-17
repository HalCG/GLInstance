#include "DeferredRenderer.hpp"

#include "AppConfig.hpp"

#include <glm/gtc/matrix_inverse.hpp>
#include <vector>

namespace {
const float kQuadVerts[] = {
    -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, -1.0f, 1.0f, 0.0f,
    1.0f,  1.0f,  1.0f, 1.0f, -1.0f, 1.0f,  0.0f, 1.0f,
};
} // namespace

bool DeferredRenderer::init() {
    // 编译延迟渲染所需的 3 个着色器程序：
    // 1. geometryShader_：几何阶段（写入 GBuffer）
    // 2. lightingShader_：光照阶段（全屏 Quad 集中计算光照）
    // 3. debugShader_：GBuffer 各通道直观可视化调试
    geometryShader_ = std::make_unique<Shader>(AppConfig::shaderPath("mesh.vert").c_str(),
                                               AppConfig::shaderPath("geometry.frag").c_str());
    lightingShader_ = std::make_unique<Shader>(AppConfig::shaderPath("fullscreen.vert").c_str(),
                                               AppConfig::shaderPath("deferred_lighting.frag").c_str());
    debugShader_ = std::make_unique<Shader>(AppConfig::shaderPath("fullscreen.vert").c_str(),
                                            AppConfig::shaderPath("gbuffer_debug.frag").c_str());

    // 创建光照阶段和调试阶段使用的全屏 Quad VAO/VBO
    // Quad 包含 4 个顶点，绘制为 GL_TRIANGLE_FAN（覆盖全屏 NDC [-1, 1] 坐标空间）
    glGenVertexArrays(1, &quadVao_);
    glGenBuffers(1, &quadVbo_);
    glBindVertexArray(quadVao_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVerts), kQuadVerts, GL_STATIC_DRAW);
    // Attribute 0: vec2 aPos
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void *>(0));
    // Attribute 1: vec2 aTexCoords
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void *>(2 * sizeof(float)));
    glBindVertexArray(0);

    return geometryShader_ && lightingShader_ && debugShader_ && geometryShader_->ID != 0;
}

void DeferredRenderer::shutdown() {
    destroyGBuffer();
    geometryShader_.reset();
    lightingShader_.reset();
    debugShader_.reset();
    if (quadVbo_) {
        glDeleteBuffers(1, &quadVbo_);
        quadVbo_ = 0;
    }
    if (quadVao_) {
        glDeleteVertexArrays(1, &quadVao_);
        quadVao_ = 0;
    }
}

void DeferredRenderer::resize(int width, int height) {
    if (width == width_ && height == height_ && gBufferFbo_) {
        return;
    }
    destroyGBuffer();
    createGBuffer(width, height);
    width_ = width;
    height_ = height;
}

// 分配 GBuffer FBO：包含 albedo(RGBA8) + normal(RGB16F) + material(RGBA8) + depth 四附件
// 【OpenGL 状态机关键点】：
// 1. glGenFramebuffers / glBindFramebuffer(GL_FRAMEBUFFER, fbo) 将当前全局状态绑定到我们自定义的 FBO。
// 2. glFramebufferTexture2D 将生成的 2D 纹理挂载到特定 Attachment 插槽。
// 3. glDrawBuffers 显式告诉 OpenGL：当前 FBO 同时向哪几个 Color Attachments 绘制（开启 MRT）。
void DeferredRenderer::createGBuffer(int width, int height) {
    glGenFramebuffers(1, &gBufferFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, gBufferFbo_);

    // 附件 0: gAlbedo (RGBA8 UNORM) - 8-bit/通道，低显存带宽，存储 Surface BaseColor
    glGenTextures(1, &gAlbedo_);
    glBindTexture(GL_TEXTURE_2D, gAlbedo_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gAlbedo_, 0);

    // 附件 1: gNormal (RGB16F Half-Float) - 16-bit 浮点，支持 [-1, 1] 负数，防止漫反射/高光出现色彩断层
    glGenTextures(1, &gNormal_);
    glBindTexture(GL_TEXTURE_2D, gNormal_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gNormal_, 0);

    // 附件 2: gMaterial (RGBA8 UNORM) - 8-bit/通道，存储 Ambient, Diffuse, Specular 系数
    glGenTextures(1, &gMaterial_);
    glBindTexture(GL_TEXTURE_2D, gMaterial_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, gMaterial_, 0);

    // 深度附件: gDepth (DEPTH_COMPONENT32F) - 高精度 32-bit 浮点深度，用于在 Lighting Pass 中逆矩阵重建世界坐标
    glGenTextures(1, &gDepth_);
    glBindTexture(GL_TEXTURE_2D, gDepth_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    // 禁用深度自动对比模式 (GL_NONE)：确保 Shader 采样返回原始 Depth 浮点值 (0~1)，而非 Shadow 比较结果 (0/1)
    // 原因：Deferred Lighting Pass 需读取真实 Depth 值，输入 reconstructWorldPos 通过逆 MVP 矩阵重构像素 3D 世界坐标
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gDepth_, 0);

    // 开启 MRT (Multiple Render Targets)：
    // attachments 数组的下标索引（0, 1, 2）与 geometry.frag 中的 `layout (location = N)` 一一建立映射关联。
    // 如果不显式调用 glDrawBuffers，OpenGL 默认只会写入 COLOR_ATTACHMENT0，后两个附件的数据将被静默丢弃。
    const GLenum attachments[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
    glDrawBuffers(3, attachments);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        // Keep running; error will show as blank output.
    }
    // 恢复绑定默认帧缓冲区（避免污染后续绘制）
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void DeferredRenderer::destroyGBuffer() {
    if (gAlbedo_) {
        glDeleteTextures(1, &gAlbedo_);
        gAlbedo_ = 0;
    }
    if (gNormal_) {
        glDeleteTextures(1, &gNormal_);
        gNormal_ = 0;
    }
    if (gMaterial_) {
        glDeleteTextures(1, &gMaterial_);
        gMaterial_ = 0;
    }
    if (gDepth_) {
        glDeleteTextures(1, &gDepth_);
        gDepth_ = 0;
    }
    if (gBufferFbo_) {
        glDeleteFramebuffers(1, &gBufferFbo_);
        gBufferFbo_ = 0;
    }
}

void DeferredRenderer::drawFullscreenQuad() {
    glBindVertexArray(quadVao_);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(0);
}

void DeferredRenderer::render(const Scene &scene, LightManager &lights, const FrameCamera &camera, int width,
                              int height, PerfStats &stats, bool showGBufferDebug, bool enableHDR) {
    resize(width, height);

    // =========================================================================
    // Pass 1: Geometry Pass（几何阶段）
    // 状态机调整：
    // 1. glBindFramebuffer(GL_FRAMEBUFFER, gBufferFbo_) 将渲染目标切到 GBuffer FBO。
    // 2. glEnable(GL_DEPTH_TEST) 开启深度测试，确保物体前遮后正常挡住，并将深度写入 gDepth_ 纹理。
    // 3. geometryShader_ 内部仅将 Albedo / Normal / Material 写入 GBuffer，不进行任何点光源计算。
    // =========================================================================
    stats.geometryPass.begin();
    glBindFramebuffer(GL_FRAMEBUFFER, gBufferFbo_);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    geometryShader_->use();
    geometryShader_->setMat4("view", camera.view);
    geometryShader_->setMat4("projection", camera.projection);
    scene.drawFloor(*geometryShader_);
    scene.drawSpotMeshes(*geometryShader_);
    // 切回默认屏幕帧缓冲区（0）
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    stats.frameStats().geometryPassMs = stats.geometryPass.endMs();

    // Debug 分支：按 G 键直接展示 GBuffer 中各个附件的纹理内容
    if (showGBufferDebug) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        debugShader_->use();
        debugShader_->setInt("uDebugMode", 1);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, gAlbedo_);
        debugShader_->setInt("uGAlbedo", 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, gNormal_);
        debugShader_->setInt("uGNormal", 1);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, gMaterial_);
        debugShader_->setInt("uGMaterial", 2);
        drawFullscreenQuad();
        stats.frameStats().lightingPassMs = 0.0f;
        return;
    }

    // =========================================================================
    // Pass 2: Lighting Pass（延迟光照阶段）
    // 状态机调整：
    // 1. 渲染目标为默认屏幕缓冲区（0）。
    // 2. glDisable(GL_DEPTH_TEST) 关闭深度测试！因为该阶段只绘制一个覆盖全屏的 Quad，
    //    像素的光照计算完全基于从 GBuffer 采样得到的地理与材质数据，不再依赖光栅化 Depth Test。
    // 3. SSBO 绑定：glBindBufferBase 将光源数据缓冲区绑定到绑定点 0 (binding = 0)。
    // 4. 纹理单元绑定：glActiveTexture(GL_TEXTUREn) 将 4 个 GBuffer 纹理槽激活并设置给 Shader Uniform。
    // =========================================================================
    stats.lightingPass.begin();
    glClearColor(0.08f, 0.09f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    // 上传并绑定光源 SSBO 缓冲区 (binding = 0)
    lights.uploadToGpu();
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, lights.lightBuffer());

    lightingShader_->use();
    lightingShader_->setVec3("uCameraPos", camera.eye);
    // 传入逆矩阵，供 deferred_lighting.frag 根据 depth 重建世界空间坐标 (worldPos)
    lightingShader_->setMat4("uInvView", glm::inverse(camera.view));
    lightingShader_->setMat4("uInvProjection", glm::inverse(camera.projection));
    lightingShader_->setInt("uLightCount", lights.activeCount());
    lightingShader_->setBool("uEnableHDR", enableHDR);

    // 绑定 GBuffer 纹理到 OpenGL 纹理单元 0, 1, 2, 3
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gAlbedo_);
    lightingShader_->setInt("uGAlbedo", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gNormal_);
    lightingShader_->setInt("uGNormal", 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, gMaterial_);
    lightingShader_->setInt("uGMaterial", 2);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, gDepth_);
    lightingShader_->setInt("uGDepth", 3);

    // 绘制全屏 Quad 执行光照计算
    drawFullscreenQuad();

    // 【状态机清理】：解绑 SSBO 绑定点，并重新开启 Depth Test 以备下一帧使用
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
    glEnable(GL_DEPTH_TEST);

    stats.frameStats().lightingPassMs = stats.lightingPass.endMs();
}
