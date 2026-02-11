//Debug.h: logging utility for Windows applications
#pragma once
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

// Global flag to enable/disable logging (defined in dllmain.cpp)
extern bool gEnableLogging;

static void LogLine(const char* fmt, ...)
{
    if (!gEnableLogging) return;

    char path[MAX_PATH] = {};
    GetModuleFileNameA(NULL, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) *(slash + 1) = 0;
    strcat_s(path, "hook.log");

    FILE* f = nullptr;
    fopen_s(&f, path, "a");
    if (!f) return;

    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    fprintf(f, "%s\n", buf);
    fclose(f);
}
