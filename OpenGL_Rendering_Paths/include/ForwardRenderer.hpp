#ifndef FORWARD_RENDERER_HPP
#define FORWARD_RENDERER_HPP

#include "LightManager.hpp"
#include "PerfStats.hpp"
#include "RenderTypes.hpp"
#include "Scene.hpp"
#include "Shader.hpp"

#include <memory>

// 经典前向渲染：每个 fragment 遍历全部点光源（O(objects × lights)）
class ForwardRenderer {
public:
    bool init();    // 加载 mesh.vert + forward.frag
    void shutdown();
    void render(const Scene &scene, LightManager &lights, const FrameCamera &camera, int width, int height,
                PerfStats &stats); // 上传 SSBO → 清屏 → drawFloor + drawSpotMeshes



private:
    std::unique_ptr<Shader> shader_;
};

#endif
