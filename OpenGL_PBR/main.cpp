#include "PbrApp.hpp"
#include <iostream>

/**
 * @brief OpenGL PBR 应用程序入口点
 */
int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    std::cout << "Starting OpenGL PBR Application..." << std::endl;

    // 创建 1280x720 分辨率窗口的 PBR 渲染应用程序实例
    PbrApp app(1280, 720, "OpenGL 4.3 PBR Renderer (Vulkan PBR Port)");
    if (!app.init()) {
        std::cerr << "Failed to initialize PBR Application!" << std::endl;
        return -1;
    }

    // 启动渲染与事件循环
    app.run();

    std::cout << "OpenGL PBR Application exited cleanly." << std::endl;
    return 0;
}

