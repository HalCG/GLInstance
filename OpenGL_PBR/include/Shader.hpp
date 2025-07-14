#ifndef SHADER_HPP
#define SHADER_HPP

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <string>

/**
 * @brief Shader 着色器程序对象封装类
 * 
 * 负责从文本文件加载顶点着色器和片段着色器，执行 OpenGL 编译与链接，
 * 并提供易用的 Uniform 参数传参接口 (setBool, setInt, setFloat, setVec3, setMat4 等)。
 */
class Shader {
public:
    GLuint ID = 0; // OpenGL Shader Program ID

    Shader() = default;
    Shader(const char* vertexPath, const char* fragmentPath);
    ~Shader();

    /**
     * @brief 从文件读取并编译链接 Shader
     * @return 成功返回 true
     */
    bool loadFromFiles(const std::string& vertexPath, const std::string& fragmentPath);

    /**
     * @brief 激活当前 Shader Program (glUseProgram)
     */
    void use() const;

    // Uniform 赋值辅助函数
    void setBool(const std::string& name, bool value) const;
    void setInt(const std::string& name, int value) const;
    void setFloat(const std::string& name, float value) const;
    void setVec2(const std::string& name, const glm::vec2& value) const;
    void setVec3(const std::string& name, const glm::vec3& value) const;
    void setVec4(const std::string& name, const glm::vec4& value) const;
    void setMat4(const std::string& name, const glm::mat4& value) const;

private:
    void checkCompileErrors(GLuint shader, const std::string& type);
};

#endif // SHADER_HPP

