#include "NvngxBridgeSnippet.hpp"

#include <filesystem>
#include <memory>

namespace nrfusion::bridge {

namespace {

std::unordered_map<std::wstring, std::unique_ptr<Snippet>> g_snippets;
std::unordered_map<void*, Snippet*> g_featureOwners;
std::recursive_mutex g_snippetMutex;
std::string g_modelError;

std::wstring GetModuleDir() {
  HMODULE hModule = nullptr;
  if (!GetModuleHandleExW(
          GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
          reinterpret_cast<LPCWSTR>(&GetModuleDir), &hModule)) {
    return L"";
  }
  wchar_t buffer[MAX_PATH]{};
  if (GetModuleFileNameW(hModule, buffer, MAX_PATH) == 0) {
    return L"";
  }
  std::filesystem::path p(buffer);
  return p.parent_path().wstring();
}

} // namespace

void RecordError(const char* context, int resultCode) {
  if (resultCode != 0) {
    g_modelError = (context ? std::string(context) + " (code " : "error code ") +
                   std::to_string(resultCode) + ")";
    return;
  }
  const DWORD err = GetLastError();
  if (err == 0) {
    g_modelError = context ? context : "unknown error";
    return;
  }
  char buf[256];
  DWORD len = FormatMessageA(
      FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, err, 0, buf, sizeof(buf), nullptr);
  while (len > 0 && (buf[len - 1] == '\r' || buf[len - 1] == '\n' || buf[len - 1] == ' ')) {
    buf[--len] = '\0';
  }
  g_modelError = (context ? std::string(context) + ": " : "") + buf;
}

const char* GetLastErrorString() {
  std::lock_guard<std::recursive_mutex> lock(g_snippetMutex);
  return g_modelError.c_str();
}

std::recursive_mutex& GetSnippetMutex() {
  return g_snippetMutex;
}

std::unordered_map<void*, Snippet*>& GetFeatureOwners() {
  return g_featureOwners;
}

Snippet* LoadSnippetLocked(const wchar_t* path) {
  if (!path || !path[0]) {
    RecordError("empty snippet path");
    return nullptr;
  }
  std::wstring key(path);
  auto it = g_snippets.find(key);
  if (it != g_snippets.end()) {
    Snippet* s = it->second.get();
    return (s && s->create && s->evaluate && s->release) ? s : nullptr;
  }

  auto snippet = std::make_unique<Snippet>();
  snippet->module = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (!snippet->module) {
    const std::wstring modDir = GetModuleDir();
    if (!modDir.empty()) {
      std::filesystem::path candidate = std::filesystem::path(modDir) / L"nvngx_dlssnr.dll";
      snippet->module = LoadLibraryExW(candidate.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    }
  }

  if (!snippet->module) {
    RecordError("failed to load snippet module");
    return nullptr;
  }

  snippet->init = reinterpret_cast<PfnNrInitExt>(
      GetProcAddress(snippet->module, "NVSDK_NGX_D3D12_Init_Ext"));
  snippet->create = reinterpret_cast<PfnNrCreate>(
      GetProcAddress(snippet->module, "NVSDK_NGX_D3D12_CreateFeature"));
  snippet->evaluate = reinterpret_cast<PfnNrEvaluate>(
      GetProcAddress(snippet->module, "NVSDK_NGX_D3D12_EvaluateFeature"));
  snippet->release = reinterpret_cast<PfnNrRelease>(
      GetProcAddress(snippet->module, "NVSDK_NGX_D3D12_ReleaseFeature"));

  if (!snippet->create || !snippet->evaluate || !snippet->release) {
    RecordError("required NGX exports missing in snippet");
    return nullptr;
  }

  Snippet* raw = snippet.get();
  g_snippets.emplace(std::move(key), std::move(snippet));
  return raw;
}

} // namespace nrfusion::bridge
