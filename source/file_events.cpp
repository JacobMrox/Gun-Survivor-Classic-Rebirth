#include "file_events.h"

FileOpenCallbackA g_OnFileOpenA = nullptr;
FileOpenCallbackW g_OnFileOpenW = nullptr;

void NotifyFileOpenA(const char* path)
{
    if (g_OnFileOpenA && path) g_OnFileOpenA(path);
}

void NotifyFileOpenW(const wchar_t* path)
{
    if (g_OnFileOpenW && path) g_OnFileOpenW(path);
}
