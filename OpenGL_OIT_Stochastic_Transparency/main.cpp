/**
 * @file main.cpp
 * @brief Stochastic Transparency 随机透明度渲染入口
 *
 * 渲染流程见 StochasticTransparencyApp：
 * 1. 初始化 GLFW/GLAD 与着色器
 * 2. 加载模型与纹理资源
 * 3. 多 pass 随机深度与样本混合
 * 4. 输出到默认帧缓冲
 */

#include "StochasticTransparencyApp.hpp"

int main() {
  StochasticTransparencyApp app;
  if (!app.init()) {
    return -1;
  }
  app.run();
  app.shutdown();
  return 0;
}
