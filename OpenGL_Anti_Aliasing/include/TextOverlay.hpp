#ifndef TEXT_OVERLAY_HPP
#define TEXT_OVERLAY_HPP

#include <glad/glad.h>
#include <string>

class TextOverlay {
public:
    bool init();
    void shutdown();
    void draw(int screenWidth, int screenHeight) const;

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint shader_ = 0;
};

#endif
