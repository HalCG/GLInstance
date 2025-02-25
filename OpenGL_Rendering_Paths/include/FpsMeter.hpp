#ifndef FPS_METER_HPP
#define FPS_METER_HPP

// 固定 1.0s 时间窗平均帧率（与 Vulkan Demo 口径一致；勿用 10s，交互对比太钝）

#include <algorithm>

class FpsMeter {
public:
    static constexpr double kWindowSec = 1.0;

    void tick(double nowSeconds, float frameMs) {
        if (windowStart_ <= 0.0) {
            windowStart_ = nowSeconds;
        }
        ++frames_;
        if (frameMs > maxFrameMs_) {
            maxFrameMs_ = frameMs;
        }

        const double dt = nowSeconds - windowStart_;
        if (dt >= kWindowSec && frames_ > 0) {
            avgFps_ = static_cast<float>(static_cast<double>(frames_) / dt);
            avgMs_ = static_cast<float>((dt * 1000.0) / static_cast<double>(frames_));
            minFps_ = maxFrameMs_ > 0.0f ? (1000.0f / maxFrameMs_) : avgFps_;
            frames_ = 0;
            maxFrameMs_ = 0.0f;
            windowStart_ = nowSeconds;
        }
    }

    float avgFps() const { return avgFps_; }
    float avgMs() const { return avgMs_; }
    float minFps() const { return minFps_; }

private:
    double windowStart_ = 0.0;
    int frames_ = 0;
    float maxFrameMs_ = 0.0f;
    float avgFps_ = 0.0f;
    float avgMs_ = 0.0f;
    float minFps_ = 0.0f;
};

#endif
