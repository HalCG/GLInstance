#pragma once
#include "StaticMesh.h"

// ==============================================================================
// 全屏四边形类 (FullScreenQuad)，继承自 StaticMesh
// 用于最终合成或后处理，将渲染结果纹理贴到全屏幕上
// ==============================================================================
class FullScreenQuad :public StaticMesh {
public:
	void Init();
};