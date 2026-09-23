// Repassa version.dll real: o instalador usa este binário como proxy, e sem isto o próprio
// import do jogo por essas funções fica sem resolver (jogo não inicia).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

namespace {

HMODULE RealVersionModule() {
    static HMODULE module = [] {
        wchar_t systemDir[MAX_PATH];
        UINT len = GetSystemDirectoryW(systemDir, MAX_PATH);
        if (len == 0 || len >= MAX_PATH) return static_cast<HMODULE>(nullptr);
        std::wstring path(systemDir, len);
        path += L"\\version.dll";
        return LoadLibraryW(path.c_str());
    }();
    return module;
}

template <typename Fn>
Fn ResolveRealExport(const char* name) {
    HMODULE module = RealVersionModule();
    if (!module) return nullptr;
    return reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(module, name)));
}

} // namespace

extern "C" {

BOOL WINAPI GetFileVersionInfoA(LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) {
    using Fn = BOOL(WINAPI*)(LPCSTR, DWORD, DWORD, LPVOID);
    static Fn real = ResolveRealExport<Fn>("GetFileVersionInfoA");
    return real ? real(lptstrFilename, dwHandle, dwLen, lpData) : FALSE;
}

BOOL WINAPI GetFileVersionInfoW(LPCWSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) {
    using Fn = BOOL(WINAPI*)(LPCWSTR, DWORD, DWORD, LPVOID);
    static Fn real = ResolveRealExport<Fn>("GetFileVersionInfoW");
    return real ? real(lptstrFilename, dwHandle, dwLen, lpData) : FALSE;
}

DWORD WINAPI GetFileVersionInfoSizeA(LPCSTR lptstrFilename, LPDWORD lpdwHandle) {
    using Fn = DWORD(WINAPI*)(LPCSTR, LPDWORD);
    static Fn real = ResolveRealExport<Fn>("GetFileVersionInfoSizeA");
    return real ? real(lptstrFilename, lpdwHandle) : 0;
}

DWORD WINAPI GetFileVersionInfoSizeW(LPCWSTR lptstrFilename, LPDWORD lpdwHandle) {
    using Fn = DWORD(WINAPI*)(LPCWSTR, LPDWORD);
    static Fn real = ResolveRealExport<Fn>("GetFileVersionInfoSizeW");
    return real ? real(lptstrFilename, lpdwHandle) : 0;
}

BOOL WINAPI GetFileVersionInfoExA(DWORD dwFlags, LPCSTR lpwstrFilename, DWORD dwHandle, DWORD dwLen,
                                   LPVOID lpData) {
    using Fn = BOOL(WINAPI*)(DWORD, LPCSTR, DWORD, DWORD, LPVOID);
    static Fn real = ResolveRealExport<Fn>("GetFileVersionInfoExA");
    return real ? real(dwFlags, lpwstrFilename, dwHandle, dwLen, lpData) : FALSE;
}

BOOL WINAPI GetFileVersionInfoExW(DWORD dwFlags, LPCWSTR lpwstrFilename, DWORD dwHandle, DWORD dwLen,
                                   LPVOID lpData) {
    using Fn = BOOL(WINAPI*)(DWORD, LPCWSTR, DWORD, DWORD, LPVOID);
    static Fn real = ResolveRealExport<Fn>("GetFileVersionInfoExW");
    return real ? real(dwFlags, lpwstrFilename, dwHandle, dwLen, lpData) : FALSE;
}

DWORD WINAPI GetFileVersionInfoSizeExA(DWORD dwFlags, LPCSTR lpwstrFilename, LPDWORD lpdwHandle) {
    using Fn = DWORD(WINAPI*)(DWORD, LPCSTR, LPDWORD);
    static Fn real = ResolveRealExport<Fn>("GetFileVersionInfoSizeExA");
    return real ? real(dwFlags, lpwstrFilename, lpdwHandle) : 0;
}

DWORD WINAPI GetFileVersionInfoSizeExW(DWORD dwFlags, LPCWSTR lpwstrFilename, LPDWORD lpdwHandle) {
    using Fn = DWORD(WINAPI*)(DWORD, LPCWSTR, LPDWORD);
    static Fn real = ResolveRealExport<Fn>("GetFileVersionInfoSizeExW");
    return real ? real(dwFlags, lpwstrFilename, lpdwHandle) : 0;
}

BOOL WINAPI VerQueryValueA(LPCVOID pBlock, LPCSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen) {
    using Fn = BOOL(WINAPI*)(LPCVOID, LPCSTR, LPVOID*, PUINT);
    static Fn real = ResolveRealExport<Fn>("VerQueryValueA");
    return real ? real(pBlock, lpSubBlock, lplpBuffer, puLen) : FALSE;
}

BOOL WINAPI VerQueryValueW(LPCVOID pBlock, LPCWSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen) {
    using Fn = BOOL(WINAPI*)(LPCVOID, LPCWSTR, LPVOID*, PUINT);
    static Fn real = ResolveRealExport<Fn>("VerQueryValueW");
    return real ? real(pBlock, lpSubBlock, lplpBuffer, puLen) : FALSE;
}

DWORD WINAPI VerLanguageNameA(DWORD wLang, LPSTR szLang, DWORD cchLang) {
    using Fn = DWORD(WINAPI*)(DWORD, LPSTR, DWORD);
    static Fn real = ResolveRealExport<Fn>("VerLanguageNameA");
    return real ? real(wLang, szLang, cchLang) : 0;
}

DWORD WINAPI VerLanguageNameW(DWORD wLang, LPWSTR szLang, DWORD cchLang) {
    using Fn = DWORD(WINAPI*)(DWORD, LPWSTR, DWORD);
    static Fn real = ResolveRealExport<Fn>("VerLanguageNameW");
    return real ? real(wLang, szLang, cchLang) : 0;
}

DWORD WINAPI VerFindFileA(DWORD uFlags, LPSTR szFileName, LPSTR szWinDir, LPSTR szAppDir, LPSTR szCurDir,
                           PUINT lpuCurDirLen, LPSTR szDestDir, PUINT lpuDestDirLen) {
    using Fn = DWORD(WINAPI*)(DWORD, LPSTR, LPSTR, LPSTR, LPSTR, PUINT, LPSTR, PUINT);
    static Fn real = ResolveRealExport<Fn>("VerFindFileA");
    return real ? real(uFlags, szFileName, szWinDir, szAppDir, szCurDir, lpuCurDirLen, szDestDir,
                        lpuDestDirLen)
                : 0;
}

DWORD WINAPI VerFindFileW(DWORD uFlags, LPWSTR szFileName, LPWSTR szWinDir, LPWSTR szAppDir,
                           LPWSTR szCurDir, PUINT lpuCurDirLen, LPWSTR szDestDir, PUINT lpuDestDirLen) {
    using Fn = DWORD(WINAPI*)(DWORD, LPWSTR, LPWSTR, LPWSTR, LPWSTR, PUINT, LPWSTR, PUINT);
    static Fn real = ResolveRealExport<Fn>("VerFindFileW");
    return real ? real(uFlags, szFileName, szWinDir, szAppDir, szCurDir, lpuCurDirLen, szDestDir,
                        lpuDestDirLen)
                : 0;
}

DWORD WINAPI VerInstallFileA(DWORD uFlags, LPSTR szSrcFileName, LPSTR szDestFileName, LPSTR szSrcDir,
                              LPSTR szDestDir, LPSTR szCurDir, LPSTR szTmpFile, PUINT lpuTmpFileLen) {
    using Fn = DWORD(WINAPI*)(DWORD, LPSTR, LPSTR, LPSTR, LPSTR, LPSTR, LPSTR, PUINT);
    static Fn real = ResolveRealExport<Fn>("VerInstallFileA");
    return real ? real(uFlags, szSrcFileName, szDestFileName, szSrcDir, szDestDir, szCurDir, szTmpFile,
                        lpuTmpFileLen)
                : 0;
}

DWORD WINAPI VerInstallFileW(DWORD uFlags, LPWSTR szSrcFileName, LPWSTR szDestFileName, LPWSTR szSrcDir,
                              LPWSTR szDestDir, LPWSTR szCurDir, LPWSTR szTmpFile, PUINT lpuTmpFileLen) {
    using Fn = DWORD(WINAPI*)(DWORD, LPWSTR, LPWSTR, LPWSTR, LPWSTR, LPWSTR, LPWSTR, PUINT);
    static Fn real = ResolveRealExport<Fn>("VerInstallFileW");
    return real ? real(uFlags, szSrcFileName, szDestFileName, szSrcDir, szDestDir, szCurDir, szTmpFile,
                        lpuTmpFileLen)
                : 0;
}

} // extern "C"
