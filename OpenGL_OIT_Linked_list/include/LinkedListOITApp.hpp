#ifndef LINKED_LIST_OIT_APP_HPP
#define LINKED_LIST_OIT_APP_HPP

#include "AppConfig.hpp"
#include "Mesh.hpp"
#include "Model.hpp"
#include "Shader.hpp"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

/**
 * OIT Linked List 透明渲染演示
 *
 * 使用基于链表(Linked List)的每像素片段存储实现 Order-Independent Transparency：
 * 1. Pass 1: 渲染不透明物体到 opaqueFBO（Blinn-Phong 光照）
 * 2. Pass 2: 渲染透明物体，片段通过 SSBO 链表存储到 oitRenderFBO
 * 3. Pass 3: 遍历链表排序混合，合成到默认帧缓冲
 */
class LinkedListOITApp {
public:
    bool init();
    void run();
    void shutdown();

    static void framebufferSizeCallback(GLFWwindow *window, int width, int height);
    static void processInput(GLFWwindow *window);

private:
    bool initWindow();
    bool initShaders();
    bool initScene();
    bool initOITBuffers();
    bool initFramebuffers();
    void destroyOITBuffers();
    void destroyFramebuffers();
    void resizeGpuResources(); // 窗口尺寸变化时重建 FBO / SSBO / headPtr

    void renderOpaquePass();
    void renderTransparentPass();
    void renderCompositePass();

    glm::mat4 modelMatrix(const glm::vec3 &translation, float scale = 0.5f) const;
    // 与 lookAt 同一套轨道眼点
    glm::vec3 orbitEye() const;

    // ---- GLFW 窗口 ----
    GLFWwindow *window_ = nullptr;

    unsigned int width_ = AppConfig::kInitialWidth;
    unsigned int height_ = AppConfig::kInitialHeight;

    // ---- 场景参数 ----
    glm::vec3 lightPos_ = AppConfig::lightPosition();
    glm::vec3 k_ = AppConfig::materialCoeffs();
    float viewRotate_ = AppConfig::kInitialOrbitAngle;

    // ---- 着色器 ----
    std::unique_ptr<Shader> blinnPhongShader_;
    std::unique_ptr<Shader> oitRenderShader_;
    std::unique_ptr<Shader> compositeShader_;

    // ---- 模型 ----
    std::unique_ptr<Model> quad_;
    std::unique_ptr<Model> spot_;

    // ---- 纹理 ----
    std::unique_ptr<Texture> textureWindowR_;
    std::unique_ptr<Texture> textureWindowG_;
    std::unique_ptr<Texture> textureWindowB_;
    std::unique_ptr<Texture> textureSpot_;

    // ---- OIT 链表缓冲区 ----
    GLuint atomicBuffer_ = 0;
    GLuint linkedListBuffer_ = 0;
    GLuint headPtrTexture_ = 0;
    GLuint clearBuf_ = 0;
    unsigned int maxNodes_ = 0;

    // ---- 帧缓冲 ----
    GLuint opaqueFBO_ = 0;
    GLuint opaqueTexture_ = 0;
    GLuint opaqueDepthTexture_ = 0;
    GLuint oitRenderFBO_ = 0;
    GLuint oitTexture_ = 0;

    // ---- 静态实例指针（用于 GLFW 回调） ----
    bool autoRotate_ = true;
    glm::vec3 bgColor_ = glm::vec3(0.2f, 0.3f, 0.3f);

    static LinkedListOITApp *s_instance_;
};

#endif