#include "SceneRenderer.hpp"

#include "AppConfig.hpp"

bool SceneRenderer::init() {
    const std::string sceneVert = AppConfig::shaderPath("scene.vert");
    const std::string sceneFrag = AppConfig::shaderPath("scene.frag");
    sceneShader_ = std::make_unique<Shader>(sceneVert.c_str(), sceneFrag.c_str());
    return sceneShader_ && sceneShader_->ID != 0;
}

void SceneRenderer::shutdown() {
    sceneShader_.reset();
}

void SceneRenderer::render(AATestScene &scene, const FrameCamera &camera) {
    // projection 可能含 TAA jitter；灯光/材质各模式相同，保证对比公平
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
}
