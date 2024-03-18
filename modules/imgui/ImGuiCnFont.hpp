#pragma once

#include "imgui.h"

#include <cstdio>

// ImGui 默认 ProggyClean 不含中文 glyph，括号里中文会显示成 '?'。
// 在 CreateContext 之后、第一次 NewFrame 之前调用本函数，加载系统中文字体。
inline bool ImGuiLoadChineseFont(float pixelSize = 18.0f) {
    ImGuiIO &io = ImGui::GetIO();

    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    cfg.PixelSnapH = true;
    // msyh.ttc / simsun.ttc 为字体集合，取第 0 个 face
    cfg.FontNo = 0;

    const char *candidates[] = {
        "C:/Windows/Fonts/msyh.ttc",   // 微软雅黑
        "C:/Windows/Fonts/msyhbd.ttc", // 微软雅黑 Bold（兜底）
        "C:/Windows/Fonts/simhei.ttf", // 黑体
        "C:/Windows/Fonts/simsun.ttc", // 宋体
    };

    const ImWchar *ranges = io.Fonts->GetGlyphRangesChineseFull();
    for (const char *path : candidates) {
        FILE *fp = nullptr;
#if defined(_MSC_VER)
        if (fopen_s(&fp, path, "rb") != 0 || !fp) {
            continue;
        }
#else
        fp = fopen(path, "rb");
        if (!fp) {
            continue;
        }
#endif
        fclose(fp);

        ImFont *font = io.Fonts->AddFontFromFileTTF(path, pixelSize, &cfg, ranges);
        if (font) {
            io.FontDefault = font;
            return true;
        }
    }

    // 找不到中文字体时退回默认字体（中文仍会是 '?'）
    io.Fonts->AddFontDefault();
    return false;
}
