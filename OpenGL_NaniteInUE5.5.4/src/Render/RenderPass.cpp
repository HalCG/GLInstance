#include "RenderPass.h"
#include "oglcontext.h"
#include "utils.h"

// ==============================================================================
// RenderPass — 单 Pass 资源绑定与 GPU 提交
//
// Compute：绑 UBO/SSBO/Image → glDispatchCompute → glMemoryBarrier
// Graphics：绑 UBO/SSBO → glDrawArraysIndirect（实例数由 GPU 写入 WorkArgs）
//
// 管线编排与 Pass 顺序见 scene.cpp::RenderOneFrame()、doc/Code_Reading_Guide.md
// ==============================================================================

// ==============================================================================
// 设置图形管线的顶点着色器和片段着色器 (Vertex & Fragment Shader)
// ==============================================================================
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

// ==============================================================================
// 设置计算管线的计算着色器 (Compute Shader)
// ==============================================================================
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

// ==============================================================================
// 绑定计算着色器读写用的图像资源 (Image)
// ==============================================================================
void RenderPass::SetComputeImage(int inBindingPoint, Texture2D* inImage, bool inIsOutputResource) {
	ShaderImageResource* shaderResource = new ShaderImageResource;
	shaderResource->mBinding = inBindingPoint;
	shaderResource->mTexture = inImage;
	mTextures.push_back(shaderResource);
	if (inIsOutputResource) {
		mOutputTextures.push_back(shaderResource);
	}
}

// ==============================================================================
// 绑定着色器存储缓冲区 (Shader Storage Buffer Object, SSBO)
// ==============================================================================
void RenderPass::SetSSBO(int inBindingPoint, GLuint inBuffer, bool inIsOutputResource) {
	ShaderBufferResource* shaderBufferResource = new ShaderBufferResource;
	shaderBufferResource->mBinding = inBindingPoint;
	shaderBufferResource->mBuffer = inBuffer;

	mBuffers.push_back(shaderBufferResource);
	if (inIsOutputResource) {
		mOutputBuffers.push_back(shaderBufferResource);
	}
}

// ==============================================================================
// 绑定统一变量缓冲区 (Uniform Buffer Object, UBO)
// ==============================================================================
void RenderPass::SetUniformBufferObject(int inBindingPoint, GLuint inBuffer) {
	ShaderBufferResource* shaderBufferResource = new ShaderBufferResource;
	shaderBufferResource->mBinding = inBindingPoint;
	shaderBufferResource->mBuffer = inBuffer;

	mUniformBuffers.push_back(shaderBufferResource);
}

// ==============================================================================
// 设置计算着色器的派发参数 (Dispatch Dimensions)
// ==============================================================================
void RenderPass::SetComputeDispatchArgs(int inX, int inY, int inZ) {
	mDispatchX = inX;
	mDispatchY = inY;
	mDispatchZ = inZ;
}

// ==============================================================================
// 构建 RenderPass，对于图形 Pass 需要记录视口尺寸
// ==============================================================================
void RenderPass::Build(uint32_t inCanvasWidth, uint32_t inCanvasHeight) {
	if (mRenderPassType == ERenderPassType::ERPT_COMPUTE) {
	}
	else {
		mViewportWidth = inCanvasWidth;
		mViewportHeight = inCanvasHeight;
	}
}

