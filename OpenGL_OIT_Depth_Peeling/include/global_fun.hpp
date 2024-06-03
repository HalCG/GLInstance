#pragma once
#include <iostream>
#include "glad/glad.h"

void checkOpenGLError(const char *stmt, const char *fname, int line)
{
    GLenum err = glGetError();
    while (err != GL_NO_ERROR)
    {
        std::cerr << "OpenGL error " << err << " at " << fname << ":" << line << " - for " << stmt << std::endl;
        err = glGetError(); // 继续检查是否有其他错误
    }
}

#define CHECK_GL(stmt)                                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        stmt;                                                                                                          \
        checkOpenGLError(#stmt, __FILE__, __LINE__);                                                                   \
    } while (0)