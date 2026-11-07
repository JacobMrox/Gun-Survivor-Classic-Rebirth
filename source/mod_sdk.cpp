#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include "mod_sdk.h"

// Gemini Classic Rebirth Mod-Sdk replica for Gun Survivor (RE: Survivor).
// Design (RE2-Mod-SDK wiki):
//  Developer mode: mods/mod_<id>/ folder with manifest.txt, paths mirroring
//    game-relative paths (e.g. mod_xxx/PL0/PLD/PL0.PLD overrides PL0/PLD/PL0.PLD).
//  Release mode: mods/mod_<id>.7z archive (non-solid, s=off) whose contents
//    sit at the archive root. Extracted on demand to mods/cache/<id>/.
//  manifest.txt fields: [MOD] Name, Title, Module (DLL to LoadLibrary),
//    SavePath (optional separate save folder). Survivor keeps Name/Title/Module.
//  A mod's Module DLL is loaded from the mod folder (DLL stored in its right
//  folder), so code mods work without touching the main game.
// Safety: never overrides GBIO.CFG, debug.txt/debug.log, config.ini, d3d8.dll,
// or the running exe. Mods cannot disable anti-tamper or ship game files.
//
// config.ini: [Mod] Active = mod_<id>  (empty/absent = no mod)

static std::string g_modDir;
static std::string g_activeName;
static std::string g_title;
static HMODULE g_moduleDll = nullptr;
static bool g_init = false;

static std::string BaseDir() {
    char p[MAX_PATH]{};
    if (GetModuleFileNameA(nullptr, p, MAX_PATH)) {
        char* s = strrchr(p, '\\');
        if (s) *s = '\0';
        return p;
    }
    GetCurrentDirectoryA(MAX_PATH, p);
    return p;
}

static std::string Trim(const std::string& s) {
    size_t a = 0; while (a < s.size() && std::isspace((unsigned char)s[a])) ++a;
    size_t b = s.size(); while (b > a && std::isspace((unsigned char)s[b-1])) --b;
    return s.substr(a, b - a);
}

static std::string Lower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

// Normalize an original open path to a game-relative forward path for lookup.
static bool ToRelative(const char* orig, std::string& out) {
    if (!orig || !orig[0]) return false;
    std::string p = orig;
    // Strip drive "X:\" and any leading slashes.
    if (p.size() >= 2 && p[1] == ':') p = p.substr(2);
    size_t i = 0; while (i < p.size() && (p[i] == '\\' || p[i] == '/' || p[i] == '.')) ++i;
    if (i) p = p.substr(i);
    if (p.empty()) return false;
    for (auto& c : p) if (c == '/') c = '\\';
    // Never allow climbing out of the mod dir.
    if (p.find("..") != std::string::npos) return false;
    out = p;
    return true;
}

static bool IsProtected(const std::string& rel) {
    std::string l = Lower(rel);
    if (l == "gbio.cfg" || l == "config.ini" || l == "d3d8.ini") return true;
    if (l.find("debug.txt") != std::string::npos) return true;
    if (l.find("debug.log") != std::string::npos) return true;
    if (l == "d3d8.dll" || l == "gun_bio.exe") return true;
    return false;
}

static bool FileExistsA(const std::string& path) {
    DWORD a = GetFileAttributesA(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static void ParseManifest(const std::string& dir, std::string& name, std::string& title, std::string& module) {
    char path[MAX_PATH]{};
    snprintf(path, sizeof(path), "%s\\manifest.txt", dir.c_str());
    FILE* f = nullptr;
    fopen_s(&f, path, "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        std::string s = Trim(line);
        if (s.empty() || s[0] == ';' || s[0] == '#' || s[0] == '[') continue;
        size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        std::string k = Lower(Trim(s.substr(0, eq)));
        std::string v = Trim(s.substr(eq + 1));
        if (k == "name") name = v;
        else if (k == "title") title = v;
        else if (k == "module") module = v;
    }
    fclose(f);
}

static void EnsureArchiveExtracted(const std::string& modsDir, const std::string& baseId, const std::string& archive) {
    std::string cache = modsDir + "\\cache\\" + baseId;
    // If cache already has manifest, assume extracted.
    std::string marker = cache + "\\manifest.txt";
    if (FileExistsA(marker)) return;
    CreateDirectoryA((modsDir + "\\cache").c_str(), nullptr);
    CreateDirectoryA(cache.c_str(), nullptr);
    // Release spec: non-solid 7z, contents at root. Extract via 7z.exe if present.
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "7z x \"%s\" -o\"%s\" -y >nul 2>&1", archive.c_str(), cache.c_str());
    int rc = system(cmd);
    // Best effort only; folder mods remain fully supported without 7-Zip.
    (void)rc;
}

extern "C" void ModSdk_Init(const char* configPath) {
    if (g_init) return;
    g_init = true;
    if (!configPath || !configPath[0]) return;
    // Read Active from [Mod] Active with fallback to Favorites? Keep simple.
    char active[256]{};
    GetPrivateProfileStringA("Mod", "Active", "", active, sizeof(active), configPath);
    std::string want = Trim(active);
    if (want.empty()) return;
    std::string base = BaseDir();
    std::string modsDir = base + "\\mods";
    std::string folder = modsDir + "\\" + want;
    std::string archive = modsDir + "\\" + want + ".7z";
    std::string dir;
    if (GetFileAttributesA(folder.c_str()) != INVALID_FILE_ATTRIBUTES) dir = folder;
    else if (FileExistsA(archive)) {
        EnsureArchiveExtracted(modsDir, want, archive);
        dir = modsDir + "\\cache\\" + want;
        if (GetFileAttributesA(dir.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    } else return;
    std::string mName, mTitle, mModule;
    ParseManifest(dir, mName, mTitle, mModule);
    g_modDir = dir;
    g_activeName = want;
    g_title = mTitle;
    if (!mModule.empty()) {
        std::string dll = dir + "\\" + mModule;
        if (FileExistsA(dll)) g_moduleDll = LoadLibraryA(dll.c_str());
    }
}

extern "C" const char* ModSdk_ResolvePathA(const char* originalPath) {
    static thread_local char out[MAX_PATH];
    out[0] = '\0';
    if (g_modDir.empty() || !originalPath) return nullptr;
    std::string rel;
    if (!ToRelative(originalPath, rel)) return nullptr;
    if (IsProtected(rel)) return nullptr;
    std::string cand = g_modDir + "\\" + rel;
    if (FileExistsA(cand)) {
        strcpy_s(out, cand.c_str());
        return out;
    }
    return nullptr;
}

extern "C" const char* ModSdk_ActiveName(void) { return g_activeName.c_str(); }
extern "C" const char* ModSdk_ModDir(void) { return g_modDir.c_str(); }
extern "C" void ModSdk_Shutdown(void) {
    if (g_moduleDll) { FreeLibrary(g_moduleDll); g_moduleDll = nullptr; }
}
