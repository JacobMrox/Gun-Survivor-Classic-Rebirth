#include <windows.h>
#include <cassert>
#include <cstring>
#include <string>
#include <vector>

HMODULE g_hWrapperModule = nullptr;
constexpr int IDR_MISC_BMP = 2005;
std::vector<std::string> writes;
std::string directory;

DWORD FakeGetModuleFileNameA(HMODULE, char* path, DWORD size) {
  strcpy_s(path, size, "C:\\Game\\d3d8.dll");
  return static_cast<DWORD>(strlen(path));
}
BOOL FakeCreateDirectoryA(const char* path, LPSECURITY_ATTRIBUTES) {
  directory = path;
  return TRUE;
}
bool WriteEmbeddedResourceIfMissing(const char* path, int id) {
  assert(id == IDR_MISC_BMP);
  writes.emplace_back(path);
  return true;
}
#define GetModuleFileNameA FakeGetModuleFileNameA
#define CreateDirectoryA FakeCreateDirectoryA
#include "misc_resource_test.inc"

int main() {
  RestoreEmbeddedMiscIfMissing();
  assert(directory == "C:\\Game\\FIXRES");
  assert(writes.size() == 1);
  assert(writes[0] == "C:\\Game\\FIXRES\\MISC.BMP");
}
