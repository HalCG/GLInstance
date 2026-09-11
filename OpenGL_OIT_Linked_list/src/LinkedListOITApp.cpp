/**
 * @file LinkedListOITApp.cpp
 * @brief OIT Linked List 透明渲染应用实现 (逐像素链表顺序无关透明度)
 *
 * =================================================================================
 * 【OpenGL 状态机与逐像素链表 OIT 架构核心概念差异说明】
 * 1. 传统 Alpha 混合 vs 逐像素链表 (Per-Pixel Linked List OIT):
 *    - 传统 Alpha 混合必须要求 CPU 侧对场景中所有半透明 Object 按 View Space 距离从远到近 (Back-to-Front) 排序，
 *      且无法解决物体交叉、自相交或封闭体前背面交叠问题。
 *    - Linked List OIT 在 GPU Fragment Shader 中利用硬件原子操作与 2D Image / SSBO 数据结构：
 *      a) 屏幕空间每个像素 (x, y) 对应一个头指针 (Head Pointer Texture)，指向链表最新插入的节点 Index。
 *      b) 节点数据（颜色、深度、next指针）统一存储在一个庞大的线性 SSBO (Shader Storage Buffer Object) 中。
 *      c) 全局单调递增的 Atomic Counter Buffer 赋予每个新生成的 Fragment 唯一的全局节点 Index。
 *
 * 2. 状态机关联与资源绑定:
 *    - Head Pointer Image: 使用 glBindImageTexture 绑定到 Image Unit 0 (GL_R32UI 格式)，支持跨线程原子无锁交换 (imageAtomicExchange)。
 *    - ListNode SSBO: 使用 glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ...) 绑定到 Block Index 0。
 *    - Atomic Counter: 使用 glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 0, ...) 绑定到 Atomic Counter 0。
 *    - PBO 快速重置: 利用 GL_PIXEL_UNPACK_BUFFER + glTexSubImage2D 将 0xFFFFFFFF (空指针 NULL) 极速写入头指针纹理，避免 CPU 逐像素循环开销。
 *    - 显存屏障 (glMemoryBarrier): 在 Pass 2 (片段收集) 结束与 Pass 3 (全屏混合) 开始之间，必须显式插入
 *      GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT | GL_ATOMIC_COUNTER_BARRIER_BIT，
 *      防止 Pass 3 读取 Head Pointer / SSBO 时出现 GPU L2/Cache 未刷新导致的 Read-After-Write 乱序竞争！
 *
 * 3. 三 Pass 渲染流程概览：
 *    - Pass 1: 不透明物体 Pass → 渲染到 opaqueFBO (写入 Color + Depth)。
 *    - Pass 2: 透明物体片段收集 Pass → 开启深度测试（与 opaqueFBO 共享 Depth Attachment），关闭深度写入 (glDepthMask(GL_FALSE))。
 *              Fragment Shader 手动与 opaqueFBO 的 Depth 进行深度裁剪；若通过，则通过原子操作头插法连入 SSBO 链表。
 *    - Pass 3: 全屏合成 Pass → 内存屏障同步；调用 compositeShader 对全屏 Quad 逐像素读取 SSBO 链表片段，
 *              在 Fragment Shader 内部局部数组内进行插入排序 (Back-to-Front)，最后完成 Over 运算并输出到 Default Framebuffer。
 * =================================================================================
 */

#include "LinkedListOITApp.hpp"

#include <cstdio>
#include <glad/glad.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/trigonometric.hpp>
#include <iostream>

// ---- 静态成员 ----
LinkedListOITApp *LinkedListOITApp::s_instance_ = nullptr;

// ---- 链表节点结构体（与 oitRender.frag / composite.frag 中的 struct NodeType 严格对齐） ----
// 内存对齐说明 (std430 布局规则):
// vec4  color (16 bytes, align 16)
// float depth (4 bytes, align 4)
// uint  next  (4 bytes, align 4)
// struct 总大小为 24 bytes (16 + 4 + 4 + 4 填充)
struct ListNode {
    glm::vec4 color;
    GLfloat depth;
    GLuint next;
};

