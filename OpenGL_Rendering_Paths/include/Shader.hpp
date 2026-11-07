#ifndef SHADER_H
#define SHADER_H
#include <glad/glad.h>

#include <glm/glm.hpp>

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <unordered_map>

class Shader
{
  public:
    unsigned int ID;
    // 构造函数即时生成着色器
    // ------------------------------------------------------------------------
    Shader(const char *vertexPath, const char *fragmentPath, const char *geometryPath = "")
    {
        // 1. 从文件路径获取顶点/片段着色器源码
        std::string vertexCode;
        std::string fragmentCode;
        std::string geometryCode;

        std::ifstream vShaderFile;
        std::ifstream fShaderFile;
        std::ifstream gShaderFile;

        // 确保 ifstream 对象可以抛出异常：
        vShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        fShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        try
        {
            // 打开文件
            vShaderFile.open(vertexPath);
            fShaderFile.open(fragmentPath);
            std::stringstream vShaderStream, fShaderStream;
            // 将文件缓冲内容读入流
            vShaderStream << vShaderFile.rdbuf();
            fShaderStream << fShaderFile.rdbuf();
            // 关闭文件句柄
            vShaderFile.close();
            fShaderFile.close();
            // 将流转换为字符串
            vertexCode = vShaderStream.str();
            fragmentCode = fShaderStream.str();
        }
        catch (std::ifstream::failure &e)
        {
            std::cout << "ERROR::SHADER::FILE_NOT_SUCCESSFULLY_READ: " << e.what() << std::endl;
        }

        const char *vShaderCode = vertexCode.c_str();
        const char *fShaderCode = fragmentCode.c_str();
        // 2. 编译着色器
        unsigned int vertex, fragment, geometry;
        // 顶点着色器
        vertex = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertex, 1, &vShaderCode, NULL);
        glCompileShader(vertex);
        checkCompileErrors(vertex, "VERTEX");
        // 片段着色器
        fragment = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragment, 1, &fShaderCode, NULL);
        glCompileShader(fragment);
        checkCompileErrors(fragment, "FRAGMENT");
        // 着色器程序
        ID = glCreateProgram();
        glAttachShader(ID, vertex);
        glAttachShader(ID, fragment);

        if (geometryPath != "")
        {
            // 打开文件
            gShaderFile.open(geometryPath);
            std::stringstream gShaderStream;
            // 将文件缓冲内容读入流
            gShaderStream << gShaderFile.rdbuf();

            // 关闭文件句柄
            gShaderFile.close();
            // 将流转换为字符串
            geometryCode = gShaderStream.str();

            const char *gShaderCode = geometryCode.c_str();

            // 几何着色器
            geometry = glCreateShader(GL_GEOMETRY_SHADER);
            glShaderSource(geometry, 1, &gShaderCode, NULL);
            glCompileShader(geometry);
            checkCompileErrors(geometry, "GEOMETRY");
            // 着色器程序
            glAttachShader(ID, geometry);
        }

        glLinkProgram(ID);
        checkCompileErrors(ID, "PROGRAM");
        // 着色器已链入程序，可删除，不再需要
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        if (geometryPath != "")
        {
            glDeleteShader(geometry);
        }
    }
    // 激活着色器
    // ------------------------------------------------------------------------
    void use() const
    {
        glUseProgram(ID);
    }
    // 实用的 uniform 设置函数
    // ------------------------------------------------------------------------
    void setBool(const std::string &name, bool value) const
    {
        glUniform1i(getLocation(name), (int)value);
    }
    // ------------------------------------------------------------------------
    void setInt(const std::string &name, int value) const
    {
        glUniform1i(getLocation(name), value);
    }
    // ------------------------------------------------------------------------
    void setVec3(const std::string &name, const glm::vec3 &value) const
    {
        glUniform3fv(getLocation(name), 1, &value[0]);
    }
    void setVec3(const std::string &name, float x, float y, float z) const
    {
        glUniform3f(getLocation(name), x, y, z);
    }
    // ------------------------------------------------------------------------
    void setMat3(const std::string &name, const glm::mat3 &mat) const
    {
        glUniformMatrix3fv(getLocation(name), 1, GL_FALSE, &mat[0][0]);
    }
    // ------------------------------------------------------------------------
    void setMat4(const std::string &name, const glm::mat4 &mat) const
    {
        glUniformMatrix4fv(getLocation(name), 1, GL_FALSE, &mat[0][0]);
    }

  private:
    mutable std::unordered_map<std::string, GLint> uniformCache_;

    GLint getLocation(const std::string &name) const
    {
        const auto it = uniformCache_.find(name);
        if (it != uniformCache_.end()) {
            return it->second;
        }
        const GLint location = glGetUniformLocation(ID, name.c_str());
        uniformCache_[name] = location;
        return location;
    }

    // 用于检查着色器编译/链接错误的工具函数。
    // ------------------------------------------------------------------------
    void checkCompileErrors(GLuint shader, std::string type)
    {
        GLint success;
        GLchar infoLog[1024];
        if (type != "PROGRAM")
        {
            glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
            if (!success)
            {
                glGetShaderInfoLog(shader, 1024, NULL, infoLog);
                std::cout << "ERROR::SHADER_COMPILATION_ERROR of type: " << type << "\n"
                          << infoLog << "\n -- --------------------------------------------------- -- " << std::endl;
            }
        }
        else
        {
            glGetProgramiv(shader, GL_LINK_STATUS, &success);
            if (!success)
            {
                glGetProgramInfoLog(shader, 1024, NULL, infoLog);
                std::cout << "ERROR::PROGRAM_LINKING_ERROR of type: " << type << "\n"
                          << infoLog << "\n -- --------------------------------------------------- -- " << std::endl;
            }
        }
    }
};
#endif