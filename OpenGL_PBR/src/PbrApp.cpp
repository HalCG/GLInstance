#include "PbrApp.hpp"
#include <stb_image.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <ImGuiCnFont.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

// 辅助函数：跨平台定位资源相对路径
static std::string resolveResourcePath(const std::string& path) {
    if (fs::exists(path)) return path;
    if (fs::exists("resources/" + path)) return "resources/" + path;
    if (fs::exists("./resources/" + path)) return "./resources/" + path;
    if (fs::exists("../resources/" + path)) return "../resources/" + path;
    if (fs::exists("../../resources/" + path)) return "../../resources/" + path;
    return path; // 回退默认路径
}

// 辅助函数：创建 1x1 回退单色纯色 2D 纹理 (防止纹理缺失崩溃)
static GLuint createDummyTexture(unsigned char r = 0, unsigned char g = 0, unsigned char b = 0, unsigned char a = 255) {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    unsigned char pixel[] = { r, g, b, a };
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return tex;
}

// 辅助函数：加载 2D 材质贴图 (sRGB 参数决定是否进行硬件 sRGB 解码，非线性颜色贴图如 Albedo 应传 true)
static GLuint load2DTexture(const std::string& path, bool sRGB = false) {
    std::string actualPath = resolveResourcePath(path);
    int w, h, nrChannels;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* data = stbi_load(actualPath.c_str(), &w, &h, &nrChannels, 0);
    if (!data) {
        std::cerr << "[PbrApp] Warning: Failed to load texture: " << actualPath << " (Using dummy fallback)" << std::endl;
        return createDummyTexture(255, 255, 255, 255);
    }

    GLenum internalFormat = GL_RGB;
    GLenum dataFormat = GL_RGB;
    if (nrChannels == 1) {
        internalFormat = dataFormat = GL_RED;
    } else if (nrChannels == 3) {
        internalFormat = sRGB ? GL_SRGB : GL_RGB;
        dataFormat = GL_RGB;
    } else if (nrChannels == 4) {
        internalFormat = sRGB ? GL_SRGB_ALPHA : GL_RGBA;
        dataFormat = GL_RGBA;
    }

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, w, h, 0, dataFormat, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    stbi_image_free(data);
    std::cout << "[PbrApp] Successfully loaded texture: " << actualPath << std::endl;
    return tex;
}

PbrApp::PbrApp(int width, int height, const std::string& title)
    : width_(width), height_(height), title_(title), camera_(glm::vec3(0.0f, 0.0f, 0.0f), 5.0f) {
}

PbrApp::~PbrApp() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (helmetAlbedo_) glDeleteTextures(1, &helmetAlbedo_);
    if (helmetNormal_) glDeleteTextures(1, &helmetNormal_);
    if (helmetEmissive_) glDeleteTextures(1, &helmetEmissive_);
    if (helmetAO_) glDeleteTextures(1, &helmetAO_);
    if (helmetMetallic_) glDeleteTextures(1, &helmetMetallic_);
    if (helmetRoughness_) glDeleteTextures(1, &helmetRoughness_);

    if (gunAlbedo_) glDeleteTextures(1, &gunAlbedo_);
    if (gunNormal_) glDeleteTextures(1, &gunNormal_);
    if (gunEmissive_) glDeleteTextures(1, &gunEmissive_);
    if (gunAO_) glDeleteTextures(1, &gunAO_);
    if (gunMetallic_) glDeleteTextures(1, &gunMetallic_);
    if (gunRoughness_) glDeleteTextures(1, &gunRoughness_);

    if (dummyBlackTex_) glDeleteTextures(1, &dummyBlackTex_);

    if (envCubemap_) glDeleteTextures(1, &envCubemap_);
    if (irradianceMap_) glDeleteTextures(1, &irradianceMap_);
    if (prefilterMap_) glDeleteTextures(1, &prefilterMap_);
    if (brdfLUT_) glDeleteTextures(1, &brdfLUT_);

    if (window_) {
        glfwDestroyWindow(window_);
        glfwTerminate();
    }
}