// ===================================================================
// 公开接口
// ===================================================================

bool LinkedListOITApp::init() {
    s_instance_ = this;

    if (!initWindow())
        return false;
    if (!initShaders())
        return false;
    if (!initScene())
        return false;
    if (!initOITBuffers())
        return false;
    if (!initFramebuffers())
        return false;

    return true;
}

void LinkedListOITApp::run() {
    glViewport(0, 0, width_, height_);
    glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    while (!glfwWindowShouldClose(window_)) {
        viewRotate_ += 1.0f;
        processInput(window_);

        // 执行 3 Pass 逐像素链表 OIT 渲染管线
        renderOpaquePass();
        renderTransparentPass();
        renderCompositePass();

        glfwSwapBuffers(window_);
        glfwPollEvents();
    }
}

void LinkedListOITApp::shutdown() {
    glfwTerminate();
}

// ===================================================================
// GLFW 回调（静态）
// ===================================================================

void LinkedListOITApp::framebufferSizeCallback(GLFWwindow * /*window*/, int width, int height) {
    if (s_instance_) {
        s_instance_->width_ = width;
        s_instance_->height_ = height;
    }
    glViewport(0, 0, width, height);
}

void LinkedListOITApp::processInput(GLFWwindow *window) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    } else if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
        if (s_instance_)
            s_instance_->viewRotate_ += 1.0f;
    } else if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
        if (s_instance_)
            s_instance_->viewRotate_ -= 1.0f;
    }
    if (s_instance_)
        printf("view_rotate:%f\n", s_instance_->viewRotate_);
}

// ===================================================================
// 初始化
// ===================================================================

bool LinkedListOITApp::initWindow() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3); // OIT Linked List 需要 SSBO 和 Image Load/Store 支持 (OpenGL 4.3+)
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    window_ = glfwCreateWindow(width_, height_, AppConfig::kWindowTitle, nullptr, nullptr);
    if (!window_) {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window_);
    glfwSetFramebufferSizeCallback(window_, framebufferSizeCallback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return false;
    }

    // 默认开启深度测试，设置 LEQUAL 比较函数
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_MULTISAMPLE);

    return true;
}

bool LinkedListOITApp::initShaders() {
    blinnPhongShader_ = std::make_unique<Shader>(
        AppConfig::resourcePath("blinnPhong.vert").c_str(),
        AppConfig::resourcePath("blinnPhong.frag").c_str());
    oitRenderShader_ = std::make_unique<Shader>(
        AppConfig::resourcePath("oitRender.vert").c_str(),
        AppConfig::resourcePath("oitRender.frag").c_str());
    compositeShader_ = std::make_unique<Shader>(
        AppConfig::resourcePath("composite.vert").c_str(),
        AppConfig::resourcePath("composite.frag").c_str());
    quadShader_ = std::make_unique<Shader>(
        AppConfig::resourcePath("quad.vert").c_str(),
        AppConfig::resourcePath("quad.frag").c_str());
    return true;
}

bool LinkedListOITApp::initScene() {
    quad_ = std::make_unique<Model>(AppConfig::resourcePath("models/quad/quad.obj"));
    spot_ = std::make_unique<Model>(AppConfig::resourcePath("models/spot/spot.obj"));

    textureWindowR_ = std::make_unique<Texture>(AppConfig::resourcePath("models/quad/window-r.png"));
    textureWindowG_ = std::make_unique<Texture>(AppConfig::resourcePath("models/quad/window-g.png"));
    textureWindowB_ = std::make_unique<Texture>(AppConfig::resourcePath("models/quad/window-b.png"));
    textureSpot_ = std::make_unique<Texture>(AppConfig::resourcePath("models/spot/spot.png"));
    return true;
}

