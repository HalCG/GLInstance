#ifndef PERF_STATS_HPP
#define PERF_STATS_HPP

#include "FpsMeter.hpp"
#include "RenderTypes.hpp"

#include <glad/glad.h>

// 单 pass GPU 耗时查询（GL_TIME_ELAPSED，双缓冲 query 避免 stall）
class GpuTimer {
public:
    void init();    // 创建 query 对象
    void shutdown();
    void begin();   // glBeginQuery
    float endMs();  // glEndQuery 并读取上一帧结果（毫秒）
    void setEnabled(bool enabled) { enabled_ = enabled; } // 拖拽时可关闭以减轻开销

private:
    static constexpr int kQueryCount = 2;
    GLuint queries_[kQueryCount] = {0, 0};
    int writeIndex_ = 0;
    bool active_ = false;
    bool enabled_ = true;
    float lastMs_ = 0.0f;
};

// 帧级 CPU/GPU 统计：scene/post pass 计时与平滑 FPS
class PerfStats {
public:
    void beginFrame(); // 记录帧起始时间
    void endFrame();   // 计算 totalFrameMs 与平滑 fps

    GpuTimer scenePass;
    GpuTimer postPass;

    FrameStats latest() const { return latest_; }
    FrameStats &frameStats() { return latest_; }

private:
    FrameStats latest_;
    FpsMeter fpsMeter_;
    double lastFrameTime_ = 0.0;
};

#endif