bool PbrApp::init() {
    // 1. 初始化 GLFW
    if (!glfwInit()) {
        std::cerr << "[PbrApp] Failed to initialize GLFW!" << std::endl;
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window_ = glfwCreateWindow(width_, height_, title_.c_str(), nullptr, nullptr);
    if (!window_) {
        std::cerr << "[PbrApp] Failed to create GLFW window!" << std::endl;
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window_);
    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, framebufferSizeCallback);
    glfwSetCursorPosCallback(window_, mouseCallback);
    glfwSetScrollCallback(window_, scrollCallback);

    // 2. 初始化 GLAD 函数指针
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "[PbrApp] Failed to initialize GLAD!" << std::endl;
        return false;
    }

    // 3. 配置全局 OpenGL 渲染状态
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS); // 开启立方体贴图跨面无缝滤波插值，防止接缝硬边

    // 4. 初始化 ImGui 上下文
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGuiLoadChineseFont(18.0f);
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 430");

    // 5. 初始化离屏 HDR 帧缓冲区 (GL_RGBA16F 浮点附件)
    if (!hdrFbo_.init(width_, height_)) {
        std::cerr << "[PbrApp] Failed to initialize HDR Framebuffer!" << std::endl;
        return false;
    }

    std::string shaderDir = resolveResourcePath("shaders");

    // 6. 加载并编译主场景 Shader
    pbrMeshShader_ = std::make_unique<Shader>();
    if (!pbrMeshShader_->loadFromFiles(shaderDir + "/pbr_mesh.vert", shaderDir + "/pbr_mesh.frag")) return false;

    pbrSphereShader_ = std::make_unique<Shader>();
    if (!pbrSphereShader_->loadFromFiles(shaderDir + "/pbr_sphere.vert", shaderDir + "/pbr_sphere.frag")) return false;

    skyboxShader_ = std::make_unique<Shader>();
    if (!skyboxShader_->loadFromFiles(shaderDir + "/skybox.vert", shaderDir + "/skybox.frag")) return false;

    toneMappingShader_ = std::make_unique<Shader>();
    if (!toneMappingShader_->loadFromFiles(shaderDir + "/tone_mapping.vert", shaderDir + "/tone_mapping.frag")) return false;

    // 7. 执行 IBL 预计算离屏 Pass (生成 3D Cubemap、Irradiance Map、Prefilter Map、BRDF LUT)
    if (!iblPipeline_.init(shaderDir)) {
        std::cerr << "[PbrApp] Failed to initialize IBL pipeline shaders!" << std::endl;
        return false;
    }

    std::string hdrPath = resolveResourcePath("textures/1.hdr");
    GLuint hdrTex = iblPipeline_.loadHdrTexture(hdrPath);
    if (!hdrTex) {
        std::cerr << "[PbrApp] Failed to load HDR environment texture: " << hdrPath << std::endl;
        return false;
    }

    std::cout << "[PbrApp] Computing IBL maps..." << std::endl;
    envCubemap_ = iblPipeline_.convertEquirectangularToCubemap(hdrTex, 512);
    glDeleteTextures(1, &hdrTex); // 转换完毕后删除原始 2D HDR 纹理

    irradianceMap_ = iblPipeline_.computeIrradianceCubemap(envCubemap_, 32);
    prefilterMap_ = iblPipeline_.computePrefilterCubemap(envCubemap_, 128);
    brdfLUT_ = iblPipeline_.computeBrdfLutTexture(512);

    // 8. 加载 3D 模型 (.staticmesh 顶点二进制格式)
    helmetMesh_ = std::make_unique<StaticMesh>();
    helmetMesh_->loadFromFile2(resolveResourcePath("models/DamagedHelmet.staticmesh"));

    sphereMesh_ = std::make_unique<StaticMesh>();
    sphereMesh_->loadFromFile(resolveResourcePath("models/Sphere.staticmesh"));

    gunMesh_ = std::make_unique<StaticMesh>();
    gunMesh_->loadFromFile2(resolveResourcePath("models/Gun.staticmesh"));

    dummyBlackTex_ = createDummyTexture(0, 0, 0, 255);

    // 9. 加载模型 PBR 材质贴图 (Albedo, Normal, Emissive, AO, Metallic, Roughness)
    helmetAlbedo_    = load2DTexture("textures/DamagedHelmet/Albedo.jpg", true);
    helmetNormal_    = load2DTexture("textures/DamagedHelmet/Normal.jpg", false);
    helmetEmissive_  = load2DTexture("textures/DamagedHelmet/Emissive.jpg", true);
    helmetAO_        = load2DTexture("textures/DamagedHelmet/AO.jpg", false);
    helmetMetallic_  = load2DTexture("textures/DamagedHelmet/Metallic.png", false);
    helmetRoughness_ = load2DTexture("textures/DamagedHelmet/Roughness.png", false);

    gunAlbedo_       = load2DTexture("textures/Gun/albedo.png", true);
    gunNormal_       = load2DTexture("textures/Gun/normal.png", false);
    gunEmissive_     = dummyBlackTex_;
    gunAO_           = load2DTexture("textures/Gun/ao.png", false);
    gunMetallic_     = load2DTexture("textures/Gun/metallic.png", false);
    gunRoughness_    = load2DTexture("textures/Gun/roughness.png", false);

    std::cout << "[PbrApp] Initialization completed successfully!" << std::endl;
    return true;
}