bool LinkedListOITApp::initOITBuffers() {
    // 假设平均每个像素支持最多 20 层半透明覆盖
    maxNodes_ = width_ * height_ * 20;

    GLuint zero = 0;
    GLint nodeSize = sizeof(ListNode); // std430 对齐下每个节点占 24 字节

    // 1. 原子计数器缓冲区 (Atomic Counter Buffer)
    // 作用：记录当前已分配的节点总数，供 Fragment Shader 内部做单调自增 atomicCounterIncrement
    glGenBuffers(1, &atomicBuffer_);
    glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 0, atomicBuffer_);
    glBufferData(GL_ATOMIC_COUNTER_BUFFER, sizeof(GLuint), nullptr, GL_DYNAMIC_DRAW);
    glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), &zero);

    // 2. 链表节点存储缓冲区 (SSBO: Shader Storage Buffer Object)
    // 作用：存放大容量的无结构 Node 数组，能够被 Fragment Shader 随机读写 (Read/Write)
    glGenBuffers(1, &linkedListBuffer_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, linkedListBuffer_);
    glBufferData(GL_SHADER_STORAGE_BUFFER, maxNodes_ * nodeSize, nullptr, GL_DYNAMIC_DRAW);

    // 3. 头指针图像纹理 (Head Pointer Image Texture)
    // 格式：GL_R32UI (单通道 32 位无符号整数)，分辨率与 Viewport 对应 (width x height)
    // 作用：保存每个屏幕像素 (x, y) 处链表头节点的 Index。初始全为 0xFFFFFFFF (即 NULL)
    glGenTextures(1, &headPtrTexture_);
    glBindTexture(GL_TEXTURE_2D, headPtrTexture_);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_R32UI, width_, height_);
    // 将 Texture 的 Mip level 0 绑定到 Shader Image Unit 0，允许读写 (GL_READ_WRITE)
    glBindImageTexture(0, headPtrTexture_, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
    glBindTexture(GL_TEXTURE_2D, 0);

    // 4. PBO (Pixel Buffer Object) 重置缓冲区
    // 作用：填充全为 0xFFFFFFFF (NULL) 的内存块，通过 GPU DMA 传送到 headPtrTexture_，
    // 相比逐像素调用 glClearTexImage 或 CPU 传输，效率极大提高
    std::vector<GLuint> headPtrClearBuf(width_ * height_, 0xffffffff);
    glGenBuffers(1, &clearBuf_);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, clearBuf_);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, headPtrClearBuf.size() * sizeof(GLuint),
                 headPtrClearBuf.data(), GL_STATIC_COPY);

    // 初始重置头指针纹理
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, clearBuf_);
    glBindTexture(GL_TEXTURE_2D, headPtrTexture_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width_, height_, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    return true;
}

bool LinkedListOITApp::initFramebuffers() {
    // ---- opaqueFBO: 不透明物体 Pass 渲染目标 ----
    glGenFramebuffers(1, &opaqueFBO_);

    // 颜色附件：RGBA16F HDR 纹理
    glGenTextures(1, &opaqueTexture_);
    glBindTexture(GL_TEXTURE_2D, opaqueTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width_, height_, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    // 深度附件：32-bit 浮点深度纹理
    // 注意：此深度纹理将同时在 Pass 2 被采样 (Samplers 采样或 Depth Attachment 挂载)
    glGenTextures(1, &opaqueDepthTexture_);
    glBindTexture(GL_TEXTURE_2D, opaqueDepthTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, width_, height_, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, opaqueFBO_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, opaqueTexture_, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, opaqueDepthTexture_, 0);

    GLenum drawBuffers[] = {GL_COLOR_ATTACHMENT0};
    glDrawBuffers(1, drawBuffers);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "ERROR::FRAMEBUFFER:: Opaque framebuffer is not complete!" << std::endl;

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // ---- oitRenderFBO: 透明物体片段收集 FBO ----
    // 注意：透明 Pass 的主存储在 SSBO / Image 中，但常规管线仍挂载一个 dummy 颜色纹理与共享深度附件
    glGenTextures(1, &oitTexture_);
    glBindTexture(GL_TEXTURE_2D, oitTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width_, height_, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &oitRenderFBO_);
    glBindFramebuffer(GL_FRAMEBUFFER, oitRenderFBO_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, oitTexture_, 0);
    // 共享不透明物体的深度缓冲区，保证透明物体的深度测试依据 opaqueDepth 裁切
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, opaqueDepthTexture_, 0);

    GLenum drawBuffersOIT[] = {GL_COLOR_ATTACHMENT0};
    glDrawBuffers(1, drawBuffersOIT);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "ERROR::FRAMEBUFFER:: OIT framebuffer is not complete!" << std::endl;

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return true;
}

