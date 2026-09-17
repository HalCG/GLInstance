#include "fullscreenquad.h"

// ==============================================================================
// 辅助函数：根据 VBO 和 VertexFactory 构建顶点数组对象 (VAO)
// ==============================================================================
GLuint BuildVAO(GLuint inVBO, int inAttributeCount, const char** inAttributeNames, int inVertexSizeInBytes) {
	int attributeComponentCount = 4;
	GLuint vao = 0;
	OGL_CALL(glGenVertexArrays(1, &vao));
	OGL_CALL(glBindVertexArray(vao));
	OGL_CALL(glBindBuffer(GL_ARRAY_BUFFER, inVBO));
	for (int i = 0; i < inAttributeCount; i++) {
		const char* attributeName = inAttributeNames[i];
		glEnableVertexAttribArray(i);
		glVertexAttribPointer(i, attributeComponentCount, GL_FLOAT, false, inVertexSizeInBytes, (void*)(sizeof(float) * attributeComponentCount * i));
	}
	OGL_CALL(glBindBuffer(GL_ARRAY_BUFFER, 0));
	OGL_CALL(glBindVertexArray(0));
	return vao;
}

// ==============================================================================
// 初始化全屏四边形，生成覆盖 [-1, 1] 范围的四个顶点，并构建相关的 VBO/VAO
// ==============================================================================
void FullScreenQuad::Init(){
	SetVertexCount(4);
	mPrimitiveType = GL_TRIANGLE_STRIP;
	SetPosition(0, -1.0f, -1.0f, 0.0f);
	SetPosition(1, 1.0f, -1.0f, 0.0f);
	SetPosition(2, -1.0f, 1.0f, 0.0f);
	SetPosition(3, 1.0f, 1.0f, 0.0f);
	SetTexcoord(0, 0.0f, 0.0f);
	SetTexcoord(1, 1.0f, 0.0f);
	SetTexcoord(2, 0.0f, 1.0f);
	SetTexcoord(3, 1.0f, 1.0f);
	mVBO = CreateBufferObject(GL_ARRAY_BUFFER, sizeof(StaticMeshVertexData) * mVertexCount, GL_DYNAMIC_DRAW, mVertices);
	mVertexFactory = GetVertexFactory();
	mVAO = BuildVAO(mVBO, mVertexFactory->mAttributeCount, mVertexFactory->mAttributes, mVertexFactory->mVertexSizeInBytes);
}