void PbrApp::run() {
    float lastFrame = 0.0f;

    while (!glfwWindowShouldClose(window_)) {
        float currentFrame = static_cast<float>(glfwGetTime());
        float deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        processInput();
        update(deltaTime);

        // =========================================================================
        // Pass 1: 将 PBR 主场景渲染到离屏 HDR 帧缓冲区 (hdrFbo_)
        // =========================================================================
        hdrFbo_.bind();
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        renderScene();

        hdrFbo_.unbind();

        // =========================================================================
        // Pass 2: 后处理 Tone Mapping 色调映射 Pass (将 HDR 浮点缓冲转换为 LDR 输出到屏幕)
        // =========================================================================
        glViewport(0, 0, width_, height_);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        toneMappingShader_->use();
        toneMappingShader_->setInt("U_HDRBuffer", 0);
        toneMappingShader_->setFloat("exposure", exposure_);
        toneMappingShader_->setFloat("gamma", gamma_);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hdrFbo_.getColorTexture());
        IblPrecomputation::renderQuad();

        // =========================================================================
        // Pass 3: 渲染 ImGui 交互控制界面 Overlay
        // =========================================================================
        renderImGui();

        glfwSwapBuffers(window_);
        glfwPollEvents();
    }
}

void PbrApp::processInput() {
    if (glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window_, true);
}

void PbrApp::update(float deltaTime) {
    camera_.update(deltaTime);
}

