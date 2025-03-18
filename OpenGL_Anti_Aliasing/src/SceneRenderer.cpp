#include "SceneRenderer.hpp"

#include "AppConfig.hpp"

bool SceneRenderer::init() {
    const std::string sceneVert = AppConfig::shaderPath("scene.vert");
    const std::string sceneFrag = AppConfig::shaderPath("scene.frag");
    const std::string lineVert = AppConfig::shaderPath("line.vert");
    const std::string lineFrag = AppConfig::shaderPath("line.frag");
    sceneShader_ = std::make_unique<Shader>(sceneVert.c_str(), sceneFrag.c_str());
    lineShader_ = std::make_unique<Shader>(lineVert.c_str(), lineFrag.c_str());
    return sceneShader_ && lineShader_ && sceneShader_->ID != 0;
}

void SceneRenderer::shutdown() {
    sceneShader_.reset();
    lineShader_.reset();
}

void SceneRenderer::render(AATestScene &scene, const FrameCamera &camera) {
    sceneShader_->use();
    sceneShader_->setMat4("view", camera.view);
    sceneShader_->setMat4("projection", camera.projection);
    sceneShader_->setVec3("uCameraPos", camera.eye);
    sceneShader_->setVec3("uMaterialK", AppConfig::materialCoeffs());
    sceneShader_->setVec3("uLightPos0", glm::vec3(4.5f, 6.5f, 3.0f));
    sceneShader_->setVec3("uLightPos1", glm::vec3(-4.0f, 4.5f, -3.5f));
    sceneShader_->setVec3("uLightColor0", glm::vec3(1.0f, 0.96f, 0.9f));
    sceneShader_->setVec3("uLightColor1", glm::vec3(0.55f, 0.65f, 0.9f));
    scene.drawOpaque(*sceneShader_);

    // 展陈场景可无辅助线；保留接口以兼容旧测试几何
    lineShader_->use();
    lineShader_->setMat4("view", camera.view);
    lineShader_->setMat4("projection", camera.projection);
    scene.drawLines(*lineShader_);
}