// ===================================================================
// 渲染 Pass 细节实现与状态机切换
// ===================================================================

void LinkedListOITApp::renderOpaquePass() {
    /* Pass 1: 不透明物体渲染
       - 渲染模型: spot (小牛)
       - 着色器: blinnPhongShader
       - 状态配置: 开启深度测试 (GL_LESS), 开启深度写入 (glDepthMask(GL_TRUE)), 关闭背面剔除 (GL_DISABLE(GL_CULL_FACE))
       - 渲染输出: opaqueFBO (opaqueTexture + opaqueDepthTexture)
    */
    glBindFramebuffer(GL_FRAMEBUFFER, opaqueFBO_);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_TRUE); // 写入深度到 opaqueDepthTexture

    blinnPhongShader_->use();
    blinnPhongShader_->setVec3("cameraPos", cameraPos_);
    blinnPhongShader_->setVec3("lightPos", lightPos_);
    blinnPhongShader_->setVec3("k", k_);

    glm::mat4 model = modelMatrix({0.0f, 0.0f, 0.0f}, 0.5f);

    glm::mat4 view = glm::lookAt(
        2.0f * glm::vec3(glm::sin(glm::radians(viewRotate_)), 0.0f, glm::cos(glm::radians(viewRotate_))),
        glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    glm::mat4 projection = glm::perspective(
        glm::radians(AppConfig::kFovDegrees), (float)(width_) / (float)(height_),
        AppConfig::kNearPlane, AppConfig::kFarPlane);

    blinnPhongShader_->setMat4("model", model);
    blinnPhongShader_->setMat4("view", view);
    blinnPhongShader_->setMat4("projection", projection);

    spot_->Draw(*blinnPhongShader_, opaqueFBO_,
                {{"diffuse_texture", textureSpot_->id}}, {}, GL_TRIANGLES, {true, true});
}

