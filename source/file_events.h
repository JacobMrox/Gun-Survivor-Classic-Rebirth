#pragma once
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

	typedef void(__cdecl* FileOpenCallbackA)(const char* path);
	typedef void(__cdecl* FileOpenCallbackW)(const wchar_t* path);

	// Subscribers (set these from alpha_fix.cpp, etc.)
	extern FileOpenCallbackA g_OnFileOpenA;
	extern FileOpenCallbackW g_OnFileOpenW;

	// Called by whoever hooks CreateFile (cd_bypass in your case)
	void NotifyFileOpenA(const char* path);
	void NotifyFileOpenW(const wchar_t* path);

#ifdef __cplusplus
}
#endif
