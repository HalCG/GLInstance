#ifndef MODEL_H
#define MODEL_H

#include <glad/glad.h> // 包含所有 OpenGL 类型声明

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stb_image.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "Mesh.hpp"
#include "Shader.hpp"

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <map>
#include <vector>
using namespace std;

unsigned int TextureFromFile(const char *path, const string &directory, bool gamma = false);

class Model
{
  public:
    // 模型数据
    vector<Texture> textures_loaded; // 存储目前已加载的全部纹理，用于优化，确保同一纹理不会
                                     // 被重复加载。
    vector<Mesh> meshes;
    string directory;
    bool gammaCorrection;

    // 构造函数，需要一个指向 3D 模型的文件路径。
    Model(string const &path, bool gamma = false) : gammaCorrection(gamma)
    {
        loadModel(path);
    }

    // 绘制模型及其全部网格
    void Draw(Shader &shader)
    {
        for (unsigned int i = 0; i < meshes.size(); i++)
            meshes[i].Draw(shader);
    }

  private:
    // 使用 Assimp 支持的扩展从文件加载模型，并将得到的网格存入 meshes 向量。
    void loadModel(string const &path)
    {
        // 通过 Assimp 读取文件
        Assimp::Importer importer;
        const aiScene *scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                                                           aiProcess_FlipUVs | aiProcess_CalcTangentSpace);
        // 检查错误
        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) // 若不为零
        {
            cout << "ERROR::ASSIMP:: " << importer.GetErrorString() << endl;
            return;
        }
        // 从文件路径中提取所在目录
        directory = path.substr(0, path.find_last_of('/'));

        // 递归处理 Assimp 的根节点
        processNode(scene->mRootNode, scene);
    }

    // 以递归方式处理节点：处理该节点上的每个网格，再对其
    // 子节点（若有）重复此过程。
    void processNode(aiNode *node, const aiScene *scene)
    {
        // 处理当前节点上的每个网格
        for (unsigned int i = 0; i < node->mNumMeshes; i++)
        {
            // 节点对象只包含用于索引场景中实际对象的索引。
            // 场景保存全部数据，节点仅用于组织关系（如节点之间的层级）。
            aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];
            meshes.push_back(processMesh(mesh, scene));
        }
        // 处理完所有网格（若有）后，再递归处理每个子节点
        for (unsigned int i = 0; i < node->mNumChildren; i++)
        {
            processNode(node->mChildren[i], scene);
        }
    }

    Mesh processMesh(aiMesh *mesh, const aiScene *scene)
    {
        // 待填充的数据
        vector<Vertex> vertices;
        vector<unsigned int> indices;
        vector<Texture> textures;

        // 遍历网格的每个顶点
        for (unsigned int i = 0; i < mesh->mNumVertices; i++)
        {
            Vertex vertex;
            glm::vec3
                vector; // 声明占位向量：Assimp 使用自己的向量类，无法直接
                        // 转换为 glm::vec3，因此先把数据拷到这个占位 glm::vec3。
            // 位置
            vector.x = mesh->mVertices[i].x;
            vector.y = mesh->mVertices[i].y;
            vector.z = mesh->mVertices[i].z;
            vertex.Position = vector;
            // 法线
            if (mesh->HasNormals())
            {
                vector.x = mesh->mNormals[i].x;
                vector.y = mesh->mNormals[i].y;
                vector.z = mesh->mNormals[i].z;
                vertex.Normal = vector;
            }
            // 纹理坐标
            if (mesh->mTextureCoords[0]) // 网格是否包含纹理坐标？
            {
                glm::vec2 vec;
                // 一个顶点最多可有 8 套不同纹理坐标。我们假设不会使用
                // 同一顶点多套纹理坐标的模型，因此始终取第一套（索引 0）。
                vec.x = mesh->mTextureCoords[0][i].x;
                vec.y = mesh->mTextureCoords[0][i].y;
                vertex.TexCoords = vec;
                // 切线
                vector.x = mesh->mTangents[i].x;
                vector.y = mesh->mTangents[i].y;
                vector.z = mesh->mTangents[i].z;
                vertex.Tangent = vector;
                // 副切线
                vector.x = mesh->mBitangents[i].x;
                vector.y = mesh->mBitangents[i].y;
                vector.z = mesh->mBitangents[i].z;
                vertex.Bitangent = vector;
            }
            else
                vertex.TexCoords = glm::vec2(0.0f, 0.0f);

            vertices.push_back(vertex);
        }
        // 接着遍历网格的每个面（面即网格三角形），并取出对应的
        // 顶点索引。
        for (unsigned int i = 0; i < mesh->mNumFaces; i++)
        {
            aiFace face = mesh->mFaces[i];
            // 取出该面的全部索引并存入 indices 向量
            for (unsigned int j = 0; j < face.mNumIndices; j++)
                indices.push_back(face.mIndices[j]);
        }
        // 处理材质
        aiMaterial *material = scene->mMaterials[mesh->mMaterialIndex];
        // 我们约定着色器中采样器的命名。每张漫反射纹理应命名为
        // 'texture_diffuseN'，其中 N 为从 1 到 MAX_SAMPLER_NUMBER 的序号。
        // 其他纹理同理，汇总如下：
        // 漫反射: texture_diffuseN
        // 镜面反射: texture_specularN
        // 法线: texture_normalN

        // 1. 漫反射 / baseColor 贴图（glTF PBR 常用 BASE_COLOR）
        vector<Texture> diffuseMaps = loadMaterialTextures(material, aiTextureType_DIFFUSE, "texture_diffuse");
        textures.insert(textures.end(), diffuseMaps.begin(), diffuseMaps.end());
        if (diffuseMaps.empty()) {
            vector<Texture> baseColorMaps =
                loadMaterialTextures(material, aiTextureType_BASE_COLOR, "texture_diffuse");
            textures.insert(textures.end(), baseColorMaps.begin(), baseColorMaps.end());
        }
        // 2. 镜面反射贴图
        vector<Texture> specularMaps = loadMaterialTextures(material, aiTextureType_SPECULAR, "texture_specular");
        textures.insert(textures.end(), specularMaps.begin(), specularMaps.end());
        // 3. 法线贴图
        std::vector<Texture> normalMaps = loadMaterialTextures(material, aiTextureType_HEIGHT, "texture_normal");
        textures.insert(textures.end(), normalMaps.begin(), normalMaps.end());
        // 4. 高度贴图
        std::vector<Texture> heightMaps = loadMaterialTextures(material, aiTextureType_AMBIENT, "texture_height");
        textures.insert(textures.end(), heightMaps.begin(), heightMaps.end());

        // 用提取的网格数据创建并返回 Mesh 对象
        return Mesh(vertices, indices, textures);
    }

    // 检查给定类型的全部材质纹理；若尚未加载则进行加载。
    // 所需信息以 Texture 结构体返回。
    vector<Texture> loadMaterialTextures(aiMaterial *mat, aiTextureType type, string typeName)
    {
        vector<Texture> textures;
        for (unsigned int i = 0; i < mat->GetTextureCount(type); i++)
        {
            aiString str;
            mat->GetTexture(type, i, &str);
            // 检查纹理是否已加载；若已加载则跳过本次，不再加载新纹理
            bool skip = false;
            for (unsigned int j = 0; j < textures_loaded.size(); j++)
            {
                if (std::strcmp(textures_loaded[j].path.data(), str.C_Str()) == 0)
                {
                    textures.push_back(textures_loaded[j]);
                    skip = true; // 相同路径的纹理已加载过，继续下一个。
                                 // （优化）
                    break;
                }
            }
            if (!skip)
            { // 若纹理尚未加载，则加载它
                Texture texture;
                texture.id = TextureFromFile(str.C_Str(), this->directory);
                texture.type = typeName;
                texture.path = str.C_Str();
                textures.push_back(texture);
                textures_loaded.push_back(texture); // 存为整个模型已加载的纹理，确保不会
                                                    // 不必要地重复加载同一纹理。
            }
        }
        return textures;
    }
};

inline unsigned int TextureFromFile(const char *path, const string &directory, bool gamma)
{
    string filename = string(path);
    filename = directory + '/' + filename;

    unsigned int textureID;
    glGenTextures(1, &textureID);

    int width, height, nrComponents;
    unsigned char *data = stbi_load(filename.c_str(), &width, &height, &nrComponents, 0);
    if (data)
    {
        GLenum format;
        if (nrComponents == 1)
            format = GL_RED;
        else if (nrComponents == 3)
            format = GL_RGB;
        else if (nrComponents == 4)
            format = GL_RGBA;

        glBindTexture(GL_TEXTURE_2D, textureID);
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        stbi_image_free(data);
    }
    else
    {
        std::cout << "Texture failed to load at path: " << filename << std::endl;
        stbi_image_free(data);
        glDeleteTextures(1, &textureID);
        return 0;
    }

    return textureID;
}
#endif
