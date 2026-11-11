#include "AATestScene.hpp"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <stb_image.h>
#include <cmath>
#include <iostream>
#include <vector>

namespace {

// 从 resources/ 加载 2D 贴图（stb），失败返回 0
GLuint loadTexture2D(const std::string &relativePath) {
    const std::string fullPath = AppConfig::resourcePath(relativePath);
    GLuint tex = 0;
    glGenTextures(1, &tex);
    int width = 0;
    int height = 0;
    int components = 0;
    stbi_set_flip_vertically_on_load(false);
    unsigned char *data = stbi_load(fullPath.c_str(), &width, &height, &components, 0);
    if (!data) {
        std::cerr << "Failed to load texture: " << fullPath << std::endl;
        glDeleteTextures(1, &tex);
        return 0;
    }

    GLenum format = GL_RGB;
    if (components == 1) {
        format = GL_RED;
    } else if (components == 4) {
        format = GL_RGBA;
    }

    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(format), width, height, 0, format, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);
    return tex;
}

// glTF 若未带上 diffuse，则手动挂一张 albedo，避免纯黑
void ensureDiffuseTexture(Model &model, const std::string &relativePath) {
    for (const Mesh &mesh : model.meshes) {
        for (const Texture &tex : mesh.textures) {
            if (tex.type == "texture_diffuse" && tex.id != 0) {
                return;
            }
        }
    }
    const GLuint texId = loadTexture2D(relativePath);
    if (texId == 0) {
        return;
    }
    Texture tex;
    tex.id = texId;
    tex.type = "texture_diffuse";
    tex.path = relativePath;
    for (Mesh &mesh : model.meshes) {
        mesh.textures.push_back(tex);
    }
}

// 程序生成棋盘格：高对比边缘，便于观察锯齿 / AA 效果
GLuint createCheckerTexture(int size = 512, int cells = 24) {
    std::vector<unsigned char> pixels(static_cast<size_t>(size * size * 3));
    const int cell = std::max(1, size / cells);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool dark = ((x / cell) + (y / cell)) % 2 == 0;
            const unsigned char c = dark ? 42 : 78;
            const size_t idx = static_cast<size_t>((y * size + x) * 3);
            pixels[idx + 0] = c;
            pixels[idx + 1] = static_cast<unsigned char>(c + 4);
            pixels[idx + 2] = static_cast<unsigned char>(c + 8);
        }
    }
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

Vertex makeVertex(const glm::vec3 &p, const glm::vec3 &n, const glm::vec2 &uv) {
    Vertex ver{};
    ver.Position = p;
    ver.Normal = n;
    ver.TexCoords = uv;
    return ver;
}

// Y=0 平面四边形；uvScale 控制棋盘重复密度
Mesh buildFloorMesh(GLuint texture, float halfExtent, float uvScale) {
    const float h = halfExtent;
    const float u = uvScale;
    std::vector<Vertex> vertices = {
        makeVertex({-h, 0.0f, -h}, {0, 1, 0}, {0, 0}),
        makeVertex({h, 0.0f, -h}, {0, 1, 0}, {u, 0}),
        makeVertex({h, 0.0f, h}, {0, 1, 0}, {u, u}),
        makeVertex({-h, 0.0f, h}, {0, 1, 0}, {0, u}),
    };
    std::vector<unsigned int> indices = {0, 1, 2, 0, 2, 3};
    Texture floorTex;
    floorTex.id = texture;
    floorTex.type = "texture_diffuse";
    return Mesh(vertices, indices, {floorTex});
}

glm::mat4 makeHelmetTransform(const glm::vec3 &pos, float yawDeg, float scale) {
    glm::mat4 m(1.0f);
    m = glm::translate(m, pos);
    m = glm::rotate(m, glm::radians(yawDeg), glm::vec3(0.0f, 1.0f, 0.0f));
    m = glm::rotate(m, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)); // glTF Z-up -> Y-up
    m = glm::scale(m, glm::vec3(scale));
    return m;
}

} // namespace

bool AATestScene::init() {
    checkerTexture_ = createCheckerTexture();
    floor_ = buildFloorMesh(checkerTexture_, AppConfig::kFloorHalfExtent, 8.0f);

    helmet_ = std::make_unique<Model>(AppConfig::resourcePath("models/DamagedHelmet/DamagedHelmet.gltf"));
    if (helmet_->meshes.empty()) {
        std::cerr << "Failed to load DamagedHelmet.gltf\n";
        return false;
    }
    ensureDiffuseTexture(*helmet_, "models/DamagedHelmet/Default_albedo.jpg");

    objects_.clear();
    // 中央主模型 + 两侧，轮廓边缘足够看 AA
    objects_.push_back({makeHelmetTransform(glm::vec3(0.0f, 0.0f, 0.0f), 25.0f, AppConfig::kHeroHelmetScale)});
    objects_.push_back({makeHelmetTransform(glm::vec3(-2.8f, 0.0f, 1.2f), -35.0f, AppConfig::kSideHelmetScale)});
    objects_.push_back({makeHelmetTransform(glm::vec3(2.8f, 0.0f, 1.2f), 55.0f, AppConfig::kSideHelmetScale)});
    return true;
}

void AATestScene::shutdown() {
    if (checkerTexture_) {
        glDeleteTextures(1, &checkerTexture_);
        checkerTexture_ = 0;
    }
    helmet_.reset();
    objects_.clear();
}

void AATestScene::drawOpaque(Shader &shader) const {
    // 先地板后头盔；model 矩阵按物体切换
    shader.setMat4("model", glm::mat4(1.0f));
    floor_.Draw(shader);

    if (!helmet_) {
        return;
    }
    // 同一头盔模型画三次：只换 model（位置 / 朝向 / 缩放）
    for (const SceneObject &obj : objects_) {
        shader.setMat4("model", obj.model);
        helmet_->Draw(shader);
    }
}
