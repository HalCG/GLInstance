#pragma once
// ==============================================================================
// 模块：Nanite 渲染场景（scene）
// 职责：对外暴露 Init / RenderOneFrame / 输入回调；内部编排 6-Pass GPU 管线
// 详述：doc/Code_Reading_Guide.md §4；实现见 scene.cpp
// 注意：管线细节与 SSBO 布局在 scene.cpp 文件头「管线速查」块
// ==============================================================================
void Init(int inWidth,int inHeight);
void RenderOneFrame(float inFrameTimeInSecond);
void OnKeyUp(unsigned int inKeyCode);

// 鼠标事件接口
void OnMousePress(int button, int action, int x, int y);
void OnMouseMove(int x, int y);
void OnMouseWheel(int delta);
