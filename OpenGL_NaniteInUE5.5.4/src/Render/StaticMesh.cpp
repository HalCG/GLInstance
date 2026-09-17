#include "StaticMesh.h"
#include "utils.h"

static const char* attributeNames[] = { "position","texcoord" };

// ==============================================================================
// 获取全局静态的 VertexFactory（单例），用于描述位置和纹理坐标的顶点布局
// ==============================================================================
const VertexFactory* GetVertexFactory() {
	static VertexFactory* sVertexFactory = nullptr;
	if (sVertexFactory == nullptr) {
		sVertexFactory = new VertexFactory(attributeNames, 2, sizeof(float) * 4 * 2);
	}
	return sVertexFactory;
}

// ==============================================================================
// 静态网格体构造函数
// ==============================================================================
StaticMesh::StaticMesh() {
	mbDrawWidthIBO = false;
	mPrimitiveType = GL_TRIANGLES;
}

// ==============================================================================
// 设置顶点总数，分配顶点数组内存
// ==============================================================================
void StaticMesh::SetVertexCount(int vertex_count) {
	mVertexCount = vertex_count;
	mVertices = new StaticMeshVertexData[vertex_count];
}

// ==============================================================================
// 设置指定索引的顶点位置数据
// ==============================================================================
void StaticMesh::SetPosition(int index, float x, float y, float z, float w) {
	mVertices[index].mPosition[0] = x;
	mVertices[index].mPosition[1] = y;
	mVertices[index].mPosition[2] = z;
	mVertices[index].mPosition[3] = w;
}

// ==============================================================================
// 设置指定索引的顶点纹理坐标数据
// ==============================================================================
void StaticMesh::SetTexcoord(int index, float x, float y, float z, float w) {
	mVertices[index].mTexcoord[0] = x;
	mVertices[index].mTexcoord[1] = y;
	mVertices[index].mTexcoord[2] = z;
	mVertices[index].mTexcoord[3] = w;
}

// ==============================================================================
// 提交数据：将 CPU 端的顶点数据更新至 GPU 的 Vertex Buffer Object (VBO)
// ==============================================================================
void StaticMesh::Submit() {
	UpdateBufferObject(mVBO, GL_ARRAY_BUFFER, mVertices, sizeof(StaticMeshVertexData)*mVertexCount, 0);
}

// ==============================================================================
// 发起绘制调用
// ==============================================================================
void StaticMesh::Draw() {
	OGL_CALL(glDrawArrays(mPrimitiveType, 0, mVertexCount));
}