void PbrApp::renderScene() {
    glm::mat4 projection = glm::perspective(glm::radians(camera_.getFov()), (float)width_ / (float)height_, 0.1f, 100.0f);
    glm::mat4 view = camera_.getViewMatrix();

    // 渲染模型分流 1: DamagedHelmet 或 Cerberus Gun
    if (currentModel_ == RenderModel::DamagedHelmet || currentModel_ == RenderModel::Gun) {
        pbrMeshShader_->use();
        pbrMeshShader_->setMat4("P", projection);
        pbrMeshShader_->setMat4("V", view);
        pbrMeshShader_->setVec3("cameraPos", camera_.getPosition());

        pbrMeshShader_->setFloat("iblDiffuseIntensity", iblIntensity_);
        pbrMeshShader_->setFloat("iblSpecularIntensity", iblIntensity_);

        // 统一绑定 IBL 预计算纹理 Unit (Slot 0 ~ 2)
        pbrMeshShader_->setInt("U_PrefilteredColor", 0);
        pbrMeshShader_->setInt("U_DiffuseIrradiance", 1);
        pbrMeshShader_->setInt("U_BRDFLUT", 2);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, prefilterMap_);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_CUBE_MAP, irradianceMap_);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, brdfLUT_);

        // 统一绑定 PBR 材质贴图 Unit (Slot 3 ~ 8)
        pbrMeshShader_->setInt("U_Albedo", 3);
        pbrMeshShader_->setInt("U_Normal", 4);
        pbrMeshShader_->setInt("U_Emissive", 5);
        pbrMeshShader_->setInt("U_AO", 6);
        pbrMeshShader_->setInt("U_Metallic", 7);
        pbrMeshShader_->setInt("U_Roughness", 8);

        if (currentModel_ == RenderModel::DamagedHelmet) {
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::rotate(model, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
            pbrMeshShader_->setMat4("M", model);

            glm::mat4 itm = glm::transpose(glm::inverse(model));
            pbrMeshShader_->setMat4("ITM", itm);

            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, helmetAlbedo_);
            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D, helmetNormal_);
            glActiveTexture(GL_TEXTURE5);
            glBindTexture(GL_TEXTURE_2D, helmetEmissive_);
            glActiveTexture(GL_TEXTURE6);
            glBindTexture(GL_TEXTURE_2D, helmetAO_);
            glActiveTexture(GL_TEXTURE7);
            glBindTexture(GL_TEXTURE_2D, helmetMetallic_);
            glActiveTexture(GL_TEXTURE8);
            glBindTexture(GL_TEXTURE_2D, helmetRoughness_);

            helmetMesh_->draw();
        } else if (currentModel_ == RenderModel::Gun) {
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::scale(model, glm::vec3(0.1f));
            model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            pbrMeshShader_->setMat4("M", model);

            glm::mat4 itm = glm::transpose(glm::inverse(model));
            pbrMeshShader_->setMat4("ITM", itm);

            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, gunAlbedo_);
            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D, gunNormal_);
            glActiveTexture(GL_TEXTURE5);
            glBindTexture(GL_TEXTURE_2D, gunEmissive_);
            glActiveTexture(GL_TEXTURE6);
            glBindTexture(GL_TEXTURE_2D, gunAO_);
            glActiveTexture(GL_TEXTURE7);
            glBindTexture(GL_TEXTURE_2D, gunMetallic_);
            glActiveTexture(GL_TEXTURE8);
            glBindTexture(GL_TEXTURE_2D, gunRoughness_);

            gunMesh_->draw();
        }
    } 
    // 渲染模型分流 2: 7x7 球体测试阵列 (SphereGrid)
    else if (currentModel_ == RenderModel::SphereGrid) {
        pbrSphereShader_->use();
        pbrSphereShader_->setMat4("P", projection);
        pbrSphereShader_->setMat4("V", view);
        pbrSphereShader_->setVec3("cameraPos", camera_.getPosition());

        pbrSphereShader_->setInt("U_PrefilteredColor", 0);
        pbrSphereShader_->setInt("U_DiffuseIrradiance", 1);
        pbrSphereShader_->setInt("U_BRDFLUT", 2);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, prefilterMap_);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_CUBE_MAP, irradianceMap_);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, brdfLUT_);

        pbrSphereShader_->setFloat("iblDiffuseIntensity", iblIntensity_);
        pbrSphereShader_->setFloat("iblSpecularIntensity", iblIntensity_);

        if (useSphereGrid_) {
            int nrRows = 7;
            int nrColumns = 7;
            float spacing = 2.2f;

            // 7x7 矩阵循环：行对应 Roughness 变化，列对应 Metallic 变化
            for (int row = 0; row < nrRows; ++row) {
                float roughness = (float)row / (float)nrRows;
                roughness = glm::clamp(roughness, 0.05f, 1.0f);
                pbrSphereShader_->setFloat("roughness", roughness);

                for (int col = 0; col < nrColumns; ++col) {
                    float metallic = (float)col / (float)nrColumns;
                    pbrSphereShader_->setFloat("metallic", metallic);
                    pbrSphereShader_->setVec3("albedo", sphereAlbedo_);

                    glm::mat4 model = glm::mat4(1.0f);
                    model = glm::translate(model, glm::vec3(
                        (col - (nrColumns / 2)) * spacing,
                        (row - (nrRows / 2)) * spacing,
                        0.0f
                    ));
                    pbrSphereShader_->setMat4("M", model);

                    glm::mat4 itm = glm::transpose(glm::inverse(model));
                    pbrSphereShader_->setMat4("ITM", itm);

                    sphereMesh_->draw();
                }
            }
        } else {
            pbrSphereShader_->setVec3("albedo", sphereAlbedo_);
            pbrSphereShader_->setFloat("metallic", sphereMetallic_);
            pbrSphereShader_->setFloat("roughness", sphereRoughness_);

            glm::mat4 model = glm::mat4(1.0f);
            pbrSphereShader_->setMat4("M", model);

            glm::mat4 itm = glm::transpose(glm::inverse(model));
            pbrSphereShader_->setMat4("ITM", itm);

            sphereMesh_->draw();
        }
    }

    // 渲染 Pass 分流 3: 绘制背景天空盒 (Skybox)
    if (renderSkybox_) {
        glDepthMask(GL_FALSE);         // 禁用深度写入，防止遮挡后续屏幕绘制
        glDepthFunc(GL_LEQUAL);        // 设置深度函数为 LEQUAL (配合 pos.xyww 背景深度 1.0)
        skyboxShader_->use();
        skyboxShader_->setMat4("view", view);
        skyboxShader_->setMat4("projection", projection);
        skyboxShader_->setInt("U_SkyBox", 0);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap_);

        IblPrecomputation::renderCube();
        glDepthMask(GL_TRUE);          // 恢复深度写入
        glDepthFunc(GL_LESS);          // 恢复默认 LESS 深度测试
    }
}

