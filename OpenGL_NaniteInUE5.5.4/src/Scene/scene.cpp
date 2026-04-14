#include "scene.h"
#include "oglcontext.h"
#include "matrix4.h"
#include "quaternion.h"
#include <stdio.h>
#include <stdint.h>
#include "utils.h"
#include "RenderPass.h"
#include "fullscreenquad.h"
#include "trackball_camera.h"

#define _4MB 4194304
static int sCanvasWidth, sCanvasHeight;

// ==============================================================================
// 管线速查（详细说明见 doc/Code_Reading_Guide.md）
//
//  GPU 缓冲：
//    sWorkArgs[2]                      — Ping-Pong 调度 + DrawIndirect 参数
//    sMainAndPostNodeAndClusterBatches — 节点栈 [0..1023] + 候选 Cluster [1024..]
//    sVisiableClusterSWHW              — Pass 3 输出的最终可见 Cluster 列表
//    sVisBuffer64                      — Pass 4 输出的 64 位可见性缓冲
//
//  Pass 顺序（RenderOneFrame）：
//    RasterClear → NodeAndClusterCull×4 → ClusterCull → HWRasterize → Visualization → FSQ
// ==============================================================================
static int sCurrentMipLevelIndex = 9;
static int sAvaliableMipLevels[] = {0,1,2,3,4,5,6,7,8,10};
matrix4 sProjectionMatrix, sViewMatrix, sModelMatrix;
// 相机初始位置（仅 Trackball 初始化用，≠ Shader 里的 CameraPositionWS UBO 字段）
float4 sCameraPositionWS(-330.0f,330.0f,-330.0f), sCameraTargetPositionWS(0.0f,80.0f,0.0f);
TrackballCamera sCamera;
// CPU 端 GlobalConstants 镜像；上传后 Shader binding=0 读取 CameraPositionWS.w=LODScale 等
GlobalConstants sGlobalConstantsData;
GLuint sFSQShader;
FullScreenQuad sFSQMesh;
Texture2D* sVisualizationTexture;
GLuint sVisBuffer64,sWorkArgs[2],sGlobalConstants,sBVH,sNaniteMesh,sVisiableClusterSWHW,sMainAndPostNodeAndClusterBatches;
RenderPass *sVisualizationPass,*sHWRasterizePass, * sRasterClearPass, * sNodeAndClusterCullPasses[4], * sClusterCullPass;

