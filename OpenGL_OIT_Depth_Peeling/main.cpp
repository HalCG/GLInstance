/**
 * @file main.cpp
 * @brief Depth Peeling 透明排序渲染入口
 *
 * 渲染流程见 DepthPeelingApp：
 * 1. 初始化双 FBO（累积 / 剥离）
 * 2. 多 pass 深度剥离并 front-to-back 混合
 * 3. 全屏着色器合成到默认帧缓冲
 */

#include "DepthPeelingApp.hpp"

int main() {
  DepthPeelingApp app;
  if (!app.init()) {
    return -1;
  }
  app.run();
  app.shutdown();
  return 0;
}
