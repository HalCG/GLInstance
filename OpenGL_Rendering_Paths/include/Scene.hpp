#ifndef SCENE_HPP
#define SCENE_HPP

#include "AppConfig.hpp"
#include "Model.hpp"

#include <glm/glm.hpp>
#include <memory>
#include <vector>

struct SceneObject {
    glm::mat4 model;
};

// 渲染路径 Demo 共用场景：棋盘地板 + DamagedHelmet 展陈（无展台）
class Scene {
public:
    bool init();
    void shutdown();

    void drawSpotMeshes(Shader &shader) const; // 绘制头盔实例（方法名保留以兼容各 Renderer）
    void drawFloor(Shader &shader) const;

    const std::vector<SceneObject> &objects() const { return objects_; }
    int objectCount() const { return static_cast<int>(objects_.size()) + 1; }

private:
    std::unique_ptr<Model> heroModel_;
    mutable Mesh floor_;
    std::vector<SceneObject> objects_;
};

#endif
