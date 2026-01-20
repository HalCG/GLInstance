#include "IblPrecomputation.hpp"
#include <stb_image.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

// 静态成员变量初始化
GLuint IblPrecomputation::cubeVAO_ = 0;
GLuint IblPrecomputation::cubeVBO_ = 0;
GLuint IblPrecomputation::quadVAO_ = 0;
GLuint IblPrecomputation::quadVBO_ = 0;

IblPrecomputation::~IblPrecomputation() {
    if (cubeVAO_) {
        glDeleteVertexArrays(1, &cubeVAO_);
        glDeleteBuffers(1, &cubeVBO_);
        cubeVAO_ = cubeVBO_ = 0;
    }
    if (quadVAO_) {
        glDeleteVertexArrays(1, &quadVAO_);
        glDeleteBuffers(1, &quadVBO_);
        quadVAO_ = quadVBO_ = 0;
    }
}

bool IblPrecomputation::init(const std::string& shaderDir) {
    // 1. 加载等距柱状全景图转立方体贴图 Shader
    equirectShader_ = std::make_unique<Shader>();
    if (!equirectShader_->loadFromFiles(shaderDir + "/equirectangular_to_cubemap.vert", shaderDir + "/equirectangular_to_cubemap.frag")) {
        std::cerr << "[IBL] Failed to load equirectangular_to_cubemap shader!" << std::endl;
        return false;
    }

    // 2. 加载漫反射辐照度卷积 Shader
    irradianceShader_ = std::make_unique<Shader>();
    if (!irradianceShader_->loadFromFiles(shaderDir + "/irradiance_convolution.vert", shaderDir + "/irradiance_convolution.frag")) {
        std::cerr << "[IBL] Failed to load irradiance_convolution shader!" << std::endl;
        return false;
    }

    // 3. 加载镜面反射预过滤 Shader
    prefilterShader_ = std::make_unique<Shader>();
    if (!prefilterShader_->loadFromFiles(shaderDir + "/prefilter.vert", shaderDir + "/prefilter.frag")) {
        std::cerr << "[IBL] Failed to load prefilter shader!" << std::endl;
        return false;
    }

    // 4. 加载 2D BRDF LUT 查找表 Shader
    brdfShader_ = std::make_unique<Shader>();
    if (!brdfShader_->loadFromFiles(shaderDir + "/brdf.vert", shaderDir + "/brdf.frag")) {
        std::cerr << "[IBL] Failed to load brdf shader!" << std::endl;
        return false;
    }

    return true;
}

GLuint IblPrecomputation::loadHdrTexture(const std::string& path) {
    // 图像加载前翻转 Y 轴，以匹配 OpenGL UV 坐标系原点在左下角
    stbi_set_flip_vertically_on_load(true);
    int width, height, nrComponents;
    float* data = stbi_loadf(path.c_str(), &width, &height, &nrComponents, 0);
    if (!data) {
        std::cerr << "[IBL] Failed to load HDR image: " << path << std::endl;
        return 0;
    }

    GLuint hdrTexture;
    glGenTextures(1, &hdrTexture);
    glBindTexture(GL_TEXTURE_2D, hdrTexture);
    // 使用 GL_RGB16F 半精度浮点格式保存高动态范围 HDR 像素颜色
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, data);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    stbi_image_free(data);
    std::cout << "[IBL] Successfully loaded HDR texture: " << path << " (" << width << "x" << height << ")" << std::endl;
    return hdrTexture;
}

