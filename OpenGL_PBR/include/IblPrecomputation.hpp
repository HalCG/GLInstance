#ifndef IBL_PRECOMPUTATION_HPP
#define IBL_PRECOMPUTATION_HPP

#include <glad/glad.h>
#include <string>
#include <memory>
#include "Shader.hpp"

/**
 * @brief IblPrecomputation 基于图像的环境光照 (IBL) 离屏预计算管线类
 * 
 * 负责在渲染引擎初始化阶段将 2D 全景 HDR 图像通过离屏 FBO 渲染预处理为：
 * 1. 3D 环境立方体贴图 (Environment Cubemap - 512x512)
 * 2. 漫反射辐照度立方体贴图 (Irradiance Cubemap - 32x32)
 * 3. 镜面反射预过滤立方体贴图 (Prefilter Cubemap - 128x128，包含 Mipmap 链)
 * 4. 2D BRDF 积分查找表 (BRDF LUT Texture - 512x512)
 */
class IblPrecomputation {
public:
    IblPrecomputation() = default;
    ~IblPrecomputation();

    /**
     * @brief 初始化 IBL 离屏计算所需的全部 Shader
     * @param shaderDir Shader 文件所在的根路径
     * @return 是否成功加载并编译所有 Shader
     */
    bool init(const std::string& shaderDir);
    
    /**
     * @brief 使用 stb_image 加载 2D HDR 等距柱状全景图像文件 (.hdr)
     * @param path HDR 图像文件路径
     * @return 生成的 OpenGL 2D 浮点纹理 ID (GL_RGB16F)
     */
    GLuint loadHdrTexture(const std::string& path);

    /**
     * @brief 将 2D 矩形全景 HDR 纹理投影转换并渲染为 3D 立方体贴图 (Cubemap)
     * @param hdrTex 输入的 2D HDR 纹理 ID
     * @param cubemapSize 立方体贴图分辨率 (默认 512x512)
     * @return 生成的 OpenGL 3D Cubemap 纹理 ID
     */
    GLuint convertEquirectangularToCubemap(GLuint hdrTex, int cubemapSize = 512);

    /**
     * @brief 卷积计算漫反射 Radiance/Irradiance 辐照度立方体贴图
     * @param envCubemap 原始 3D 环境立方体贴图
     * @param cubemapSize 辐照度贴图分辨率 (因低频漫反射，默认 32x32 足够)
     * @return 生成的 Irradiance Cubemap 纹理 ID
     */
    GLuint computeIrradianceCubemap(GLuint envCubemap, int cubemapSize = 32);

    /**
     * @brief 重要性采样预过滤镜面反射环境立方体贴图 (包含多级粗糙度 Mipmap)
     * @param envCubemap 原始 3D 环境立方体贴图
     * @param cubemapSize 基础 level 0 分辨率 (默认 128x128)
     * @return 生成的 Prefilter Cubemap 纹理 ID
     */
    GLuint computePrefilterCubemap(GLuint envCubemap, int cubemapSize = 128);

    /**
     * @brief 生成 2D BRDF 积分查找表 (Split-Sum 镜面反射第二项 LUT)
     * @param lutSize 查找表宽高分辨率 (默认 512x512)
     * @return 生成的 2D 浮点纹理 ID (GL_RG16F)
     */
    GLuint computeBrdfLutTexture(int lutSize = 512);

    /**
     * @brief 绘制包含 3D 位置属性的内部单位立方体 (用于捕获渲染 6 个方向)
     */
    static void renderCube();

    /**
     * @brief 绘制包含 UV 属性的全屏四边形 (用于渲染 2D BRDF LUT)
     */
    static void renderQuad();

private:
    std::unique_ptr<Shader> equirectShader_;  // 等距柱状投影转 Cubemap Shader
    std::unique_ptr<Shader> irradianceShader_; // 漫反射辐照度卷积 Shader
    std::unique_ptr<Shader> prefilterShader_;  // 镜面反射预过滤 Shader
    std::unique_ptr<Shader> brdfShader_;       // 2D BRDF LUT 积分 Shader

    // 辅助几何体 VAO / VBO 静态句柄
    static GLuint cubeVAO_;
    static GLuint cubeVBO_;
    static GLuint quadVAO_;
    static GLuint quadVBO_;
};

#endif // IBL_PRECOMPUTATION_HPP

