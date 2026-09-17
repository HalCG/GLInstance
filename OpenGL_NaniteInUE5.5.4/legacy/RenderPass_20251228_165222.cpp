#include "RenderPass.h"
#include "oglcontext.h"
#include "utils.h"
void RenderPass::SetVSPS(const char* inVSPath, const char* inFSPath) {
	size_t fileSize = 0;
	unsigned char* fileContent = LoadFileContent(inVSPath, fileSize);
	char* shaderCode = new char[fileSize + 1];
	memcpy(shaderCode, fileContent, fileSize);
	shaderCode[fileSize] = 0;
	GLuint vsshader = CompileShader(GL_VERTEX_SHADER, shaderCode);
	delete[]shaderCode;
	delete[]fileContent;
	fileContent = LoadFileContent(inFSPath, fileSize);
	shaderCode = new char[fileSize + 1];
	memcpy(shaderCode, fileContent, fileSize);
	shaderCode[fileSize] = 0;
	GLuint fsshader = CompileShader(GL_FRAGMENT_SHADER, shaderCode);
	delete[]shaderCode;
	mVSPShader = CreateProgram(vsshader,fsshader);
}
void RenderPass::SetCS(const char* inCSPath) {
	size_t fileSize = 0;
	unsigned char* fileContent = LoadFileContent(inCSPath, fileSize);
	char* shaderCode = new char[fileSize+1];
	memcpy(shaderCode, fileContent, fileSize);
	shaderCode[fileSize] = 0;
	GLuint shader = CompileShader(GL_COMPUTE_SHADER, shaderCode);
	mComputeShader = CreateProgram(shader);
	delete[]shaderCode;
	delete[]fileContent;
}
void RenderPass::SetComputeImage(int inBindingPoint, Texture2D* inImage, bool inIsOutputResource) {
	ShaderImageResource* shaderResource = new ShaderImageResource;
	shaderResource->mBinding = inBindingPoint;
	shaderResource->mTexture = inImage;
	mTextures.push_back(shaderResource);
	if (inIsOutputResource) {
		mOutputTextures.push_back(shaderResource);
	}
}
void RenderPass::SetSSBO(int inBindingPoint, GLuint inBuffer, bool inIsOutputResource) {
	ShaderBufferResource* shaderBufferResource = new ShaderBufferResource;
	shaderBufferResource->mBinding = inBindingPoint;
	shaderBufferResource->mBuffer = inBuffer;

	mBuffers.push_back(shaderBufferResource);
	if (inIsOutputResource) {
		mOutputBuffers.push_back(shaderBufferResource);
	}
}
void RenderPass::SetUniformBufferObject(int inBindingPoint, GLuint inBuffer) {
	ShaderBufferResource* shaderBufferResource = new ShaderBufferResource;
	shaderBufferResource->mBinding = inBindingPoint;
	shaderBufferResource->mBuffer = inBuffer;

	mUniformBuffers.push_back(shaderBufferResource);
}
void RenderPass::SetComputeDispatchArgs(int inX, int inY, int inZ) {
	mDispatchX = inX;
	mDispatchY = inY;
	mDispatchZ = inZ;
}
void RenderPass::Build(uint32_t inCanvasWidth, uint32_t inCanvasHeight) {
	if (mRenderPassType == ERenderPassType::ERPT_COMPUTE) {
	}
	else {
		mViewportWidth = inCanvasWidth;
		mViewportHeight = inCanvasHeight;
	}
}
void RenderPass::Execute() {
	if (mRenderPassType == ERenderPassType::ERPT_COMPUTE)
	{
		SCOPED_EVENT(mName.c_str());
		OGL_CALL(glUseProgram(mComputeShader));
		int uboSlot = 0;
		for (auto shaderResource : mUniformBuffers) {
			OGL_CALL(glUniformBlockBinding(mComputeShader, shaderResource->mBinding, uboSlot));
			OGL_CALL(glBindBufferBase(GL_UNIFORM_BUFFER, uboSlot++, shaderResource->mBuffer));
		}
		for (auto shaderResource : mBuffers) {
			OGL_CALL(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, shaderResource->mBinding, shaderResource->mBuffer));
		}
		for (auto iter = mTextures.begin(); iter != mTextures.end(); ++iter) {
			ShaderImageResource* shaderResource = *iter;
			OGL_CALL(glBindImageTexture(shaderResource->mBinding, shaderResource->mTexture->mTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F));
		}
		OGL_CALL(glDispatchCompute(mDispatchX, mDispatchY, mDispatchZ));
		for (auto shaderResource : mBuffers) {
			OGL_CALL(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, shaderResource->mBinding, 0));
		}
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
		glFinish();
	}
}
void RenderPass::ExecuteIndirect(GLuint inIndirectBuffer) {
	SCOPED_EVENT(mName.c_str());
	glViewport(0, 0, mViewportWidth, mViewportHeight);
	glFrontFace(GL_CW);
	glUseProgram(mVSPShader);
	int uboSlot = 0;
	for (auto shaderResource : mUniformBuffers) {
		OGL_CALL(glUniformBlockBinding(mVSPShader, shaderResource->mBinding, uboSlot));
		OGL_CALL(glBindBufferBase(GL_UNIFORM_BUFFER, uboSlot++, shaderResource->mBuffer));
	}
	for (auto shaderResource : mBuffers) {
		OGL_CALL(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, shaderResource->mBinding, shaderResource->mBuffer));
	}
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, inIndirectBuffer);
	glDrawArraysIndirect(GL_TRIANGLES, nullptr);
	for (auto shaderResource : mBuffers) {
		OGL_CALL(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, shaderResource->mBinding, 0));
	}
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
	glFinish();
}