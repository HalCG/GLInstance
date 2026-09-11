#include "LightManager.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <random>

void LightManager::init()
{
    lights_.resize(AppConfig::kMaxLights);
    regenerate(activeCount_);

    glGenBuffers(1, &lightBuffer_);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, lightBuffer_);
    glBufferData(GL_SHADER_STORAGE_BUFFER, lights_.size() * sizeof(GpuPointLight), lights_.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    glGenBuffers(1, &tileCountBuffer_);
    glGenBuffers(1, &tileIndexBuffer_);
}

void LightManager::shutdown()
{
    if (lightBuffer_)
    {
        glDeleteBuffers(1, &lightBuffer_);
        lightBuffer_ = 0;
    }
    if (tileCountBuffer_)
    {
        glDeleteBuffers(1, &tileCountBuffer_);
        tileCountBuffer_ = 0;
    }
    if (tileIndexBuffer_)
    {
        glDeleteBuffers(1, &tileIndexBuffer_);
        tileIndexBuffer_ = 0;
    }
}

// 固定种子随机生成 activeCount_ 个点光源
void LightManager::regenerate(int activeCount)
{
    activeCount_ = std::max(1, std::min(activeCount, AppConfig::kMaxLights));

    std::mt19937 rng(1337);
    std::uniform_real_distribution<float> posX(-4.0f, 4.0f);
    std::uniform_real_distribution<float> posY(0.5f, 3.5f);
    std::uniform_real_distribution<float> posZ(-4.0f, 4.0f);
    std::uniform_real_distribution<float> hue(0.0f, 1.0f);
    std::uniform_real_distribution<float> intensity(0.6f, 1.4f);

    for (int i = 0; i < AppConfig::kMaxLights; ++i)
    {
        GpuPointLight light{};
        if (i < activeCount_)
        {
            light.positionRadius = glm::vec4(posX(rng), posY(rng), posZ(rng), 3.5f);
            const float h = hue(rng);
            // 生成彩虹色
            const glm::vec3 color = glm::abs(glm::vec3(h * 6.0f + 0.0f, h * 6.0f + 2.0f, h * 6.0f + 4.0f) -
                                             glm::vec3(3.0f));
            light.colorIntensity = glm::vec4(color, intensity(rng));
        }
        else
        {
            light.positionRadius = glm::vec4(0.0f);
            light.colorIntensity = glm::vec4(0.0f);
        }
        lights_[static_cast<size_t>(i)] = light;
    }
}

void LightManager::uploadToGpu() const
{
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, lightBuffer_);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, lights_.size() * sizeof(GpuPointLight), lights_.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