GLuint IblPrecomputation::convertEquirectangularToCubemap(GLuint hdrTex, int cubemapSize) {
    // 步骤 A: 创建离屏捕获捕获帧缓冲区 (Capture FBO) 与深度渲染缓冲区 (RBO)
    GLuint captureFBO, captureRBO;
    glGenFramebuffers(1, &captureFBO);
    glGenRenderbuffers(1, &captureRBO);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, cubemapSize, cubemapSize);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, captureRBO);

    // 步骤 B: 创建目标 3D 环境立方体贴图 (Cubemap Texture)
    GLuint envCubemap;
    glGenTextures(1, &envCubemap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);
    for (unsigned int i = 0; i < 6; ++i) {
        // 为 6 个面分别分配 16 位浮点 GL_RGB16F 存储
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F,
                     cubemapSize, cubemapSize, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); // 启用 Mipmap 线性采样
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // 步骤 C: 构建 90 度 FOV 透视投影矩阵与面向 6 个方向 (+X, -X, +Y, -Y, +Z, -Z) 的 View 矩阵
    glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    glm::mat4 captureViews[] = {
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
    };

    // 步骤 D: 绑定 Shader 与输入 2D HDR 纹理
    equirectShader_->use();
    equirectShader_->setInt("equirectangularMap", 0);
    equirectShader_->setMat4("projection", captureProjection);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, hdrTex);

    // 步骤 E: 调整 Viewport 并逐面渲染捕获
    glViewport(0, 0, cubemapSize, cubemapSize);
    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    for (unsigned int i = 0; i < 6; ++i) {
        equirectShader_->setMat4("view", captureViews[i]);
        // 将 FBO 的 Color Attachment0 绑定到当前立方体贴图的第 i 个面
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, envCubemap, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        renderCube();
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // 步骤 F: 自动生成 Mipmap (供后续 Prefilter 进行环境采样)
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    // 释放临时 FBO / RBO
    glDeleteFramebuffers(1, &captureFBO);
    glDeleteRenderbuffers(1, &captureRBO);

    return envCubemap;
}

GLuint IblPrecomputation::computeIrradianceCubemap(GLuint envCubemap, int cubemapSize) {
    // 步骤 A: 创建 32x32 分辨率捕获 FBO / RBO
    GLuint captureFBO, captureRBO;
    glGenFramebuffers(1, &captureFBO);
    glGenRenderbuffers(1, &captureRBO);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, cubemapSize, cubemapSize);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, captureRBO);

    // 步骤 B: 创建目标 Irradiance Cubemap 纹理
    GLuint irradianceMap;
    glGenTextures(1, &irradianceMap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, irradianceMap);
    for (unsigned int i = 0; i < 6; ++i) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F,
                     cubemapSize, cubemapSize, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    glm::mat4 captureViews[] = {
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
    };

    // 步骤 C: 绑定漫反射卷积 Shader 与原始环境 Cubemap
    irradianceShader_->use();
    irradianceShader_->setInt("environmentMap", 0);
    irradianceShader_->setMat4("projection", captureProjection);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);

    // 步骤 D: 在 Viewport (32x32) 下渲染卷积捕获 6 个面
    glViewport(0, 0, cubemapSize, cubemapSize);
    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    for (unsigned int i = 0; i < 6; ++i) {
        irradianceShader_->setMat4("view", captureViews[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, irradianceMap, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        renderCube();
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glDeleteFramebuffers(1, &captureFBO);
    glDeleteRenderbuffers(1, &captureRBO);

    return irradianceMap;
}

GLuint IblPrecomputation::computePrefilterCubemap(GLuint envCubemap, int cubemapSize) {
    GLuint captureFBO, captureRBO;
    glGenFramebuffers(1, &captureFBO);
    glGenRenderbuffers(1, &captureRBO);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);

    // 步骤 A: 创建 Prefilter Cubemap 并开启 Mipmap 链 (5 个 LOD 级别: 128x128 -> 64x64 -> 32x32 -> 16x16 -> 8x8)
    GLuint prefilterMap;
    glGenTextures(1, &prefilterMap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, prefilterMap);
    for (unsigned int i = 0; i < 6; ++i) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F,
                     cubemapSize, cubemapSize, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); // 支持根据粗糙度在不同 Mip 间三线性插值
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    glm::mat4 captureViews[] = {
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
    };

    prefilterShader_->use();
    prefilterShader_->setInt("environmentMap", 0);
    prefilterShader_->setMat4("projection", captureProjection);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    unsigned int maxMipLevels = 5;
    // 步骤 B: 遍历每个 Mipmap 级别 (对应粗糙度 roughness 0.0 到 1.0)
    for (unsigned int mip = 0; mip < maxMipLevels; ++mip) {
        // 动态调整当前 Mip 层级对应的宽高 Viewport
        unsigned int mipWidth = static_cast<unsigned int>(cubemapSize * std::pow(0.5, mip));
        unsigned int mipHeight = static_cast<unsigned int>(cubemapSize * std::pow(0.5, mip));
        
        glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipWidth, mipHeight);
        glViewport(0, 0, mipWidth, mipHeight);

        // roughness = mip / (maxMipLevels - 1) 均匀插值映射
        float roughness = (float)mip / (float)(maxMipLevels - 1);
        prefilterShader_->setFloat("roughness", roughness);

        // 渲染捕获 6 个面并附加到对应的 mip 层级
        for (unsigned int i = 0; i < 6; ++i) {
            prefilterShader_->setMat4("view", captureViews[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, prefilterMap, mip);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            renderCube();
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glDeleteFramebuffers(1, &captureFBO);
    glDeleteRenderbuffers(1, &captureRBO);

    return prefilterMap;
}

GLuint IblPrecomputation::computeBrdfLutTexture(int lutSize) {
    // 步骤 A: 创建 512x512 帧缓冲区
    GLuint captureFBO, captureRBO;
    glGenFramebuffers(1, &captureFBO);
    glGenRenderbuffers(1, &captureRBO);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, lutSize, lutSize);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, captureRBO);

    // 步骤 B: 创建 2D RG 16 位浮点纹理 (GL_RG16F) 存储 (Scale A, Bias B)
    GLuint brdfLUTTexture;
    glGenTextures(1, &brdfLUTTexture);
    glBindTexture(GL_TEXTURE_2D, brdfLUTTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, lutSize, lutSize, 0, GL_RG, GL_FLOAT, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // 步骤 C: 附加纹理到 FBO 并调整 Viewport
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, brdfLUTTexture, 0);
    glViewport(0, 0, lutSize, lutSize);

    // 步骤 D: 绘制全屏四边形执行 2D BRDF 准蒙特卡洛积分
    brdfShader_->use();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    renderQuad();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glDeleteFramebuffers(1, &captureFBO);
    glDeleteRenderbuffers(1, &captureRBO);

    return brdfLUTTexture;
}


