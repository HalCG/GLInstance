// ==============================================================================
// 模块：应用程序入口（Win32 + WGL）
// 职责：创建 OpenGL 窗口与 Core Profile 上下文；主循环调用 scene::RenderOneFrame
// 详述：doc/Code_Reading_Guide.md §1.1（CPU 每帧三件事中的窗口与循环部分）
// 注意：Nanite 管线本身在 scene.cpp；本文件不含 Pass 逻辑
// ==============================================================================
#include "utils.h"
#include "oglcontext.h"
#include "scene.h"
#if _DEBUG
#pragma comment( linker, "/subsystem:\"console\" /entry:\"WinMainCRTStartup\"")
#else
#endif

// ==============================================================================
// 创建并返回高版本(Core Profile)及多重采样(MSAA)的 OpenGL 渲染上下文 (HGLRC)
// ==============================================================================
HGLRC CreateNBRC(HDC dc)
{
	HGLRC rc = nullptr;
	// WGL 像素格式扩展属性数组 (用于 wglChoosePixelFormatARB 查询显卡支持的合适像素格式)
	GLint attribs[]{
		WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,            // 属性：允许绘制到 Win32 窗口中 (而不是内存位图)
		WGL_ACCELERATION_ARB, WGL_FULL_ACCELERATION_ARB, // 属性：必须使用 GPU 硬件全加速 (禁止 CPU 软渲染)
		WGL_DOUBLE_BUFFER_ARB, GL_TRUE,            // 属性：开启双缓冲区 (前台/后台缓冲，防止渲染画面闪烁撕裂)
		WGL_RED_BITS_ARB, 8,                        // 属性：RGBA 颜色缓冲区中红色通道为 8 位 (256色阶)
		WGL_GREEN_BITS_ARB, 8,                      // 属性：RGBA 颜色缓冲区中绿色通道为 8 位
		WGL_BLUE_BITS_ARB, 8,                       // 属性：RGBA 颜色缓冲区中蓝色通道为 8 位
		WGL_ALPHA_BITS_ARB, 8,                      // 属性：RGBA 颜色缓冲区中 Alpha 通道为 8 位 (合计 32 位真彩色)
		WGL_DEPTH_BITS_ARB, 24,                     // 属性：深度缓冲区 (Z-Buffer) 为 24 位精度
		WGL_STENCIL_BITS_ARB, 8,                    // 属性：模板缓冲区 (Stencil Buffer) 为 8 位精度
		WGL_SAMPLE_BUFFERS_ARB, GL_TRUE,            // 属性：开启多重采样抗锯齿缓冲区 (MSAA Buffer)
		WGL_SAMPLES_ARB, 1,                         // 属性：多重采样数量为 1 (1x MSAA)
		NULL, NULL                                  // 结尾标记：WGL 属性列表必须以 0, 0 (NULL, NULL) 键值对终止
	};
	int pixelFormat[256] = { 0 };
	UINT formatNum = 0;
	wglChoosePixelFormatARB(dc, attribs, NULL, 256, pixelFormat, &formatNum);
	printf("support format nb format num : [%u]\n", formatNum);
	if (formatNum > 0)
	{
		PIXELFORMATDESCRIPTOR pfd;
		DescribePixelFormat(dc, pixelFormat[0], sizeof(pfd), &pfd);
		SetPixelFormat(dc, pixelFormat[0], &pfd);

		int contexAttributes[] = {
			WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
			0 
		};
		rc = wglCreateContextAttribsARB(dc, nullptr, contexAttributes);
	}
	return rc;
}
// ==============================================================================
// 窗口消息回调函数，负责处理输入和退出事件
// ==============================================================================
LRESULT CALLBACK GLWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
	case WM_CLOSE:
		// 窗口关闭消息：向消息队列发送退出指令中断主循环
		PostQuitMessage(0);
		return 0;
	case WM_KEYUP:
		// 键盘按键抬起事件：用于响应方向键 (上/下) 切换 LOD 细节等级
		OnKeyUp(wParam);
		break;
	case WM_LBUTTONDOWN:
		// 鼠标左键按下：进入旋转 (Rotate) 拖拽模式，从 lParam 中提取 (x, y) 坐标
		OnMousePress(0, 1, (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam));
		break;
	case WM_LBUTTONUP:
		// 鼠标左键抬起：结束拖拽
		OnMousePress(0, 0, (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam));
		break;
	case WM_MBUTTONDOWN:
		// 鼠标中键按下：进入平移 (Pan) 拖拽模式
		OnMousePress(1, 1, (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam));
		break;
	case WM_MBUTTONUP:
		// 鼠标中键抬起：结束拖拽
		OnMousePress(1, 0, (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam));
		break;
	case WM_RBUTTONDOWN:
		// 鼠标右键按下：进入缩放 (Dolly) 拖拽模式
		OnMousePress(2, 1, (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam));
		break;
	case WM_RBUTTONUP:
		// 鼠标右键抬起：结束拖拽
		OnMousePress(2, 0, (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam));
		break;
	case WM_MOUSEMOVE:
		// 鼠标移动事件：更新相机视角旋转/平移/缩放
		OnMouseMove((int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam));
		break;
	case WM_MOUSEWHEEL:
		// 鼠标滚轮滚动事件：应用视距推拉缩放
		OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
		break;
	}
	return DefWindowProc(hwnd, msg, wParam, lParam);
}
// ==============================================================================
// Win32 应用程序入口，创建 OpenGL 窗口，注册鼠标交互回调，并开启渲染循环
// ==============================================================================
INT WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	// 1. 注册 Win32 窗口类 (GLWindow)
	WNDCLASSEX wndclass;
	wndclass.cbClsExtra = 0;
	wndclass.cbSize = sizeof(WNDCLASSEX);
	wndclass.cbWndExtra = 0;
	wndclass.hbrBackground = NULL;
	wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
	wndclass.hIcon = NULL;
	wndclass.hIconSm = NULL;
	wndclass.hInstance = hInstance;
	wndclass.lpfnWndProc = GLWindowProc;
	wndclass.lpszClassName = L"GLWindow";
	wndclass.lpszMenuName = NULL;
	wndclass.style = CS_VREDRAW | CS_HREDRAW;
	ATOM atom = RegisterClassEx(&wndclass);
	if (!atom) {
		MessageBox(NULL, L"Register Fail", L"Error", MB_OK);
		return 0;
	}

	// 2. 根据目标画布尺寸 (1280x720) 计算包含窗口边框的实际窗口大小
	int prefered_canvas_width = 1280;
	int prefered_canvas_height = 720;
	RECT rect;
	rect.left = 0;
	rect.right = prefered_canvas_width;
	rect.top = 0;
	rect.bottom = prefered_canvas_height;
	AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, NULL);
	int windowWidth = rect.right - rect.left;
	int windowHeight = rect.bottom - rect.top;

	// 3. 创建哑窗口 (Dummy Window) 用于初始化临时 OpenGL 上下文，加载 wgl 扩展函数
	HWND hwnd = CreateWindowEx(NULL, L"GLWindow", L"OpenGL Window", WS_OVERLAPPEDWINDOW,
		100, 100, windowWidth, windowHeight,
		NULL, NULL, hInstance, NULL);
	HDC dc = GetDC(hwnd);
	PIXELFORMATDESCRIPTOR pfd;
	memset(&pfd, 0, sizeof(PIXELFORMATDESCRIPTOR));
	pfd.nVersion = 1;
	pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
	pfd.cColorBits = 32;
	pfd.cDepthBits = 24;
	pfd.cStencilBits = 8;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	int pixelFormat = ChoosePixelFormat(dc, &pfd);
	SetPixelFormat(dc, pixelFormat, &pfd);
	HGLRC rc = wglCreateContext(dc);
	wglMakeCurrent(dc, rc);
	glewInit();

	// 4. 利用已经加载的 wglChoosePixelFormatARB 销毁哑窗口，创建支持 Core Profile 的正式 OpenGL 4.5 窗口
	if (wglChoosePixelFormatARB)
	{
		// 销毁旧上下文与窗口
		wglMakeCurrent(dc, nullptr);
		wglDeleteContext(rc);
		rc = nullptr;
		ReleaseDC(hwnd, dc);
		dc = nullptr;
		DestroyWindow(hwnd);
		
		// 重新创建正式渲染窗口
		hwnd = CreateWindowEx(NULL, L"GLWindow", L"RenderWindow", WS_OVERLAPPEDWINDOW, 100, 100, windowWidth, windowHeight, NULL, NULL, hInstance, NULL);
		dc = GetDC(hwnd);
		rc = CreateNBRC(dc); // 创建 Core Profile 及 MSAA 上下文
		wglMakeCurrent(dc, rc);
		const char* glVersionStr = (char*)glGetString(GL_VERSION);
		printf("OpenGL Version : %s\n", glVersionStr);
		Alice::InitExtensions();
	}

	// 5. 检查并打印硬件支持的 64 位 Shader 及 Atomic 扩展功能
	const char* extensionName = Alice::LikelyToSupportExtension("shader_ballot");
	if (extensionName != nullptr) printf("support [%s]\n", extensionName);
	extensionName = Alice::LikelyToSupportExtension("group_vote");
	if (extensionName != nullptr) printf("support [%s]\n", extensionName);
	extensionName = Alice::LikelyToSupportExtension("shader_int64");
	if (extensionName != nullptr) printf("support [%s]\n", extensionName);
	extensionName = Alice::LikelyToSupportExtension("atomic_int64");
	if (extensionName != nullptr) printf("support [%s]\n", extensionName);
	extensionName = Alice::LikelyToSupportExtension("shader5");
	if (extensionName != nullptr) printf("support [%s]\n", extensionName);

	// 6. 初始化 Nanite 渲染场景 (加载模型、BVH 树及创建 RenderPass)
	Init(prefered_canvas_width,prefered_canvas_height);
	glEnable(GL_CULL_FACE);
	glFrontFace(GL_CCW);
	ShowWindow(hwnd, SW_SHOW);
	UpdateWindow(hwnd);

	// 7. 进入 Win32 消息循环与渲染主循环
	MSG msg;
	DWORD last_time = timeGetTime();
	while (true)
	{
		// 处理系统 Windows 消息 (键盘/鼠标/窗口事件)
		if (PeekMessage(&msg, NULL, NULL, NULL, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
			{
				break;
			}
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else {
			// 计算两帧之间的时间间隔 (Delta Time)
			DWORD current_time = timeGetTime(); // 单位：毫秒 ms
			DWORD frameTime = current_time - last_time;
			last_time = current_time;
			float frameTimeInSecond = float(frameTime) / 1000.0f;
			
			// 渲染单帧并交换双缓冲区
			RenderOneFrame(frameTimeInSecond);
			SwapBuffers(dc);
		}
	}
	return 0;
}