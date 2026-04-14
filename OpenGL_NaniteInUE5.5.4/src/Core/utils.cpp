#include "utils.h"
#include <Windows.h>
#ifdef _WIN32
#pragma comment(lib,"winmm.lib")
#endif
unsigned char* LoadFileContent(const char* inFilePath, size_t& outFileSize) {
    FILE* file = nullptr;
    errno_t err = fopen_s(&file, inFilePath, "rb");
    if (err != 0) {
        return nullptr;
    }
    fseek(file, 0, SEEK_END);
    outFileSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    unsigned char* fileContent = new unsigned char[outFileSize];
    fread(fileContent, 1, outFileSize, file);
    fclose(file);
    return fileContent;
}
float GetFrameTime() {
    static unsigned long lastTime = 0, timeSinceComputerStart = 0;
    timeSinceComputerStart = timeGetTime();
    unsigned long frameTime = lastTime == 0 ? 0 : timeSinceComputerStart - lastTime;
    lastTime = timeSinceComputerStart;
    return float(frameTime) / 1000.0f;
}