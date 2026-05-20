#ifndef SCENE_RENDERER_HPP
#define SCENE_RENDERER_HPP

#include "AATestScene.hpp"
#include "RenderTypes.hpp"
#include "Shader.hpp"

#include <memory>

// AA 测试场景渲染器：只画不透明几何（头盔 + 棋盘地板），四种 AA 共用同一绘制路径
class SceneRenderer {
public:
    bool init(); // 加载 scene.vert / scene.frag
    void shutdown();
    void render(AATestScene &scene, const FrameCamera &camera); // 绑定 VP/灯光并 drawOpaque

private:
    std::unique_ptr<Shader> sceneShader_;
};

#endif