// ==============================================================================
// Init — 创建 Nanite 渲染所需的 GPU 资源与各 RenderPass
//
// 1. 相机 / 投影 / GlobalConstants（与 Shader binding=0 布局一致，见 oglcontext.h）
// 2. 加载 Res/mitsuba.bvh、Res/mitsuba.nanitemesh 到 SSBO
// 3. 分配 WorkArgs、Batches、VisBuffer、VisiableClusterSWHW
// 4. 构建 6 阶段 Pass 并绑定 UBO/SSBO（绑定表见 doc/Code_Reading_Guide.md §3.1）
// ==============================================================================
void Init(int inCanvasWidth, int inCanvasHeight) {
	sCanvasWidth = inCanvasWidth;
	sCanvasHeight = inCanvasHeight;
	sProjectionMatrix.Perspective(90.0f, float(inCanvasWidth) / float(inCanvasHeight), 1.0f, 10000.0f);
	sCamera.Init(sCameraPositionWS, sCameraTargetPositionWS);
	sViewMatrix = sCamera.GetViewMatrix();
	{
		matrix3 scaleMatrix;
		scaleMatrix.LoadIdentity();
		matrix3 lt3x3 = scaleMatrix * quaternion(180.0f, 0.0f, 0.0f).toMatrix3();
		sModelMatrix.LoadIdentity();
		sModelMatrix.SetLeftTop3x3(lt3x3);

		sGlobalConstantsData.SetProjectionMatrix(sProjectionMatrix.v);
		sGlobalConstantsData.SetViewMatrix(sViewMatrix.v);
		sGlobalConstantsData.SetModelMatrix(sModelMatrix.v);

		// ViewToPixels：当前 FOV+分辨率下，世界 1 单位 ≈ 多少屏幕像素（LOD 阈值换算用）
		const float ViewToPixels = 0.5f * sProjectionMatrix.v[5] * float(inCanvasHeight);
		const float LODScale = ViewToPixels / 1.0f;     // 实际参与 Pass2/3 LOD 误差比较
		const float LODScaleHW = ViewToPixels / 32.0f;  // 仅对齐 UE 布局，本 Demo 无 SW/HW 分流
		float4 eye = sCamera.GetEye();
		float4 fwd = sCamera.GetForward();
		sGlobalConstantsData.SetCameraPositionWS(eye.x, eye.y, eye.z, LODScale);
		sGlobalConstantsData.SetCameraViewDirectionWS(fwd.x, fwd.y, fwd.z, LODScaleHW);
	}
	sGlobalConstantsData.SetMisc0(sAvaliableMipLevels[sCurrentMipLevelIndex], 0, 0, 0);
	sVisualizationTexture = CreateTexture2D(nullptr, inCanvasWidth, inCanvasHeight, GL_RGBA32F, GL_RGBA);
	SetObjectName(GL_TEXTURE, sVisualizationTexture->mTexture, "VisualizationTexture");
	sGlobalConstants = CreateBufferObject(GL_UNIFORM_BUFFER, 4096, GL_STATIC_DRAW, nullptr);
	SetObjectName(GL_BUFFER, sGlobalConstants, "GlobalConstants");
	sVisiableClusterSWHW = CreateBufferObject(GL_SHADER_STORAGE_BUFFER, _4MB, GL_STATIC_DRAW, nullptr);
	SetObjectName(GL_BUFFER, sVisiableClusterSWHW, "VisiableClusterSWHW");
	{
		size_t fileSize = 0;
		unsigned char* fileContent = LoadFileContent("Res/mitsuba.bvh", fileSize);
		sBVH = CreateBufferObject(GL_SHADER_STORAGE_BUFFER, fileSize, GL_STATIC_DRAW, fileContent);
		SetObjectName(GL_BUFFER, sBVH, "HierarchyBuffer");
		delete[] fileContent;
		fileContent = LoadFileContent("Res/mitsuba.nanitemesh", fileSize);
		sNaniteMesh = CreateBufferObject(GL_SHADER_STORAGE_BUFFER, fileSize, GL_STATIC_DRAW, fileContent);
		SetObjectName(GL_BUFFER, sNaniteMesh, "NaniteMesh");
		delete[] fileContent;
	}
	sWorkArgs[0] = CreateBufferObject(GL_SHADER_STORAGE_BUFFER , _4MB, GL_STATIC_DRAW, nullptr);
	SetObjectName(GL_BUFFER, sWorkArgs[0], "WorkArgs[0]");
	sWorkArgs[1] = CreateBufferObject(GL_SHADER_STORAGE_BUFFER, _4MB, GL_STATIC_DRAW, nullptr);
	SetObjectName(GL_BUFFER, sWorkArgs[1], "WorkArgs[1]");
	sMainAndPostNodeAndClusterBatches = CreateBufferObject(GL_SHADER_STORAGE_BUFFER, _4MB, GL_STATIC_DRAW, nullptr);
	SetObjectName(GL_BUFFER, sMainAndPostNodeAndClusterBatches, "MainAndPostNodeAndClusterBatches");
	sVisBuffer64 = CreateBufferObject(GL_SHADER_STORAGE_BUFFER, inCanvasWidth * inCanvasHeight * sizeof(uint64_t), GL_STATIC_DRAW, nullptr);
	SetObjectName(GL_BUFFER, sVisBuffer64, "VisBuffer64");
	
	// ==========================================
	// 创建各 RenderPass（binding 表见 doc/Code_Reading_Guide.md §3.1）
	// ==========================================
	{
		sRasterClearPass = new RenderPass(ERenderPassType::ERPT_COMPUTE, "RasterClear");
		sRasterClearPass->SetSSBO(0, sWorkArgs[0]);
		sRasterClearPass->SetSSBO(1, sWorkArgs[1]);
		sRasterClearPass->SetSSBO(2, sVisBuffer64);
		sRasterClearPass->SetCS("Res/Shaders/RasterClear.glsl");
		sRasterClearPass->SetComputeDispatchArgs(
			int(ceilf(float(inCanvasWidth) / 8.0f)), 
			int(ceilf(float(inCanvasHeight) / 8.0f)),
			1);
		sRasterClearPass->Build();
	}
	for(int i=0;i<4;i++)
	{
		char szName[128] = {0};
		sprintf_s(szName,"NodeAndClusterCull_%d",i);
		int currentWorkArgsIndex = i % 2;
		int nextWorkArgsIndex = (i + 1) % 2;
		sNodeAndClusterCullPasses[i] = new RenderPass(ERenderPassType::ERPT_COMPUTE, szName);
		sNodeAndClusterCullPasses[i]->SetUniformBufferObject(0, sGlobalConstants);
		sNodeAndClusterCullPasses[i]->SetSSBO(1, sBVH);

		sNodeAndClusterCullPasses[i]->SetSSBO(2, sWorkArgs[currentWorkArgsIndex]);
		sNodeAndClusterCullPasses[i]->SetSSBO(3, sWorkArgs[nextWorkArgsIndex]);

		sNodeAndClusterCullPasses[i]->SetSSBO(4, sMainAndPostNodeAndClusterBatches);
		sNodeAndClusterCullPasses[i]->SetCS("Res/Shaders/NodeAndClusterCull.glsl");
		sNodeAndClusterCullPasses[i]->SetComputeDispatchArgs(1, 1, 1);
		sNodeAndClusterCullPasses[i]->Build();
	}
	{
		sClusterCullPass = new RenderPass(ERenderPassType::ERPT_COMPUTE, "ClusterCull");
		sClusterCullPass->SetUniformBufferObject(0, sGlobalConstants);
		sClusterCullPass->SetSSBO(1, sNaniteMesh);
		sClusterCullPass->SetSSBO(2, sWorkArgs[0]);
		sClusterCullPass->SetSSBO(3, sVisiableClusterSWHW);
		sClusterCullPass->SetSSBO(4, sMainAndPostNodeAndClusterBatches);
		sClusterCullPass->SetCS("Res/Shaders/ClusterCull.glsl");
		sClusterCullPass->SetComputeDispatchArgs(1, 1, 1);
		sClusterCullPass->Build();
	}
	{
		sVisualizationPass = new RenderPass(ERenderPassType::ERPT_COMPUTE, "Visualization");
		sVisualizationPass->SetSSBO(0, sVisBuffer64);
		sVisualizationPass->SetComputeImage(0, sVisualizationTexture, true);
		sVisualizationPass->SetCS("Res/Shaders/Visualization.glsl");
		sVisualizationPass->SetComputeDispatchArgs(
			int(ceilf(float(inCanvasWidth) / 8.0f)),
			int(ceilf(float(inCanvasHeight) / 8.0f)),
			1);
		sVisualizationPass->Build();
	}
	{
		sHWRasterizePass = new RenderPass(ERenderPassType::ERPT_GRAPHICS, "HWRasterize");
		sHWRasterizePass->SetVSPS("Res/Shaders/HWRasterizeVS.glsl", "Res/Shaders/HWRasterizeFS.glsl");
		sHWRasterizePass->SetUniformBufferObject(0, sGlobalConstants);
		sHWRasterizePass->SetSSBO(1, sNaniteMesh);
		sHWRasterizePass->SetSSBO(2, sVisiableClusterSWHW);
		sHWRasterizePass->SetSSBO(3, sVisBuffer64);
		sHWRasterizePass->Build(inCanvasWidth,inCanvasHeight);
	}
	sFSQMesh.Init();
	{
		size_t fileSize = 0;
		unsigned char* fileContent = LoadFileContent("Res/Shaders/fsqVS.glsl", fileSize);
		char* shaderCode = new char[fileSize+1];
		memcpy(shaderCode, fileContent, fileSize);
		shaderCode[fileSize] = 0;
		GLuint vsShader = CompileShader(GL_VERTEX_SHADER, shaderCode);
		delete[] shaderCode;
		delete[] fileContent;
		fileContent = LoadFileContent("Res/Shaders/fsqFS.glsl", fileSize);
		shaderCode = new char[fileSize + 1];
		memcpy(shaderCode, fileContent, fileSize);
		shaderCode[fileSize] = 0;
		GLuint fsShader = CompileShader(GL_FRAGMENT_SHADER, shaderCode);
		delete[] shaderCode;
		delete[] fileContent;
		sFSQShader = CreateProgram(vsShader, fsShader);
	}
}
void RenderOneFrame(float inFrameTimeInSecond) {
	// ==============================================================================
	// 【管线阶段 0：更新每帧全局 Uniform 变量与相机视角矩阵】
	// 从 Trackball 交互相机中提取最新的 ViewMatrix、Eye Position 和 Forward Direction，
	// 并计算 LODScale（Pass2/3 实际使用）与 LODScaleHW（仅 UBO 布局占位）。
	// ==============================================================================
	sViewMatrix = sCamera.GetViewMatrix();
	sGlobalConstantsData.SetViewMatrix(sViewMatrix.v);

	// --- LOD 屏幕尺度（每帧更新，随窗口高度与 FOV 变化）---
	// ViewToPixels：世界空间 1 单位长度在屏幕上约等于多少像素
	//   Proj.v[5] = cot(FOV/2)，* 0.5 * 屏高 = 与透视投影一致的像素尺度
	const float ViewToPixels = 0.5f * sProjectionMatrix.v[5] * float(sCanvasHeight);
	const float LODScale = ViewToPixels / 1.0f;     // → CameraPositionWS.w，Pass2/3 LOD 判据用
	const float LODScaleHW = ViewToPixels / 32.0f;  // → ViewDirectionWS.w，布局占位，Shader 未参与判据
	float4 eye = sCamera.GetEye();
	float4 fwd = sCamera.GetForward();
	sGlobalConstantsData.SetCameraPositionWS(eye.x, eye.y, eye.z, LODScale);
	sGlobalConstantsData.SetCameraViewDirectionWS(fwd.x, fwd.y, fwd.z, LODScaleHW);

	// 将最新常量数据上传更新至 GPU Uniform Buffer (sGlobalConstants)
	UpdateBufferObject(sGlobalConstants, GL_UNIFORM_BUFFER, &sGlobalConstantsData, sizeof(GlobalConstants), 0);
	SCOPED_EVENT("Scene");

	// ==============================================================================
	// 【管线阶段 1：重置状态 Pass (RasterClear.glsl)】
	// - 清空 VisBuffer64（每个像素填充 0xFFFFFFFF00000000 即背景最大深度）
	// - 初始化 sWorkArgs[0] 与 sWorkArgs[1]（设置根节点 offset=0, count=1，准备开始遍历 BVH 树）
	// ==============================================================================
	sRasterClearPass->Execute();
	
	// ==============================================================================
	// 【管线阶段 2：BVH 树与节点剔除 Pass (NodeAndClusterCull.glsl x 4次 Ping-Pong)】
	// 详解（为何 4 次、Ping-Pong 读写表、与 ClusterCull 分工）：doc/Code_Reading_Guide.md §4.5
	// ==============================================================================
	for (int i = 0; i < 4; i++) {
		sNodeAndClusterCullPasses[i]->Execute();
	}
	
	// ==============================================================================
	// 【管线阶段 3：簇选择与可见性剔除 Pass (ClusterCull.glsl)】
	// - 从 sMainAndPostNodeAndClusterBatches[1024..] 中读取收集到的叶子 Cluster
	// - 再次根据 LOD 屏幕误差和簇边缘长度测试筛选出最终可见的 Cluster
	// - 将可见 Cluster 写入 sVisiableClusterSWHW 缓冲区
	// - 将通过筛选的 Cluster 数量写入 sWorkArgs[0].mData[1] (即 DrawIndirect 的 instanceCount)
	// ==============================================================================
	sClusterCullPass->Execute();
	
	// ==============================================================================
	// 【管线阶段 4：硬件光栅化间接绘制 Pass (HWRasterizeVS.glsl + HWRasterizeFS.glsl)】
	// - 根据 GPU 生成的 sWorkArgs[0] (DrawArraysIndirectCommand: count=384, instanceCount=可见簇数量)
	// - 触发 glDrawArraysIndirect 批量绘制所有可见 Cluster（每个 Instance 代表一个 128 三角形簇）
	// - 片段着色器使用 64位 atomicMin 写入 VisBuffer64（高32位=深度，低32位=PageID+ClusterID）
	// ==============================================================================
	sHWRasterizePass->ExecuteIndirect(sWorkArgs[0]);
	
	// ==============================================================================
	// 【管线阶段 5：VisBuffer 结果解析与伪彩可视化 Pass (Visualization.glsl)】
	// - Compute Shader 逐像素读取 VisBuffer64[pixelIndex]
	// - 解码出低 32 位的 Cluster 索引并用 Hash 算法生成独一无二的 RGB 伪彩
	// - 将 RGB 颜色写入 2D 纹理 sVisualizationTexture (imageStore)
	// ==============================================================================
	sVisualizationPass->Execute();

	// ==============================================================================
	// 【管线阶段 6：全屏四边形 FSQ 显示 Pass (fsqVS.glsl + fsqFS.glsl)】
	// - 将 sVisualizationTexture 纹理贴图渲染到屏幕 BackBuffer 供用户查看
	// ==============================================================================
	OGL_CALL(glViewport(0,0,sCanvasWidth, sCanvasHeight));
	OGL_CALL(glScissor(0,0,sCanvasWidth, sCanvasHeight));
	OGL_CALL(glClearColor(0.1f, 0.4f, 0.6f, 1.0f));
	OGL_CALL(glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT));
	OGL_CALL(glFrontFace(GL_CCW));
	OGL_CALL(glUseProgram(sFSQShader));
	OGL_CALL(glBindVertexArray(sFSQMesh.mVAO));
	OGL_CALL(glActiveTexture(GL_TEXTURE0));
	OGL_CALL(glBindTexture(GL_TEXTURE_2D, sVisualizationTexture->mTexture));
	OGL_CALL(glUniform1i(0, 0));
	OGL_CALL(glDrawArrays(GL_TRIANGLE_STRIP, 0, 4));
	OGL_CALL(glUseProgram(0));
}

