#include "PerfStats.hpp"

#include <GLFW/glfw3.h>

void GpuTimer::init() {
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

    const int readIndex = 1 - writeIndex_;
    GLint available = 0;
    glGetQueryObjectiv(queries_[readIndex], GL_QUERY_RESULT_AVAILABLE, &available);
    if (available) {
        GLuint64 elapsed = 0;
        glGetQueryObjectui64v(queries_[readIndex], GL_QUERY_RESULT, &elapsed);
        lastMs_ = static_cast<float>(elapsed) / 1000000.0f;
    }

    writeIndex_ = readIndex;
    return lastMs_;
}

void PerfStats::beginFrame() {
    lastFrameTime_ = glfwGetTime();
}

void PerfStats::endFrame() {
    const double now = glfwGetTime();
    const float frameMs = static_cast<float>((now - lastFrameTime_) * 1000.0);
    latest_.totalFrameMs = frameMs;
    fpsMeter_.tick(now, frameMs);
    latest_.fps = fpsMeter_.avgFps();
    latest_.avgFrameMs = fpsMeter_.avgMs();
    latest_.minFps = fpsMeter_.minFps();
}