void PbrApp::renderImGui() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340, 520), ImGuiCond_FirstUseEver);

    ImGui::Begin("OpenGL PBR Controls");

    ImGui::Text("FPS: %.1f (%.3f ms/frame)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
    ImGui::Separator();

    ImGui::Text("Model Selection:");
    int modelIdx = static_cast<int>(currentModel_);
    if (ImGui::RadioButton("Damaged Helmet", &modelIdx, 0)) currentModel_ = RenderModel::DamagedHelmet;
    ImGui::SameLine();
    if (ImGui::RadioButton("Sphere Grid", &modelIdx, 1)) currentModel_ = RenderModel::SphereGrid;
    ImGui::SameLine();
    if (ImGui::RadioButton("Cerberus Gun", &modelIdx, 2)) currentModel_ = RenderModel::Gun;

    ImGui::Separator();

    if (currentModel_ == RenderModel::SphereGrid) {
        if (ImGui::CollapsingHeader("Sphere Material Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Render 7x7 Grid", &useSphereGrid_);
            ImGui::ColorEdit3("Albedo Color", glm::value_ptr(sphereAlbedo_));
            if (!useSphereGrid_) {
                ImGui::SliderFloat("Metallic", &sphereMetallic_, 0.0f, 1.0f);
                ImGui::SliderFloat("Roughness", &sphereRoughness_, 0.05f, 1.0f);
            }
            ImGui::SliderFloat("AO", &sphereAO_, 0.0f, 1.0f);
        }
    }

    if (ImGui::CollapsingHeader("Lighting & Tone Mapping", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("IBL Intensity", &iblIntensity_, 0.0f, 5.0f);
        ImGui::SliderFloat("Exposure", &exposure_, 0.1f, 10.0f);
        ImGui::SliderFloat("Gamma", &gamma_, 1.0f, 3.0f);
        ImGui::Checkbox("Render Skybox", &renderSkybox_);

        const char* toneMappingNames[] = { "Reinhard", "ACES", "Uncharted2", "None" };
        ImGui::Combo("Tone Mapper", &toneMappingMode_, toneMappingNames, IM_ARRAYSIZE(toneMappingNames));
    }

    if (ImGui::CollapsingHeader("Camera Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* cameraModeNames[] = { "Trackball (360 Free Arcball)", "Turntable (Y-Up Orbit)" };
        int currentCamMode = static_cast<int>(camera_.Mode);
        if (ImGui::Combo("Camera Mode", &currentCamMode, cameraModeNames, IM_ARRAYSIZE(cameraModeNames))) {
            camera_.Mode = static_cast<CameraMode>(currentCamMode);
            if (camera_.Mode == CameraMode::Trackball) {
                camera_.syncQuaternionFromEuler();
            }
        }
        ImGui::Checkbox("Auto Rotate Model", &camera_.AutoRotate);
        ImGui::SliderFloat("Camera Distance", &camera_.Distance, 0.5f, 30.0f);
        float fov = camera_.getFov();
        if (ImGui::SliderFloat("FOV", &fov, 10.0f, 90.0f)) {
            camera_.setFov(fov);
        }
        glm::vec3 pos = camera_.getPosition();
        ImGui::Text("Camera Pos: (%.2f, %.2f, %.2f)", pos.x, pos.y, pos.z);
        if (ImGui::Button("Reset Camera")) {
            camera_.reset();
        }
    }

    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void PbrApp::framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    auto app = reinterpret_cast<PbrApp*>(glfwGetWindowUserPointer(window));
    if (app && width > 0 && height > 0) {
        app->width_ = width;
        app->height_ = height;
        app->hdrFbo_.resize(width, height);
    }
}

void PbrApp::mouseCallback(GLFWwindow* window, double xpos, double ypos) {
    auto app = reinterpret_cast<PbrApp*>(glfwGetWindowUserPointer(window));
    if (!app) return;

    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;

    bool leftPressed = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
    bool rightPressed = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);
    bool shiftPressed = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);

    if (leftPressed || rightPressed) {
        if (app->firstMouse_) {
            app->lastX_ = xpos;
            app->lastY_ = ypos;
            app->firstMouse_ = false;
        }

        float xoffset = static_cast<float>(xpos - app->lastX_);
        float yoffset = static_cast<float>(app->lastY_ - ypos);

        app->lastX_ = xpos;
        app->lastY_ = ypos;

        if (rightPressed || (leftPressed && shiftPressed)) {
            app->camera_.processPan(xoffset, yoffset);
        } else if (leftPressed) {
            app->camera_.processMouseMovement(xoffset, yoffset);
        }
    } else {
        app->firstMouse_ = true;
    }
}

void PbrApp::scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    auto app = reinterpret_cast<PbrApp*>(glfwGetWindowUserPointer(window));
    if (!app) return;

    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;

    app->camera_.processMouseScroll(static_cast<float>(yoffset));
}