// ==============================================================================
// 执行当前 RenderPass (主要针对 Compute Pass 的资源绑定与线程组派发)
// ==============================================================================
void RenderPass::Execute() {
	if (mRenderPassType == ERenderPassType::ERPT_COMPUTE)
	{
		// 1. 在 RenderDoc 或 Nsight Graphics 调试器中压入当前 Pass 的事件标签名
		SCOPED_EVENT(mName.c_str());

		// 2. 激活并绑定当前的 Compute Shader Program
		OGL_CALL(glUseProgram(mComputeShader));

		// 3. 循环绑定所有依赖的 Uniform Buffer (UBO，如全局常量矩阵 GlobalConstants)
		int uboSlot = 0;
		for (auto shaderResource : mUniformBuffers) {
			// 将 Shader 中指定的 layout(binding = N) 映射到硬件 UBO 槽位 uboSlot
			OGL_CALL(glUniformBlockBinding(mComputeShader, shaderResource->mBinding, uboSlot));
			// 将 CPU/GPU 端的 UBO 缓冲句柄 mBuffer 挂载连接到对应的 uboSlot 槽位
			OGL_CALL(glBindBufferBase(GL_UNIFORM_BUFFER, uboSlot++, shaderResource->mBuffer));
		}

		// 4. 循环绑定所有依赖的 Shader Storage Buffer Object (SSBO，如 BVH、WorkArgs、VisBuffer 等)
		for (auto shaderResource : mBuffers) {
			// 直接将 SSBO 显存句柄挂载到 Shader 代码中 layout(binding = N) 指定的 N 号槽位
			OGL_CALL(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, shaderResource->mBinding, shaderResource->mBuffer));
		}

		// 5. 循环绑定所有依赖的 2D 图像纹理 (Image，如可视化输出纹理 VisualizationTexture)
		for (auto iter = mTextures.begin(); iter != mTextures.end(); ++iter) {
			ShaderImageResource* shaderResource = *iter;
			// 绑定 Image 纹理，开启 GL_WRITE_ONLY 写权限，供 Compute Shader 执行 imageStore 写入
			OGL_CALL(glBindImageTexture(shaderResource->mBinding, shaderResource->mTexture->mTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F));
		}

		// 6. 【触发 GPU 计算派发】：在 X, Y, Z 三个维度调度 mDispatchX * mDispatchY * mDispatchZ 个工作组并发执行
		OGL_CALL(glDispatchCompute(mDispatchX, mDispatchY, mDispatchZ));

		// 7. 善后处理：解绑所有 SSBO 槽位 (绑定 0)，防止影响后续 RenderPass
		for (auto shaderResource : mBuffers) {
			OGL_CALL(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, shaderResource->mBinding, 0));
		}

		// 8. 【核心 GPU 内存屏障】：强行插入全局屏障，保证 CS 写入 SSBO/Image 的数据对后续 Pass 完全可见
		glMemoryBarrier(GL_ALL_BARRIER_BITS);
		glFlush();
	}
}

// ==============================================================================
// 间接绘制执行 (针对 Graphics Pass 使用 GPU 生成的参数触发 DrawIndirect)
// ==============================================================================
void RenderPass::ExecuteIndirect(GLuint inIndirectBuffer) {
	// 1. 压入 RenderDoc 调试事件标签名
	SCOPED_EVENT(mName.c_str());

	// 2. 设置图形管线渲染视口大小与剔除卷绕方向
	glViewport(0, 0, mViewportWidth, mViewportHeight);
	glFrontFace(GL_CW);

	// 3. 激活图形管线 Program (包含 VS 顶点着色器 + FS 片段着色器)
	glUseProgram(mVSPShader);

	// 4. 绑定 UBO 常量缓冲
	int uboSlot = 0;
	for (auto shaderResource : mUniformBuffers) {
		OGL_CALL(glUniformBlockBinding(mVSPShader, shaderResource->mBinding, uboSlot));
		OGL_CALL(glBindBufferBase(GL_UNIFORM_BUFFER, uboSlot++, shaderResource->mBuffer));
	}

	// 5. 绑定 SSBO 数据缓冲 (如 NaniteMesh、VisiableClusterSWHW、VisBuffer64)
	for (auto shaderResource : mBuffers) {
		OGL_CALL(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, shaderResource->mBinding, shaderResource->mBuffer));
	}

	// 6. 【核心间接绘制】：绑定包含 DrawArraysIndirectCommand 结构的 GPU 缓冲区 (sWorkArgs[0])
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, inIndirectBuffer);

	// 7. 触发硬件间接绘制：CPU 零开销，全由 GPU 显存中的 instanceCount 决定绘制多少个 Cluster！
	glDrawArraysIndirect(GL_TRIANGLES, nullptr);

	// 8. 解绑 SSBO 槽位与内存屏障同步
	for (auto shaderResource : mBuffers) {
		OGL_CALL(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, shaderResource->mBinding, 0));
	}
	glMemoryBarrier(GL_ALL_BARRIER_BITS);
	glFlush();
}

