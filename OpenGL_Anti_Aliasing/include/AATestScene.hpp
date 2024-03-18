#ifndef AA_TEST_SCENE_HPP
#define AA_TEST_SCENE_HPP

#include "AppConfig.hpp"
#include "Model.hpp"
#include "Shader.hpp"

#include <glm/glm.hpp>
#include <memory>
#include <vector>

struct SceneObject {
    glm::mat4 model;
};

// 同一展陈场景：DamagedHelmet + 地板。四种 AA 共用，保证同位置对比公平。
class AATestScene {
public:
    bool init();
    void shutdown();

    void drawOpaque(Shader &shader) const;
    void drawLines(Shader &lineShader) const;

private:
    std::unique_ptr<Model> helmet_;
    mutable Mesh floor_;
    std::vector<SceneObject> objects_;
    GLuint checkerTexture_ = 0;
};

#endif
