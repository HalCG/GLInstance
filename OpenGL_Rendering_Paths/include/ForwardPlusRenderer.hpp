#ifndef FORWARD_PLUS_RENDERER_HPP
#define FORWARD_PLUS_RENDERER_HPP

#include "LightManager.hpp"
#include "PerfStats.hpp"
#include "RenderTypes.hpp"
#include "Scene.hpp"
#include "Shader.hpp"

#include <memory>

// Forward+：CPU tile light culling + GPU 按 tile 读 SSBO 做前向 shading
class ForwardPlusRenderer {
public:
    bool init();    // 加载 mesh.vert + forward_plus.frag
    void shutdown();
    void render(const Scene &scene, LightManager &lights, const FrameCamera &camera, int width, int height,
                PerfStats &stats); // cullPass → shadingPass



private:
    std::unique_ptr<Shader> shader_;
};

#endif
