#ifndef PERF_STATS_HPP
#define PERF_STATS_HPP

#include "FpsMeter.hpp"
#include "RenderTypes.hpp"

#include <glad/glad.h>

class GpuTimer {
public:
    void init();
    void shutdown();
    void begin();
    float endMs();
    void setEnabled(bool enabled) { enabled_ = enabled; }

private:
    static constexpr int kQueryCount = 2;
    GLuint queries_[kQueryCount] = {0, 0};
    int writeIndex_ = 0;
    bool active_ = false;
    bool enabled_ = true;
    float lastMs_ = 0.0f;
};

// 渲染路径 Demo 性能统计：forward / geometry / lighting / cull / shading 各 pass 计时
class PerfStats {
public:
    void beginFrame();
    void endFrame();

    GpuTimer forwardPass;
    GpuTimer geometryPass;
    GpuTimer lightingPass;
    GpuTimer cullPass;
    GpuTimer shadingPass;

    FrameStats latest() const { return latest_; }
    FrameStats &frameStats() { return latest_; }

private:
    FrameStats latest_;
    FpsMeter fpsMeter_;
    double lastFrameTime_ = 0.0;
};

#endif