// ==============================================================================
// 键盘输入响应函数：切换 LOD 等级（方向键 上/下）
// ==============================================================================
void OnKeyUp(unsigned int inKeyCode) {
	if (inKeyCode == VK_UP) {
		sCurrentMipLevelIndex++;
		if (sCurrentMipLevelIndex == _countof(sAvaliableMipLevels)) {
			sCurrentMipLevelIndex = 0;
		}
		sGlobalConstantsData.SetMisc0(sAvaliableMipLevels[sCurrentMipLevelIndex], 0, 0, 0);
	}
	else if (inKeyCode == VK_DOWN) {
		sCurrentMipLevelIndex--;
		if (sCurrentMipLevelIndex < 0) {
			sCurrentMipLevelIndex = _countof(sAvaliableMipLevels) - 1;
		}
		sGlobalConstantsData.SetMisc0(sAvaliableMipLevels[sCurrentMipLevelIndex], 0, 0, 0);
	}
}

// ==============================================================================
// 鼠标按键按下 / 抬起处理
// button: 0=左键(旋转), 1=中键(平移), 2=右键(缩放)
// action: 1=按下, 0=抬起
// ==============================================================================
void OnMousePress(int button, int action, int x, int y) {
	if (action == 1) { // 鼠标按键按下
		CameraDragMode mode = CameraDragMode::None;
		if (button == 0)      mode = CameraDragMode::Rotate; // 左键：旋转
		else if (button == 1) mode = CameraDragMode::Pan;    // 中键：平移
		else if (button == 2) mode = CameraDragMode::Dolly;  // 右键：推拉缩放
		sCamera.BeginDrag(mode, x, y);
	}
	else { // 鼠标按键抬起
		sCamera.EndDrag();
	}
}

// ==============================================================================
// 鼠标移动处理：应用光标 Delta 偏移计算视角旋转/平移
// ==============================================================================
void OnMouseMove(int x, int y) {
	if (sCamera.IsDragging()) {
		sCamera.ApplyCursorDelta(x, y, sCanvasWidth, sCanvasHeight);
	}
}

// ==============================================================================
// 鼠标滚轮处理：应用视距缩放
// ==============================================================================
void OnMouseWheel(int delta) {
	// Win32 标准 WHEEL_DELTA 为 120
	sCamera.ApplyScroll(delta / 120.0f);
}