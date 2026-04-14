#pragma once
#include "oglcontext.h"
#include <string>
#include <vector>

// ==============================================================================
// 渲染阶段类型枚举
// ==============================================================================
enum class ERenderPassType {
	ERPT_GRAPHICS, // 图形渲染管线阶段 (包含 VS/FS Vertex/Fragment Shader)
	ERPT_COMPUTE   // 计算渲染管线阶段 (包含 CS Compute Shader)
};

// ==============================================================================
// 着色器缓冲区资源绑定结构 (Shader Buffer Resource)
// 用于记录显存缓冲区 (SSBO 或 UBO) 挂载到 Shader 中哪一个 layout(binding = N) 槽位
// ==============================================================================
struct ShaderBufferResource {
	GLuint mBuffer; // GPU 显存缓冲区的 OpenGL 句柄 ID (由 glGenBuffers 或 CreateBufferObject 生成)
	int mBinding;   // 着色器代码中的 Binding 槽位编号 (与 GLSL 中的 layout(binding = N) 一一对应)
};

// ==============================================================================
// 着色器图像资源绑定结构 (Shader Image Resource)
// 用于记录 2D 图像纹理 (Image2D) 挂载到 Compute Shader 中哪一个 layout(binding = N) 槽位
// ==============================================================================
struct ShaderImageResource {
	Texture2D* mTexture; // CPU 端封装的 2D 纹理对象指针
	int mBinding;        // 着色器代码中的 Image 槽位编号 (对应 GLSL layout(binding = N, rgba32f) uniform image2D)
};

// ==============================================================================
// 渲染管线封装类 (RenderPass)
// 统一抽象与管理一个独立的 Compute Pass 或 Graphics Pass 的 Shader 编译、资源绑定与派发执行
// ==============================================================================
class RenderPass {
public:
	ERenderPassType mRenderPassType; // 当前 Pass 的类型 (Graphics 或 Compute)
	union {
		GLuint mVSPShader;     // 图形管线的 Shader Program (包含 VS + FS)
		GLuint mComputeShader;  // 计算管线的 Shader Program (包含 CS)
	};
	std::vector<ShaderBufferResource*> mBuffers, mOutputBuffers, mUniformBuffers; // SSBO 与 UBO 资源列表
	std::vector<ShaderImageResource*> mTextures, mOutputTextures;                  // Image 图像资源列表
	int mDispatchX, mDispatchY, mDispatchZ;                                        // Compute Shader 的线程组派发尺寸 (X, Y, Z)
	uint32_t mViewportWidth, mViewportHeight;                                      // 图形管线渲染视口的宽度与高度
	std::string mName;                                                             // RenderPass 的调试名称 (用于 RenderDoc 或 ScopedEvent)

	RenderPass(ERenderPassType inRPT, const char* inName)
		: mRenderPassType(inRPT), mName(inName), mDispatchX(1), mDispatchY(1), mDispatchZ(1), mViewportWidth(0u), mViewportHeight(0u) {}

	// 设置顶点和片段着色器 (Graphics Pass)
	void SetVSPS(const char* inVSPath, const char* inFSPath);
	// 设置 Uniform Buffer (UBO)
	void SetUniformBufferObject(int inBindingPoint, GLuint inUBO);
	// 设置计算着色器 (Compute Pass)
	void SetCS(const char* inCSPath);
	// 设置 Image 资源 (用于 Compute Shader 读写图像)
	void SetComputeImage(int inBindingPoint, Texture2D* inImage, bool inIsOutputResource = false);
	// 设置 Shader Storage Buffer Object (SSBO)
	void SetSSBO(int inBindingPoint, GLuint inBuffer, bool inIsOutputResource = false);
	// 设置 Compute Shader 调度的线程组参数
	void SetComputeDispatchArgs(int inX, int inY, int inZ);

	// 构建 RenderPass (编译着色器并准备状态)
	void Build(uint32_t inCanvasWidth = 0u, uint32_t inCanvasHeight = 0u);
	// 执行 RenderPass (直接 Dispatch 或 Draw)
	void Execute();
	// 执行基于硬件的间接绘制 (Indirect Draw)
	void ExecuteIndirect(GLuint inIndirectBuffer);
};