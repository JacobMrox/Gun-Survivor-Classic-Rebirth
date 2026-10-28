#include "file_events.h"
#include <cstring>

namespace {
thread_local bool g_pendingBloodTexture = false;

bool IsNativeBloodPath(const char* path) {
    if (!path) return false;
    const char* name = path;
    if (const char* slash = strrchr(path, '\\')) name = slash + 1;
    if (const char* slash = strrchr(name, '/')) name = slash + 1;
    return _stricmp(name, "BLOOD1.BMP") == 0 || _stricmp(name, "BLOOD2.BMP") == 0;
}
}

FileOpenCallbackA g_OnFileOpenA = nullptr;
FileOpenCallbackW g_OnFileOpenW = nullptr;

void NotifyFileOpenA(const char* path)
{
    if (IsNativeBloodPath(path)) g_pendingBloodTexture = true;
    if (g_OnFileOpenA && path) g_OnFileOpenA(path);
}

void NotifyFileOpenW(const wchar_t* path)
{
    if (g_OnFileOpenW && path) g_OnFileOpenW(path);
}

bool HasPendingBloodTextureOpen() { return g_pendingBloodTexture; }
void ConsumePendingBloodTextureOpen() { g_pendingBloodTexture = false; }