void IblPrecomputation::renderCube() {
    if (cubeVAO_ == 0) {
        float vertices[] = {
            // 后面
            -1.0f, -1.0f, -1.0f,
             1.0f,  1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,
             1.0f,  1.0f, -1.0f,
            -1.0f, -1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f,
            // 前面
            -1.0f, -1.0f,  1.0f,
             1.0f, -1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
            -1.0f,  1.0f,  1.0f,
            -1.0f, -1.0f,  1.0f,
            // 左面
            -1.0f,  1.0f,  1.0f,
            -1.0f,  1.0f, -1.0f,
            -1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f,  1.0f,
            -1.0f,  1.0f,  1.0f,
            // 右面
             1.0f,  1.0f,  1.0f,
             1.0f, -1.0f, -1.0f,
             1.0f,  1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f, -1.0f,  1.0f,
            // 底面
            -1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,
             1.0f, -1.0f,  1.0f,
             1.0f, -1.0f,  1.0f,
            -1.0f, -1.0f,  1.0f,
            -1.0f, -1.0f, -1.0f,
            // 顶面
            -1.0f,  1.0f, -1.0f,
            -1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f,  1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f
        };
        glGenVertexArrays(1, &cubeVAO_);
        glGenBuffers(1, &cubeVBO_);

        glBindBuffer(GL_ARRAY_BUFFER, cubeVBO_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

        glBindVertexArray(cubeVAO_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

    glBindVertexArray(cubeVAO_);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
}

void IblPrecomputation::renderQuad() {
    if (quadVAO_ == 0) {
        float quadVertices[] = {
            // 位置        // 纹理坐标
            -1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
            -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
             1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
             1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
        };
        glGenVertexArrays(1, &quadVAO_);
        glGenBuffers(1, &quadVBO_);
        glBindVertexArray(quadVAO_);
        glBindBuffer(GL_ARRAY_BUFFER, quadVBO_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    }
    glBindVertexArray(quadVAO_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}
