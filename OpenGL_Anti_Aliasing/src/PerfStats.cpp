#include "PerfStats.hpp"

#include <GLFW/glfw3.h>

void GpuTimer::init() {
    // 双 query：本帧写入一个，读上一帧结果，避免同步等待 GPU
    glGenQueries(kQueryCount, queries_);
}

void GpuTimer::shutdown() {
    if (queries_[0]) {
        glDeleteQueries(kQueryCount, queries_);
        queries_[0] = 0;
        queries_[1] = 0;
    }
}

void GpuTimer::begin() {
    // 拖拽窗口等场景可临时关闭，减少 query 开销
    if (!enabled_ || !queries_[0]) {
        return;
    }
    glBeginQuery(GL_TIME_ELAPSED, queries_[writeIndex_]);
    active_ = true;
}

float GpuTimer::endMs() {
    if (!enabled_ || !queries_[0] || !active_) {
        return lastMs_;
    }
    glEndQuery(GL_TIME_ELAPSED);
    active_ = false;

    // 读另一路 query（上一帧的结果）；未就绪则沿用 lastMs_，不 stall
    const int readIndex = 1 - writeIndex_;
    GLint available = 0;
    glGetQueryObjectiv(queries_[readIndex], GL_QUERY_RESULT_AVAILABLE, &available);
    if (available) {
        GLuint64 elapsed = 0; // 纳秒
        glGetQueryObjectui64v(queries_[readIndex], GL_QUERY_RESULT, &elapsed);
        lastMs_ = static_cast<float>(elapsed) / 1000000.0f;
    }

    writeIndex_ = readIndex; // 下一帧改写刚读完的那一路
    return lastMs_;
}

void PerfStats::beginFrame() {
    lastFrameTime_ = glfwGetTime(); // CPU 帧起点（秒）
}

void PerfStats::endFrame() {
    const double now = glfwGetTime();
    const float frameMs = static_cast<float>((now - lastFrameTime_) * 1000.0);
    latest_.totalFrameMs = frameMs;
    fpsMeter_.tick(now, frameMs); // 滑动窗口平滑
    latest_.fps = fpsMeter_.avgFps();
    latest_.avgFrameMs = fpsMeter_.avgMs();
    latest_.minFps = fpsMeter_.minFps();
}
