#ifndef LIGHT_MANAGER_HPP
#define LIGHT_MANAGER_HPP

#include "AppConfig.hpp"
#include "RenderTypes.hpp"

#include <glad/glad.h>
#include <vector>

// 点光源管理：SSBO 上传 + Forward+ tile 可见性裁剪
class LightManager {
public:
    void init();
    void shutdown();
    void regenerate(int activeCount); // 按数量随机生成点光源

    void uploadToGpu() const;
    // 返回 CPU 剔除耗时（毫秒）。相机/光源未变时跳过重建。
    float buildForwardPlusTiles(int screenWidth, int screenHeight, const FrameCamera &camera);

    void markTilesDirty() { tilesDirty_ = true; }
    void setStressLighting(bool enabled);
    bool stressLighting() const { return stressLighting_; }

    int activeCount() const { return activeCount_; }
    const std::vector<GpuPointLight> &lights() const { return lights_; }
    // Forward+ 诊断：最近一次 tile 重建后的平均每格灯数 / 顶满格子占比
    float averageLightsPerTile() const { return avgLightsPerTile_; }
    float saturatedTileRatio() const { return saturatedTileRatio_; }

    GLuint lightBuffer() const { return lightBuffer_; }
    GLuint tileCountBuffer() const { return tileCountBuffer_; }
    GLuint tileIndexBuffer() const { return tileIndexBuffer_; }
    int tilesX() const { return tilesX_; }
    int tilesY() const { return tilesY_; }

private:
    void regenerateInternal(int activeCount);

    std::vector<GpuPointLight> lights_;
    std::vector<uint32_t> tileCountsCpu_;
    std::vector<uint32_t> tileIndicesCpu_;

    int activeCount_ = 64;
    int tilesX_ = 0;
    int tilesY_ = 0;
    bool tilesDirty_ = true;
    bool stressLighting_ = true; // 默认开：灯覆盖更大，GPU 成本随灯数更明显
    float avgLightsPerTile_ = 0.0f;
    float saturatedTileRatio_ = 0.0f;
    glm::mat4 lastViewProj_{1.0f};
    int lastScreenW_ = 0;
    int lastScreenH_ = 0;

    GLuint lightBuffer_ = 0;
    GLuint tileCountBuffer_ = 0;
    GLuint tileIndexBuffer_ = 0;
    GLsizeiptr tileCountBytes_ = 0;
    GLsizeiptr tileIndexBytes_ = 0;
};

#endif
