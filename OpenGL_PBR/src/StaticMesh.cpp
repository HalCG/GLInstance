#include "StaticMesh.hpp"

#include <cstdio>
#include <iostream>

StaticMesh::StaticMesh() {}

StaticMesh::~StaticMesh() {
    cleanup();
}

void StaticMesh::cleanup() {
    // 释放所有子网格索引缓冲区 (IBO)
    for (auto& pair : subMeshes_) {
        if (pair.second.ibo != 0) {
            glDeleteBuffers(1, &pair.second.ibo);
            pair.second.ibo = 0;
        }
    }
    subMeshes_.clear();

    // 释放顶点缓冲区 (VBO) 和顶点数组对象 (VAO)
    if (vbo_ != 0) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
}

bool StaticMesh::loadFromFile(const std::string& filePath) {
    cleanup();
    hasTangent_ = false;

    FILE* pFile = nullptr;
    errno_t err = fopen_s(&pFile, filePath.c_str(), "rb");
    if (err != 0 || !pFile) {
        std::cerr << "Failed to open staticmesh file: " << filePath << std::endl;
        return false;
    }

    // 1. 读取顶点总数并加载 StaticMeshVertexData 数组
    fread(&vertexCount_, sizeof(int), 1, pFile);
    std::vector<StaticMeshVertexData> vertexData(vertexCount_);
    fread(vertexData.data(), sizeof(StaticMeshVertexData), vertexCount_, pFile);

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(StaticMeshVertexData) * vertexCount_, vertexData.data(), GL_STATIC_DRAW);

    // 属性位置 0：顶点位置 Position (vec4)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(StaticMeshVertexData), (void*)offsetof(StaticMeshVertexData, mPosition));

    // 属性位置 1：纹理坐标 Texcoord (vec4)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(StaticMeshVertexData), (void*)offsetof(StaticMeshVertexData, mTexcoord));

    // 属性位置 2：法线 Normal (vec4)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(StaticMeshVertexData), (void*)offsetof(StaticMeshVertexData, mNormal));

    // 2. 循环读取子网格 (SubMesh) 及其索引列表 (IBO)
    while (!feof(pFile)) {
        int nameLen = 0;
        int indexCount = 0;
        if (fread(&nameLen, sizeof(int), 1, pFile) != 1) break;
        if (feof(pFile)) break;

        std::vector<char> nameBuf(nameLen + 1, 0);
        fread(nameBuf.data(), sizeof(char), nameLen, pFile);
        std::string submeshName(nameBuf.data());

        fread(&indexCount, sizeof(int), 1, pFile);
        std::vector<unsigned int> indices(indexCount);
        fread(indices.data(), sizeof(unsigned int), indexCount, pFile);

        OpenGLSubMesh submesh;
        submesh.indexCount = indexCount;
        glGenBuffers(1, &submesh.ibo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, submesh.ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(unsigned int) * indexCount, indices.data(), GL_STATIC_DRAW);

        subMeshes_[submeshName] = submesh;
    }

    glBindVertexArray(0);
    fclose(pFile);
    return true;
}

bool StaticMesh::loadFromFile2(const std::string& filePath) {
    cleanup();
    hasTangent_ = true;

    FILE* pFile = nullptr;
    errno_t err = fopen_s(&pFile, filePath.c_str(), "rb");
    if (err != 0 || !pFile) {
        std::cerr << "Failed to open staticmesh file2: " << filePath << std::endl;
        return false;
    }

    // 1. 读取带正交切线的顶点数组 StaticMeshVertexDataEx
    fread(&vertexCount_, sizeof(int), 1, pFile);
    std::vector<StaticMeshVertexDataEx> vertexData(vertexCount_);
    fread(vertexData.data(), sizeof(StaticMeshVertexDataEx), vertexCount_, pFile);

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(StaticMeshVertexDataEx) * vertexCount_, vertexData.data(), GL_STATIC_DRAW);

    // 属性 0：Position (vec4)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(StaticMeshVertexDataEx), (void*)offsetof(StaticMeshVertexDataEx, mPosition));

    // 属性 1：Texcoord (vec4)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(StaticMeshVertexDataEx), (void*)offsetof(StaticMeshVertexDataEx, mTexcoord));

    // 属性 2：Normal (vec4)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(StaticMeshVertexDataEx), (void*)offsetof(StaticMeshVertexDataEx, mNormal));

    // 属性 3：Tangent (vec4)
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(StaticMeshVertexDataEx), (void*)offsetof(StaticMeshVertexDataEx, mTangent));

    // 2. 循环读取子网格索引块
    while (!feof(pFile)) {
        int nameLen = 0;
        int indexCount = 0;
        if (fread(&nameLen, sizeof(int), 1, pFile) != 1) break;
        if (feof(pFile)) break;

        std::vector<char> nameBuf(nameLen + 1, 0);
        fread(nameBuf.data(), sizeof(char), nameLen, pFile);
        std::string submeshName(nameBuf.data());

        fread(&indexCount, sizeof(int), 1, pFile);
        std::vector<unsigned int> indices(indexCount);
        fread(indices.data(), sizeof(unsigned int), indexCount, pFile);

        OpenGLSubMesh submesh;
        submesh.indexCount = indexCount;
        glGenBuffers(1, &submesh.ibo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, submesh.ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(unsigned int) * indexCount, indices.data(), GL_STATIC_DRAW);

        subMeshes_[submeshName] = submesh;
    }

    glBindVertexArray(0);
    fclose(pFile);
    return true;
}

void StaticMesh::draw() const {
    if (vao_ == 0) return;

    glBindVertexArray(vao_);
    if (subMeshes_.empty()) {
        glDrawArrays(GL_TRIANGLES, 0, vertexCount_);
    } else {
        // 遍历所有 SubMesh 执行索引绘制 glDrawElements
        for (const auto& pair : subMeshes_) {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, pair.second.ibo);
            glDrawElements(GL_TRIANGLES, pair.second.indexCount, GL_UNSIGNED_INT, 0);
        }
    }
    glBindVertexArray(0);
}

FullScreenQuad::FullScreenQuad() {
    float quadVertices[] = {
        // 位置(vec4)           // 纹理坐标(vec4)
        -1.0f, -1.0f, 0.0f, 1.0f,  0.0f, 0.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 0.0f, 1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,  0.0f, 1.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 0.0f, 1.0f,  1.0f, 1.0f, 0.0f, 1.0f,
    };

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(4 * sizeof(float)));

    glBindVertexArray(0);
}

FullScreenQuad::~FullScreenQuad() {
    if (vbo_ != 0) glDeleteBuffers(1, &vbo_);
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
}

void FullScreenQuad::draw() const {
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}

