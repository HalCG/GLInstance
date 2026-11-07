#ifndef MESH_H
#define MESH_H

#include <glad/glad.h> // 包含所有 OpenGL 类型声明

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "Shader.hpp"

#include <string>
#include <vector>
using namespace std;

#define MAX_BONE_INFLUENCE 4

struct Vertex
{
    // 位置
    glm::vec3 Position;
    // 法线
    glm::vec3 Normal;
    // 纹理坐标
    glm::vec2 TexCoords;
    // 切线
    glm::vec3 Tangent;
    // 副切线
    glm::vec3 Bitangent;
    // 影响该顶点的骨骼索引
    int m_BoneIDs[MAX_BONE_INFLUENCE];
    // 各骨骼的权重
    float m_Weights[MAX_BONE_INFLUENCE];
};

struct Texture
{
    unsigned int id;
    string type;
    string path;
    Texture() = default;
};

class Mesh
{
  public:
    // 网格数据
    vector<Vertex> vertices;
    vector<unsigned int> indices;
    vector<Texture> textures;
    unsigned int VAO;
    Mesh() = default;
    // 构造函数
    Mesh(vector<Vertex> vertices, vector<unsigned int> indices, vector<Texture> textures)
    {
        this->vertices = vertices;
        this->indices = indices;
        this->textures = textures;

        // 已具备全部所需数据，接下来设置顶点缓冲及其属性指针。
        setupMesh();
    }

    // 渲染网格
    void Draw(Shader &shader)
    {
        unsigned int diffuseNr = 1;
        unsigned int specularNr = 1;
        unsigned int normalNr = 1;
        unsigned int heightNr = 1;
        for (unsigned int i = 0; i < textures.size(); i++)
        {
            glActiveTexture(GL_TEXTURE0 + i);
            const string &type = textures[i].type;
            if (type == "texture_diffuse")
            {
                switch (diffuseNr++) {
                case 1: shader.setInt("texture_diffuse1", static_cast<int>(i)); break;
                case 2: shader.setInt("texture_diffuse2", static_cast<int>(i)); break;
                case 3: shader.setInt("texture_diffuse3", static_cast<int>(i)); break;
                default: break;
                }
            }
            else if (type == "texture_specular")
            {
                switch (specularNr++) {
                case 1: shader.setInt("texture_specular1", static_cast<int>(i)); break;
                case 2: shader.setInt("texture_specular2", static_cast<int>(i)); break;
                default: break;
                }
            }
            else if (type == "texture_normal")
            {
                switch (normalNr++) {
                case 1: shader.setInt("texture_normal1", static_cast<int>(i)); break;
                default: break;
                }
            }
            else if (type == "texture_height")
            {
                switch (heightNr++) {
                case 1: shader.setInt("texture_height1", static_cast<int>(i)); break;
                default: break;
                }
            }
            glBindTexture(GL_TEXTURE_2D, textures[i].id);
        }

        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, static_cast<unsigned int>(indices.size()), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);

        glActiveTexture(GL_TEXTURE0);
    }

  private:
    // 渲染数据
    unsigned int VBO, EBO;

  protected:
    // 初始化所有缓冲对象/数组
    void setupMesh()
    {
        // 创建缓冲/数组
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);

        glBindVertexArray(VAO);
        // 将数据载入顶点缓冲
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        // 结构体的一大优点是其成员在内存中连续排布。
        // 因此我们可以直接传入结构体指针，它会完美映射为 glm::vec3/2
        // 数组，再进一步映射为 3/2 个 float，最终对应字节数组。
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), &vertices[0], GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), &indices[0], GL_STATIC_DRAW);

        // 设置顶点属性指针
        // 顶点位置
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)0);
        // 顶点法线
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, Normal));
        // 顶点纹理坐标
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, TexCoords));
        // 顶点切线
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, Tangent));
        // 顶点副切线
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, Bitangent));
        // 骨骼 ID
        glEnableVertexAttribArray(5);
        glVertexAttribIPointer(5, 4, GL_INT, sizeof(Vertex), (void *)offsetof(Vertex, m_BoneIDs));

        // 权重
        glEnableVertexAttribArray(6);
        glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, m_Weights));
        glBindVertexArray(0);
    }
};
#endif
