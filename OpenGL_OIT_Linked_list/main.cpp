/**
 * @file main.cpp
 * @brief OIT Linked List 透明渲染入口
 *
 * 渲染流程见 LinkedListOITApp：
 * 1. Pass 1: 渲染不透明物体到 opaqueFBO（Blinn-Phong 光照）
 * 2. Pass 2: 渲染透明物体，片段通过 SSBO 链表存储
 * 3. Pass 3: 遍历链表排序混合，合成到默认帧缓冲
 */

#include "LinkedListOITApp.hpp"

int main() {
    LinkedListOITApp app;
    if (!app.init()) {
        return -1;
    }
    app.run();
    app.shutdown();
    return 0;
}