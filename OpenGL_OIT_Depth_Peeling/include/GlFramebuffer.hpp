#ifndef GL_FRAMEBUFFER_HPP
#define GL_FRAMEBUFFER_HPP

#include <glad/glad.h>
#include <iostream>

/** 2D 纹理封装 */
class GlTexture2D {
public:
  GLuint id = 0;

  void createColorHDR(int width, int height) {
    destroy();
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA,
                 GL_HALF_FLOAT, nullptr);
    setClampLinear();
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  void createDepth32F(int width, int height) {
    destroy();
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, width, height, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  void destroy() {
    if (id != 0) {
      glDeleteTextures(1, &id);
      id = 0;
    }
  }

private:
  void setClampLinear() {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
};

/** 颜色 + 深度双附件 FBO */
class GlFramebuffer {
public:
  GLuint fbo = 0;
  GlTexture2D color;
  GlTexture2D depth;

  void create(int width, int height, const char *debugName) {
    destroy();
    color.createColorHDR(width, height);
    depth.createDepth32F(width, height);

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, color.id, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                           depth.id, 0);

    const GLenum bufs[] = {GL_COLOR_ATTACHMENT0, GL_DEPTH_ATTACHMENT};
    glDrawBuffers(2, bufs);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
        GL_FRAMEBUFFER_COMPLETE) {
      std::cout << "ERROR::FRAMEBUFFER:: " << debugName
                << " framebuffer is not complete!" << std::endl;
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  void bindColorDepth(GLuint colorTex, GLuint depthTex) const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           colorTex, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                           depthTex, 0);
  }

  void destroy() {
    if (fbo != 0) {
      glDeleteFramebuffers(1, &fbo);
      fbo = 0;
    }
    color.destroy();
    depth.destroy();
  }
};

#endif
