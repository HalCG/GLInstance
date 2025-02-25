#include "LightManager.hpp"

#include <chrono>
#include <cmath>
#include <cstring>
#include <random>

namespace {

glm::vec3 hsvToRgb(float h, float s, float v) {
    const float c = v * s;
    const float x = c * (1.0f - std::fabs(std::fmod(h * 6.0f, 2.0f) - 1.0f));
    const float m = v - c;
    glm::vec3 rgb(0.0f);
    if (h < 1.0f / 6.0f) {
        rgb = {c, x, 0};
    } else if (h < 2.0f / 6.0f) {
        rgb = {x, c, 0};
    } else if (h < 3.0f / 6.0f) {
        rgb = {0, c, x};
    } else if (h < 4.0f / 6.0f) {
        rgb = {0, x, c};
    } else if (h < 5.0f / 6.0f) {
        rgb = {x, 0, c};
    } else {
        rgb = {c, 0, x};
    }
    return rgb + glm::vec3(m);
}

bool nearlyEqualMat(const glm::mat4 &a, const glm::mat4 &b) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (std::fabs(a[c][r] - b[c][r]) > 1e-5f) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

void LightManager::init() {
    lights_.resize(AppConfig::kMaxLights);
    regenerateInternal(activeCount_);

    glGenBuffers(1, &lightBuffer_);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, lightBuffer_);
    glBufferData(GL_SHADER_STORAGE_BUFFER, lights_.size() * sizeof(GpuPointLight), lights_.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    glGenBuffers(1, &tileCountBuffer_);
    glGenBuffers(1, &tileIndexBuffer_);
}

void LightManager::shutdown() {
    if (lightBuffer_) {
        glDeleteBuffers(1, &lightBuffer_);
        lightBuffer_ = 0;
    }
    if (tileCountBuffer_) {
        glDeleteBuffers(1, &tileCountBuffer_);
        tileCountBuffer_ = 0;
    }
    if (tileIndexBuffer_) {
        glDeleteBuffers(1, &tileIndexBuffer_);
        tileIndexBuffer_ = 0;
    }
}

void LightManager::setStressLighting(bool enabled) {
    if (stressLighting_ == enabled) {
        return;
    }
    stressLighting_ = enabled;
    regenerateInternal(activeCount_);
}

void LightManager::regenerate(int activeCount) {
    regenerateInternal(activeCount);
}

void LightManager::regenerateInternal(int activeCount) {
    activeCount_ = std::max(1, std::min(activeCount, AppConfig::kMaxLights));
    tilesDirty_ = true;

    std::mt19937 rng(1337);
    // Stress：大半径让大多数片元无法 early-out；关闭时半径小，灯数变化对 GPU 几乎无感
    const float posSpan = stressLighting_ ? 9.0f : 7.5f;
    const float yMin = stressLighting_ ? 0.6f : 0.8f;
    const float yMax = stressLighting_ ? 4.8f : 4.5f;
    const float radiusMin = stressLighting_ ? 9.0f : 1.8f;
    const float radiusMax = stressLighting_ ? 14.0f : 3.2f;
    const float intensityMin = stressLighting_ ? 0.22f : 0.10f;
    const float intensityMax = stressLighting_ ? 0.42f : 0.22f;

    std::uniform_real_distribution<float> posX(-posSpan, posSpan);
    std::uniform_real_distribution<float> posY(yMin, yMax);
    std::uniform_real_distribution<float> posZ(-posSpan, posSpan);
    std::uniform_real_distribution<float> hue(0.0f, 1.0f);
    std::uniform_real_distribution<float> sat(0.25f, 0.55f);
    std::uniform_real_distribution<float> val(0.75f, 1.0f);
    std::uniform_real_distribution<float> intensity(intensityMin, intensityMax);
    std::uniform_real_distribution<float> radius(radiusMin, radiusMax);

    for (int i = 0; i < AppConfig::kMaxLights; ++i) {
        GpuPointLight light{};
        if (i < activeCount_) {
            light.positionRadius = glm::vec4(posX(rng), posY(rng), posZ(rng), radius(rng));
            light.colorIntensity = glm::vec4(hsvToRgb(hue(rng), sat(rng), val(rng)), intensity(rng));
        }
        lights_[static_cast<size_t>(i)] = light;
    }
}

void LightManager::uploadToGpu() const {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, lightBuffer_);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, lights_.size() * sizeof(GpuPointLight), lights_.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

float LightManager::buildForwardPlusTiles(int screenWidth, int screenHeight, const FrameCamera &camera) {
    const glm::mat4 viewProj = camera.projection * camera.view;
    if (!tilesDirty_ && screenWidth == lastScreenW_ && screenHeight == lastScreenH_ &&
        nearlyEqualMat(viewProj, lastViewProj_)) {
        return 0.0f;
    }

    const auto t0 = std::chrono::steady_clock::now();

    tilesX_ = (screenWidth + AppConfig::kTileSize - 1) / AppConfig::kTileSize;
    tilesY_ = (screenHeight + AppConfig::kTileSize - 1) / AppConfig::kTileSize;
    const int tileCount = tilesX_ * tilesY_;

    tileCountsCpu_.assign(static_cast<size_t>(tileCount), 0);
    // 只保证容量，不必每帧 memset 整表为 0xFFFFFFFF（shader 用 counts 截断）
    const size_t indexSlots = static_cast<size_t>(tileCount) * AppConfig::kMaxLightsPerTile;
    if (tileIndicesCpu_.size() != indexSlots) {
        tileIndicesCpu_.assign(indexSlots, 0u);
    }

    for (int i = 0; i < activeCount_; ++i) {
        const glm::vec3 center = glm::vec3(lights_[static_cast<size_t>(i)].positionRadius);
        const float radius = lights_[static_cast<size_t>(i)].positionRadius.w;

        glm::vec3 corners[8];
        for (int x = 0; x < 2; ++x) {
            for (int y = 0; y < 2; ++y) {
                for (int z = 0; z < 2; ++z) {
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
        bool anyInFront = false;

        for (const glm::vec3 &world : corners) {
            glm::vec4 clip = viewProj * glm::vec4(world, 1.0f);
            if (clip.w <= 0.0f) {
                continue;
            }
            anyInFront = true;
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            const float sx = (ndc.x * 0.5f + 0.5f) * screenWidth;
            const float sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * screenHeight;
            minX = std::min(minX, sx);
            minY = std::min(minY, sy);
            maxX = std::max(maxX, sx);
            maxY = std::max(maxY, sy);
        }

        if (!anyInFront) {
            continue;
        }

        minX = glm::clamp(minX, 0.0f, static_cast<float>(screenWidth - 1));
        maxX = glm::clamp(maxX, 0.0f, static_cast<float>(screenWidth - 1));
        minY = glm::clamp(minY, 0.0f, static_cast<float>(screenHeight - 1));
        maxY = glm::clamp(maxY, 0.0f, static_cast<float>(screenHeight - 1));

        const int tileMinX = static_cast<int>(minX) / AppConfig::kTileSize;
        const int tileMaxX = static_cast<int>(maxX) / AppConfig::kTileSize;
        const int tileMinY = static_cast<int>(minY) / AppConfig::kTileSize;
        const int tileMaxY = static_cast<int>(maxY) / AppConfig::kTileSize;

        for (int ty = tileMinY; ty <= tileMaxY; ++ty) {
            for (int tx = tileMinX; tx <= tileMaxX; ++tx) {
                if (tx < 0 || ty < 0 || tx >= tilesX_ || ty >= tilesY_) {
                    continue;
                }
                const int tileIndex = ty * tilesX_ + tx;
                uint32_t &count = tileCountsCpu_[static_cast<size_t>(tileIndex)];
                if (count >= static_cast<uint32_t>(AppConfig::kMaxLightsPerTile)) {
                    continue;
                }
                tileIndicesCpu_[static_cast<size_t>(tileIndex) * AppConfig::kMaxLightsPerTile + count] =
                    static_cast<uint32_t>(i);
                ++count;
            }
        }
    }

    const GLsizeiptr countBytes = static_cast<GLsizeiptr>(tileCountsCpu_.size() * sizeof(uint32_t));
    const GLsizeiptr indexBytes = static_cast<GLsizeiptr>(tileIndicesCpu_.size() * sizeof(uint32_t));

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, tileCountBuffer_);
    if (countBytes > tileCountBytes_) {
        glBufferData(GL_SHADER_STORAGE_BUFFER, countBytes, tileCountsCpu_.data(), GL_DYNAMIC_DRAW);
        tileCountBytes_ = countBytes;
    } else {
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, countBytes, tileCountsCpu_.data());
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, tileIndexBuffer_);
    if (indexBytes > tileIndexBytes_) {
        glBufferData(GL_SHADER_STORAGE_BUFFER, indexBytes, tileIndicesCpu_.data(), GL_DYNAMIC_DRAW);
        tileIndexBytes_ = indexBytes;
    } else {
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, indexBytes, tileIndicesCpu_.data());
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    lastViewProj_ = viewProj;
    lastScreenW_ = screenWidth;
    lastScreenH_ = screenHeight;
    tilesDirty_ = false;

    uint64_t sumCounts = 0;
    uint32_t saturated = 0;
    const uint32_t cap = static_cast<uint32_t>(AppConfig::kMaxLightsPerTile);
    for (uint32_t c : tileCountsCpu_) {
        sumCounts += c;
        if (c >= cap) {
            ++saturated;
        }
    }
    avgLightsPerTile_ = tileCount > 0 ? static_cast<float>(sumCounts) / static_cast<float>(tileCount) : 0.0f;
    saturatedTileRatio_ = tileCount > 0 ? static_cast<float>(saturated) / static_cast<float>(tileCount) : 0.0f;

    const auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<float, std::milli>(t1 - t0).count();
}