void LinkedListOITApp::renderTransparentPass() {
    /* Pass 2: 透明物体片段收集 Pass (逐像素链表构建)
       - 渲染模型: 3 个重叠的彩色透明 Quad 面片 (红/绿/蓝)
       - 着色器: oitRenderShader
       - 状态配置: 开启深度测试 (GL_LESS), 关闭深度写入 (glDepthMask(GL_FALSE))
                   原因：
                   1. 开启 Z-Test：利用不透明 Pass 留下来的 Depth 缓冲剔除被实体挡住的无效透明面片（避免无意义插入链表）；
                   2. 禁用 Z-Write：防止透明面片互相遮挡/污染 Z-Buffer，确保相机与不透明物体之间的所有透明面片都能成功推入 SSBO 链表！
       - OIT 状态准备:
         a) 重置 Atomic Counter = 0 (glBufferSubData)
         b) 重置 Head Pointer Texture 全为 0xFFFFFFFF (PBO + glTexSubImage2D)
         c) 绑定 Image Unit 0 (headPtrTexture_), SSBO 0 (linkedListBuffer_), Atomic 0 (atomicBuffer_)
    */
    glBindFramebuffer(GL_FRAMEBUFFER, oitRenderFBO_);
    // 注意：不清除 Color，深度也不擦除

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE); // 关键！只读不透明 Pass 的 Z-Buffer 剔除背部遮挡片元，但禁止写入深度以收集所有透明层

    // 1. 重置原子计数器
    GLuint zero = 0;
    glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 0, atomicBuffer_);
    glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), &zero);

    // 2. 绑定 SSBO 节点链表
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, linkedListBuffer_);

    // 3. 重新建立头指针 Image 绑定与 PBO 快速清空
    glBindImageTexture(0, headPtrTexture_, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, clearBuf_);
    glBindTexture(GL_TEXTURE_2D, headPtrTexture_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width_, height_, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    oitRenderShader_->use();
    oitRenderShader_->setVec3("cameraPos", cameraPos_);
    oitRenderShader_->setVec3("lightPos", lightPos_);
    oitRenderShader_->setVec3("k", k_);
    oitRenderShader_->setUint("MaxNodes", maxNodes_);

    glm::mat4 view = glm::lookAt(
        2.0f * glm::vec3(glm::sin(glm::radians(viewRotate_)), 0.0f, glm::cos(glm::radians(viewRotate_))),
        glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    glm::mat4 projection = glm::perspective(
        glm::radians(AppConfig::kFovDegrees), (float)(width_) / (float)(height_),
        AppConfig::kNearPlane, AppConfig::kFarPlane);

    oitRenderShader_->setMat4("view", view);
    oitRenderShader_->setMat4("projection", projection);

    // 红色透明方块
    glm::mat4 model = modelMatrix({-0.5f, 0.0f, 0.8f}, 0.5f);
    oitRenderShader_->setMat4("model", model);
    quad_->Draw(*oitRenderShader_, oitRenderFBO_,
                {{"diffuse_texture", textureWindowR_->id}, {"texture_depth", opaqueDepthTexture_}},
                {}, GL_TRIANGLES, {true, false});

    // 绿色透明方块
    model = modelMatrix({0.2f, -0.5f, -1.0f}, 0.5f);
    oitRenderShader_->setMat4("model", model);
    quad_->Draw(*oitRenderShader_, oitRenderFBO_,
                {{"diffuse_texture", textureWindowG_->id}, {"texture_depth", opaqueDepthTexture_}},
                {}, GL_TRIANGLES, {false, false});

    // 蓝色透明方块
    model = modelMatrix({0.2f, 0.0f, -0.5f}, 0.5f);
    oitRenderShader_->setMat4("model", model);
    quad_->Draw(*oitRenderShader_, oitRenderFBO_,
                {{"diffuse_texture", textureWindowB_->id}, {"texture_depth", opaqueDepthTexture_}},
                {}, GL_TRIANGLES, {false, false});
}

void LinkedListOITApp::renderCompositePass() {
    /* Pass 3: 链表遍历、排序与 Over 混合合成 Pass
       - 渲染模型: 全屏 Quad (quad_)
       - 着色器: compositeShader
       - 状态配置: 解绑自定义 FBO，输出到默认 Framebuffer (0)
                   开启深度测试 (GL_ALWAYS / GL_LEQUAL), 开启深度写入
       - 核心硬件同步机制:
         调用 glMemoryBarrier(...)！
         由于 Pass 2 的 Fragment Shader 向 Head Pointer Image 和 SSBO 缓冲区中大量写入数据，
         这些写入可能依然缓存在 GPU 的 L1/L2 Cache 中。
         在 Pass 3 读取这些 Texture/SSBO 前，必须强制刷新 GPU Cache 屏障，
         确保 Read-After-Write (RAW) 的内存访问一致性，避免画面闪烁或图像损坏！
    */
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                    GL_SHADER_STORAGE_BARRIER_BIT |
                    GL_ATOMIC_COUNTER_BARRIER_BIT);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    compositeShader_->use();
    quad_->Draw(*compositeShader_, 0,
                {{"texture_opaque", opaqueTexture_}}, {}, GL_TRIANGLES, {true, true});

    // 清理解绑状态
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
}

// ===================================================================
// 辅助函数
// ===================================================================

glm::mat4 LinkedListOITApp::modelMatrix(const glm::vec3 &translation, float scale) const {
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, translation);
    model = glm::scale(model, glm::vec3(scale));
    return model;
}