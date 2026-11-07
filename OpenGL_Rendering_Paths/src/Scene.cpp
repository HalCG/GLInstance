#include "Scene.hpp"

#include <glad/glad.h>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stb_image.h>

#include <cmath>
#include <iostream>
#include <vector>

namespace {

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

GLuint createCheckerTexture(int size, int tile) {
    std::vector<unsigned char> pixels(static_cast<size_t>(size) * static_cast<size_t>(size) * 3u);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool dark = ((x / tile) + (y / tile)) % 2 == 0;
            const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(size) + static_cast<size_t>(x)) * 3u;
            if (dark) {
                pixels[i + 0] = 48;
                pixels[i + 1] = 52;
                pixels[i + 2] = 60;
            } else {
                pixels[i + 0] = 78;
                pixels[i + 1] = 84;
                pixels[i + 2] = 94;
            }
        }
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

void ensureDiffuseTexture(Model &model, const std::string &relativePath) {
    bool hasDiffuse = false;
    for (const Mesh &mesh : model.meshes) {
        for (const Texture &tex : mesh.textures) {
            if (tex.type == "texture_diffuse") {
                hasDiffuse = true;
                break;
            }
        }
        if (hasDiffuse) {
            break;
        }
    }
    if (hasDiffuse) {
        return;
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
        mesh.textures.clear();
        mesh.textures.push_back(tex);
    }
}

Vertex makeVertex(const glm::vec3 &p, const glm::vec3 &n, const glm::vec2 &uv) {
    Vertex ver{};
    ver.Position = p;
    ver.Normal = n;
    ver.TexCoords = uv;
    return ver;
}

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
    // DamagedHelmet glTF 为 Z-up，转到 Y-up
    m = glm::rotate(m, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    m = glm::scale(m, glm::vec3(scale));
    return m;
}

} // namespace

bool Scene::init() {
    const GLuint floorTex = createCheckerTexture(512, 32);
    floor_ = buildFloorMesh(floorTex, AppConfig::kFloorHalfExtent, 10.0f);

    heroModel_ = std::make_unique<Model>(AppConfig::resourcePath("models/DamagedHelmet/DamagedHelmet.gltf"));
    if (heroModel_->meshes.empty()) {
        std::cerr << "Failed to load DamagedHelmet.gltf\n";
        return false;
    }
    ensureDiffuseTexture(*heroModel_, "models/DamagedHelmet/Default_albedo.jpg");

    objects_.clear();

    // 中央主头盔（直接落在地板上）
    {
        SceneObject hero;
        hero.model = makeHelmetTransform(glm::vec3(0.0f, 0.0f, 0.0f), 25.0f, AppConfig::kHeroHelmetScale);
        objects_.push_back(hero);
    }

    // 环绕 5 个稍小头盔，拉开多光源对比空间
    const float ringRadius = 4.2f;
    const float ringYaws[] = {0.0f, 72.0f, 144.0f, 216.0f, 288.0f};
    for (float yaw : ringYaws) {
        const float rad = glm::radians(yaw);
        const glm::vec3 pos(std::cos(rad) * ringRadius, 0.0f, std::sin(rad) * ringRadius);
        SceneObject obj;
        obj.model = makeHelmetTransform(pos, yaw + 180.0f, AppConfig::kSideHelmetScale);
        objects_.push_back(obj);
    }

    return true;
}

void Scene::shutdown() {
    heroModel_.reset();
    objects_.clear();
}

void Scene::drawSpotMeshes(Shader &shader) const {
    if (!heroModel_) {
        return;
    }
    for (const SceneObject &obj : objects_) {
        // normalMatrix 在 CPU 算好再上传，避免 mesh.vert 每顶点 inverse(transpose)
        shader.setMat4("model", obj.model);
        shader.setMat3("normalMatrix", glm::inverseTranspose(glm::mat3(obj.model)));
        heroModel_->Draw(shader);
    }
}

void Scene::drawFloor(Shader &shader) const {
    shader.setMat4("model", glm::mat4(1.0f));
    shader.setMat3("normalMatrix", glm::mat3(1.0f));
    floor_.Draw(shader);
}
