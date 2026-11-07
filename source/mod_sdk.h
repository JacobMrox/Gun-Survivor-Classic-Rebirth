#pragma once
#ifdef __cplusplus
extern "C" {
#endif

void ModSdk_Init(const char* configPath);
const char* ModSdk_ResolvePathA(const char* originalPath);
const char* ModSdk_ActiveName(void);
const char* ModSdk_ModDir(void);
void ModSdk_Shutdown(void);

#ifdef __cplusplus
}
#endif
