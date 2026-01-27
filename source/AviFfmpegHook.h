#pragma once
#include <windows.h>

namespace AviFfmpegHook {
	bool InstallForMainModule();
	bool InstallForModule(HMODULE module);
}