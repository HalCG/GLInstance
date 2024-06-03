#ifndef SHADER_H
#define SHADER_H
#include "glad/glad.h"

#include <glm/glm.hpp>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

class Shader {
public:
  unsigned int ID;
  // 构造函数即时生成着色器
  // ------------------------------------------------------------------------
  Shader(const char *vertexPath, const char *fragmentPath,
         std::string geometryPath = "") {
    std::string vertexCode;
    std::string fragmentCode;
    std::string geometryCode;

    auto readFile = [](const char *path, std::string &out) -> bool {
      std::ifstream file(path, std::ios::in | std::ios::binary);
      if (!file.is_open()) {
        std::cout << "ERROR::SHADER::FILE_NOT_FOUND: " << path << std::endl;
        return false;
      }
      std::stringstream buffer;
      buffer << file.rdbuf();
      out = buffer.str();
      if (out.empty()) {
        std::cout << "ERROR::SHADER::FILE_EMPTY: " << path << std::endl;
        return false;
      }
      return true;
    };

    const bool vertexOk = readFile(vertexPath, vertexCode);
    const bool fragmentOk = readFile(fragmentPath, fragmentCode);
    bool geometryOk = true;
    if (!geometryPath.empty()) {
      geometryOk = readFile(geometryPath.c_str(), geometryCode);
    }

    if (!vertexOk || !fragmentOk || !geometryOk) {
      ID = 0;
      return;
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

    if (!geometryPath.empty()) {
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
    if (!geometryPath.empty()) {
      glDeleteShader(geometry);
    }
  }
  // 激活着色器
  // ------------------------------------------------------------------------
  void use() const {
    if (ID != 0) {
      glUseProgram(ID);
    }
  }
  // 实用的 uniform 设置函数
  // ------------------------------------------------------------------------
  void setBool(const std::string &name, bool value) const {
    glUniform1i(glGetUniformLocation(ID, name.c_str()), (int)value);
  }
  // ------------------------------------------------------------------------
  void setInt(const std::string &name, int value) const {
    glUniform1i(glGetUniformLocation(ID, name.c_str()), value);
  }
  void setUint(const std::string &name, int value) const {
    glUniform1ui(glGetUniformLocation(ID, name.c_str()), value);
  }
  // ------------------------------------------------------------------------
  void setFloat(const std::string &name, float value) const {
    glUniform1f(glGetUniformLocation(ID, name.c_str()), value);
  }
  // ------------------------------------------------------------------------
  void setVec2(const std::string &name, const glm::vec2 &value) const {
    glUniform2fv(glGetUniformLocation(ID, name.c_str()), 1, &value[0]);
  }
  void setVec2(const std::string &name, float x, float y) const {
    glUniform2f(glGetUniformLocation(ID, name.c_str()), x, y);
  }
  // ------------------------------------------------------------------------
  void setVec3(const std::string &name, const glm::vec3 &value) const {
    glUniform3fv(glGetUniformLocation(ID, name.c_str()), 1, &value[0]);
  }
  void setVec3(const std::string &name, float x, float y, float z) const {
    glUniform3f(glGetUniformLocation(ID, name.c_str()), x, y, z);
  }
  // ------------------------------------------------------------------------
  void setVec4(const std::string &name, const glm::vec4 &value) const {
    glUniform4fv(glGetUniformLocation(ID, name.c_str()), 1, &value[0]);
  }
  void setVec4(const std::string &name, float x, float y, float z,
               float w) const {
    glUniform4f(glGetUniformLocation(ID, name.c_str()), x, y, z, w);
  }
  // ------------------------------------------------------------------------
  void setMat2(const std::string &name, const glm::mat2 &mat) const {
    glUniformMatrix2fv(glGetUniformLocation(ID, name.c_str()), 1, GL_FALSE,
                       &mat[0][0]);
  }
  // ------------------------------------------------------------------------
  void setMat3(const std::string &name, const glm::mat3 &mat) const {
    glUniformMatrix3fv(glGetUniformLocation(ID, name.c_str()), 1, GL_FALSE,
                       &mat[0][0]);
  }
  // ------------------------------------------------------------------------
  void setMat4(const std::string &name, const glm::mat4 &mat) const {
    glUniformMatrix4fv(glGetUniformLocation(ID, name.c_str()), 1, GL_FALSE,
                       &mat[0][0]);
  }

private:
  // 用于检查着色器编译/链接错误的工具函数。
  // ------------------------------------------------------------------------
  void checkCompileErrors(GLuint shader, std::string type) {
    GLint success;
    GLchar infoLog[1024];
    if (type != "PROGRAM") {
      glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
      if (!success) {
        glGetShaderInfoLog(shader, 1024, NULL, infoLog);
        std::cout
            << "ERROR::SHADER_COMPILATION_ERROR of type: " << type << "\n"
            << infoLog
            << "\n -- --------------------------------------------------- -- "
            << std::endl;
      }
    } else {
      glGetProgramiv(shader, GL_LINK_STATUS, &success);
      if (!success) {
        glGetProgramInfoLog(shader, 1024, NULL, infoLog);
        std::cout
            << "ERROR::PROGRAM_LINKING_ERROR of type: " << type << "\n"
            << infoLog
            << "\n -- --------------------------------------------------- -- "
            << std::endl;
      }
    }
  }
};
#endif