// Forward+ 渲染路径的核心：CPU 端的 Tile 光源剔除 (Tile Light Culling)
// 算法逻辑：
// 1. 将屏幕划分为 N x M 个 16x16 像素的 Tile 块。
// 2. 遍历所有点光源，将点光源的 3D 球体包围盒 (AABB) 投影到屏幕空间，算出其覆盖的 2D Tile 范围。
// 3. 将可能照到该 Tile 的光源索引 (Index) 写入 CPU 端的临时数组。
// 4. 将计算好的 Tile 光源数量 (tileCountBuffer_) 与光源索引表 (tileIndexBuffer_) 上传至 GPU SSBO，供 Shader 查询。
void LightManager::buildForwardPlusTiles(int screenWidth, int screenHeight, const FrameCamera &camera)
{
    // -------------------------------------------------------------------------
    // 1. 计算 Tile 网格数量并初始化 CPU 缓存
    // -------------------------------------------------------------------------
    // 向上取整计算横向 (X) 和纵向 (Y) 的 Tile 块数量 (例如 1920 / 16 = 120 个 Tile)
    tilesX_ = (screenWidth + AppConfig::kTileSize - 1) / AppConfig::kTileSize;
    tilesY_ = (screenHeight + AppConfig::kTileSize - 1) / AppConfig::kTileSize;
    const int tileCount = tilesX_ * tilesY_;

    // counts: 记录每个 Tile 当前关联的光源数量
    std::vector<uint32_t> counts(static_cast<size_t>(tileCount), 0);
    // indices: 展平二维数组，每个 Tile 分配 kMaxLightsPerTile (如 64) 个 Slot 存放光源 Index
    std::vector<uint32_t> indices(static_cast<size_t>(tileCount) * AppConfig::kMaxLightsPerTile, 0xFFFFFFFFu);

    // 组合 View 与 Projection 矩阵，用于将世界坐标变换至裁剪空间 (Clip Space)
    const glm::mat4 viewProj = camera.projection * camera.view;

    // -------------------------------------------------------------------------
    // 2. 逐光源投影求 2D 屏幕包围盒 (Screen-Space AABB)
    // -------------------------------------------------------------------------
    for (int i = 0; i < activeCount_; ++i)
    {
        const glm::vec3 center = glm::vec3(lights_[static_cast<size_t>(i)].positionRadius);
        const float radius = lights_[static_cast<size_t>(i)].positionRadius.w;

        // 2.1 构建点光源包围球在世界空间中的 3D 轴对齐包围盒 (AABB 的 8 个顶点)
        glm::vec3 corners[8];
        for (int x = 0; x < 2; ++x)
        {
            for (int y = 0; y < 2; ++y)
            {
                for (int z = 0; z < 2; ++z)
                {
                    const glm::vec3 offset((x ? 1.0f : -1.0f) * radius, (y ? 1.0f : -1.0f) * radius,
                                           (z ? 1.0f : -1.0f) * radius);
                    corners[x * 4 + y * 2 + z] = center + offset;
                }
            }
        }

        float minX = static_cast<float>(screenWidth);
        float minY = static_cast<float>(screenHeight);
        float maxX = 0.0f;
        float maxY = 0.0f;
        bool anyInFront = false; // 标记是否有顶点在相机前方

        // 2.2 将 8 个顶点变换到屏幕像素空间，并求 2D 包围盒的最小/最大外包矩形 [minX, maxX, minY, maxY]
        for (const glm::vec3 &world : corners)
        {
            glm::vec4 clip = viewProj * glm::vec4(world, 1.0f);
            // 裁掉在相机近裁剪平面后面的点 (w <= 0)
            if (clip.w <= 0.0f)
            {
                continue;
            }
            anyInFront = true;
            // 透视除法转为 NDC 坐标 (范围 [-1, 1])****
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            // 将 NDC 坐标从 [-1, 1] 映射到 屏幕像素坐标 [0, Width/Height] (注意 Y 轴反向映射)
            const float sx = (ndc.x * 0.5f + 0.5f) * screenWidth;
            const float sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * screenHeight;
            minX = std::min(minX, sx);
            minY = std::min(minY, sy);
            maxX = std::max(maxX, sx);
            maxY = std::max(maxY, sy);
        }

        // 如果光源 8 个顶点完全在相机身后，直接跳过该光源
        if (!anyInFront)
        {
            continue;
        }

        // 2.3 将像素坐标限制在屏幕合法像素分辨率范围内
        minX = glm::clamp(minX, 0.0f, static_cast<float>(screenWidth - 1));
        maxX = glm::clamp(maxX, 0.0f, static_cast<float>(screenWidth - 1));
        minY = glm::clamp(minY, 0.0f, static_cast<float>(screenHeight - 1));
        maxY = glm::clamp(maxY, 0.0f, static_cast<float>(screenHeight - 1));

        // 2.4 计算该光源像素范围对应的 Tile 索引区间 [tileMinX ~ tileMaxX, tileMinY ~ tileMaxY]
        const int tileMinX = static_cast<int>(minX) / AppConfig::kTileSize;
        const int tileMaxX = static_cast<int>(maxX) / AppConfig::kTileSize;
        const int tileMinY = static_cast<int>(minY) / AppConfig::kTileSize;
        const int tileMaxY = static_cast<int>(maxY) / AppConfig::kTileSize;

        // -------------------------------------------------------------------------
        // 3. 将光源索引写入其覆盖的所有 Tile 的列表数据中
        // -------------------------------------------------------------------------
        for (int ty = tileMinY; ty <= tileMaxY; ++ty)
        {
            for (int tx = tileMinX; tx <= tileMaxX; ++tx)
            {
                // 越界保护
                if (tx < 0 || ty < 0 || tx >= tilesX_ || ty >= tilesY_)
                {
                    continue;
                }
                const int tileIndex = ty * tilesX_ + tx;
                uint32_t &count = counts[static_cast<size_t>(tileIndex)];
                // 若单个 Tile 关联的光源数已满 (超过 kMaxLightsPerTile)，则舍弃多余光源
                if (count >= static_cast<uint32_t>(AppConfig::kMaxLightsPerTile))
                {
                    continue;
                }
                // 在 indices 数组中该 Tile 专属的空间存入光源索引 i
                indices[static_cast<size_t>(tileIndex) * AppConfig::kMaxLightsPerTile + count] =
                    static_cast<uint32_t>(i);
                ++count; // 自增当前 Tile 的光源计数
            }
        }
    }

    // -------------------------------------------------------------------------
    // 4. 将计算好的 Tile 数据上传至 GPU 显存 (SSBO 存储缓冲)
    // -------------------------------------------------------------------------
    // 上传每个 Tile 的有效光源数量 (Binding 1)
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, tileCountBuffer_);
    glBufferData(GL_SHADER_STORAGE_BUFFER, counts.size() * sizeof(uint32_t), counts.data(), GL_DYNAMIC_DRAW);
    // 上传每个 Tile 包含的光源 Index 列表 (Binding 2)
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, tileIndexBuffer_);
    glBufferData(GL_SHADER_STORAGE_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_DYNAMIC_DRAW);
    // 解绑 SSBO
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}
