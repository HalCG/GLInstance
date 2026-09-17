#pragma once
#include "oglcontext.h"
#include "utils.h"
struct StaticMeshVertexData {
	float mPosition[4];
	float mTexcoord[4];
};

// ==============================================================================
// 顶点工厂类：用于定义顶点的内存布局（包含哪些属性及总字节数）
// ==============================================================================
struct VertexFactory {
	const char** mAttributes;
	int mAttributeCount;
	int mVertexSizeInBytes;
	VertexFactory(const char** inAttributes, int inAttributeCount, int inVertexSize) {
		mAttributes = inAttributes;
		mAttributeCount = inAttributeCount;
		mVertexSizeInBytes = inVertexSize;
	}
};
const VertexFactory* GetVertexFactory();

// ==============================================================================
// 静态网格体类：提供基础的光栅化网格体支持
// ==============================================================================
class StaticMesh {
public:
	StaticMeshVertexData* mVertices;
	int mVertexCount;
	const VertexFactory* mVertexFactory;
	GLuint mVBO, mVAO;
	bool mbDrawWidthIBO;
	GLenum mPrimitiveType;
	StaticMesh();
	virtual void Init() {}
	void SetVertexCount(int vertex_count);
	void SetPosition(int index, float x, float y, float z, float w = 1.0f);
	void SetTexcoord(int index, float x, float y, float z=1.0f, float w = 1.0f);
	void Submit();
	virtual void Update(float delta) {}
	virtual void Draw();
};
