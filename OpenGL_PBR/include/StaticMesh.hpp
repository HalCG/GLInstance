#ifndef STATIC_MESH_HPP
#define STATIC_MESH_HPP

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <string>
#include <vector>
#include <unordered_map>

/**
 * @brief 基础静态网格顶点内存结构 (不带切线)
 */
struct StaticMeshVertexData {
    float mPosition[4]; // 局部坐标 (x, y, z, w)
    float mTexcoord[4]; // 纹理坐标 (u, v, s, t)
    float mNormal[4];   // 法线向量 (nx, ny, nz, nw)
};

/**
 * @brief 扩展静态网格顶点内存结构 (包含正交切线，用于切线空间法线贴图)
 */
struct StaticMeshVertexDataEx {
    float mPosition[4]; // 局部坐标 (x, y, z, w)
    float mTexcoord[4]; // 纹理坐标 (u, v, s, t)
    float mNormal[4];   // 法线向量 (nx, ny, nz, nw)
    float mTangent[4];  // 切线向量 (tx, ty, tz, tw)
};

/**
 * @brief 子网格 (SubMesh) 索引缓冲区结构
 */
struct OpenGLSubMesh {
    GLuint ibo = 0;          // Element Array Buffer (Index Buffer) 句柄
    unsigned int indexCount = 0; // 索引总数
};

/**
 * @brief StaticMesh 二进制 3D 模型渲染类
 * 
 * 负责从自定义二进制格式 (.staticmesh) 加载 VAO/VBO 顶点缓冲区数据及 SubMesh IBO 索引数据，并执行绘制。
 */
class StaticMesh {
public:
    StaticMesh();
    ~StaticMesh();

    /**
     * @brief 加载标准 StaticMeshVertexData 格式顶点网格 (3 个属性)
     */
    bool loadFromFile(const std::string& filePath);

    /**
     * @brief 加载 StaticMeshVertexDataEx 格式带切线顶点网格 (4 个属性: Position, Texcoord, Normal, Tangent)
     */
    bool loadFromFile2(const std::string& filePath);

    /**
     * @brief 绘制所有子网格 (glDrawElements)
     */
    void draw() const;

private:
    void cleanup();

    GLuint vao_ = 0; // Vertex Array Object 句柄
    GLuint vbo_ = 0; // Vertex Buffer Object 句柄
    int vertexCount_ = 0;
    bool hasTangent_ = false;

    std::unordered_map<std::string, OpenGLSubMesh> subMeshes_; // 子网格映射表
};

/**
 * @brief 屏幕全屏四边形 (FullScreenQuad) 辅助绘制类
 */
class FullScreenQuad {
public:
    FullScreenQuad();
    ~FullScreenQuad();

    void draw() const;

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
};

#endif // STATIC_MESH_HPP

