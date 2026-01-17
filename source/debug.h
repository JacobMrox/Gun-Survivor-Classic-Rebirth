#pragma once
#include <libloaderapi.h>
#include <cstdio>
// ===================== File Logger =====================
// Writes to "<gamefolder>\cdhook.log"
static void LogLine(const char* line)
{
    char path[MAX_PATH] = {};
    GetModuleFileNameA(NULL, path, MAX_PATH);

    char* slash = strrchr(path, '\\');
    if (slash) *(slash + 1) = 0; // keep folder only

    strcat_s(path, "hook.log");

    FILE* f = nullptr;
    fopen_s(&f, path, "a");
    if (!f) return;

    fprintf(f, "%s\n", line);
    fclose(f);
}