// ==============================================================================
// 附录技术详解：OpenGL UBO 缓冲绑定原理（GLSL 写法差异与 C++ 两行 vs 单行代码对比）
// ==============================================================================
/*
 * 【核心直观对比：GLSL 声明方式决定了 C++ 端的绑定代码写法】
 *
 * 场景一：Shader 中【未写】layout(binding = N) (传统/旧版 GLSL 写法)
 * ------------------------------------------------------------------------------
 *  [GLSL 代码]：
 *      uniform GlobalConstants {   // ⚠️ 没有写 layout(binding = 0) !
 *          mat4 ViewMatrix;
 *          mat4 ProjectionMatrix;
 *      };
 *
 *  [C++ 绑定代码]：必须使用【两行代码】！
 *      // 1. 【第一行】：用 glUniformBlockBinding 将 Shader Block 显式指定映射到硬件 0 号 Slot
 *      glUniformBlockBinding(shaderProgram, blockBindingPoint, 0);
 *      // 2. 【第二行】：用 glBindBufferBase 将物理 UBO 显存句柄挂载连接到硬件 0 号 Slot
 *      glBindBufferBase(GL_UNIFORM_BUFFER, 0, uboBufferHandle);
 *
 *  [原理说明]：
 *      因为 Shader 中没有指定 Slot 槽位，驱动无法自动绑定，必须由 C++ 第一行代码在运行时指定。
 * ------------------------------------------------------------------------------
 *
 * 场景二：Shader 中【显式声明】layout(binding = N) (现代 GLSL 4.2+ 简化写法)
 * ------------------------------------------------------------------------------
 *  [GLSL 代码]：
 *      layout(binding = 0) uniform GlobalConstants {   // ✅ 显式指定 binding = 0 !
 *          mat4 ViewMatrix;
 *          mat4 ProjectionMatrix;
 *      };
 *
 *  [C++ 绑定代码]：可以简化为【单行代码】！
 *      // 【仅需单行】：直接将物理 UBO 显存句柄挂载到已在 Shader 中指定的 0 号 Slot
 *      glBindBufferBase(GL_UNIFORM_BUFFER, 0, uboBufferHandle);
 *
 *  [原理说明]：
 *      Shader 编译链接时，OpenGL 驱动会自动完成 Block 到 Slot 0 的映射（自动帮我们做好了第一行的事）。
 *      因此 C++ 端可以省略 `glUniformBlockBinding`。
 * ------------------------------------------------------------------------------
 *
 * 【对比总结表】
 * ----------------------------------------------------------------------------------------------
 *  场景类别      | GLSL 写法示例                              | C++ 绑定代码                      | 说明
 * ----------------------------------------------------------------------------------------------
 *  场景一 (传统) | uniform GlobalConstants { ... };           | 2行: glUniformBlockBinding +     | 必须由 C++ 显式分配
 *                | (未写 layout(binding=0))                   |      glBindBufferBase             | 硬件 Slot 槽位
 * ----------------------------------------------------------------------------------------------
 *  场景二 (现代) | layout(binding=0) uniform GlobalConstants; | 1行: glBindBufferBase (或继续写2行)| 驱动自动完成 Slot 映射，
 *                | (写了 layout(binding=0))                   |                                   | C++ 可省略第一行
 * ----------------------------------------------------------------------------------------------
 *
 * 【本项目（Nanite 实现）为什么统一采用【两行代码】形式？】
 *  1. 鲁棒性与超高兼容性：无论 Shader 中写没写 layout(binding=N)，两行代码在任何 OpenGL 环境下均 100% 正常运行。
 *  2. 展现底层双重映射模型：
 *     - 第一行：[Shader Uniform Block] ===(glUniformBlockBinding)===> [硬件 Slot 槽位]
 *     - 第二行：[硬件 Slot 槽位]       <===(glBindBufferBase)======= [物理显存 Buffer]
 *     理解此过程能帮助清晰区分 Uniform Block Index、Binding Slot ID 和 Buffer Handle 三者的本质差别。
 */