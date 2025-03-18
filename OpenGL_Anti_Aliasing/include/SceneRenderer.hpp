#ifndef SCENE_RENDERER_HPP
#define SCENE_RENDERER_HPP

#include "AATestScene.hpp"
#include "RenderTypes.hpp"
#include "Shader.hpp"

// AA 测试场景渲染器：不透明几何 + 细线/网格（易暴露锯齿）
class SceneRenderer {
public:
    bool init();    // 加载 scene/line shader
    void shutdown();
    void render(AATestScene &scene, const FrameCamera &camera); // 绑定 VP 并 drawOpaque + drawLines

private:
    std::unique_ptr<Shader> sceneShader_;
    std::unique_ptr<Shader> lineShader_;
};

#endif
