#include "ForwardPlusRenderer.hpp"

#include "AppConfig.hpp"

bool ForwardPlusRenderer::init() {
    // 编译 Forward+ 渲染着色器：mesh.vert + forward_plus.frag
    shader_ = std::make_unique<Shader>(AppConfig::shaderPath("mesh.vert").c_str(),
                                       AppConfig::shaderPath("forward_plus.frag").c_str());
    return shader_ && shader_->ID != 0;
}

void ForwardPlusRenderer::shutdown() {
    shader_.reset();
}

// Forward+（Tiled Forward Rendering）管线：
// 核心思想：将屏幕划分为 16x16 像素的网格（Tiles），在光照着色之前对每个 Tile 进行光源求交剔除（Light Culling）。
// 复杂度：从全量遍历 O(Objects × Lights) 降为按 Tile 精确循环 O(Objects × Lights_per_Tile)。
void ForwardPlusRenderer::render(const Scene &scene, LightManager &lights, const FrameCamera &camera, int width,
                                 int height, PerfStats &stats, bool enableHDR) {
    // =========================================================================
    // 阶段 1：光源剔除（Light Culling）
    // CPU 侧对全屏 16x16 Tile 构建视锥体（Frustum），剔除不在该 Tile 影响范围内的点光源，
    // 生成两个 SSBO 数组：TileCounts (每个 Tile 的灯数) 和 TileIndices (每个 Tile 包含的光源索引列表)。
    // =========================================================================
    // Cull 是 CPU 工作：用 CPU 计时，不要包在 GL_TIME_ELAPSED 里
    stats.frameStats().cullPassMs = lights.buildForwardPlusTiles(width, height, camera);

    // =========================================================================
    // 阶段 2：按 Tile 前向着色（Tile-based Shading）
    // =========================================================================
    stats.shadingPass.begin();
    glClearColor(0.08f, 0.09f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    lights.uploadToGpu();
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, lights.lightBuffer());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, lights.tileCountBuffer());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, lights.tileIndexBuffer());

    // 激活 Forward+ 着色器并传参
    shader_->use();
    shader_->setMat4("view", camera.view);
    shader_->setMat4("projection", camera.projection);
    shader_->setVec3("uCameraPos", camera.eye);
    shader_->setVec3("uMaterialK", AppConfig::materialCoeffs());
    shader_->setInt("uLightCount", lights.activeCount());
    shader_->setInt("uTilesX", lights.tilesX());
    shader_->setInt("uTilesY", lights.tilesY());
    shader_->setInt("uTileSize", AppConfig::kTileSize);
    shader_->setInt("uMaxLightsPerTile", AppConfig::kMaxLightsPerTile);
    shader_->setBool("uEnableHDR", enableHDR);

    // 绘制场景几何体：在 forward_plus.frag 中根据 gl_FragCoord 计算 Tile 坐标并只循环本 Tile 影响的光源
    scene.drawFloor(*shader_);
    scene.drawSpotMeshes(*shader_);

    // 【状态机解绑清理】：解绑绑定点 0, 1, 2，恢复 GL_SHADER_STORAGE_BUFFER 默认状态
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, 0);

    stats.frameStats().shadingPassMs = stats.shadingPass.endMs();
}



