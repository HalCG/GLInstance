#pragma once
#include <stdio.h>
#include <string.h>
#include <string>
unsigned char* LoadFileContent(const char* inFilePath, size_t& outFileSize);
float GetFrameTime();