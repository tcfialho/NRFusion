#pragma once

#include <d3d12.h>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace nrfusion::bridge {

using PfnNrInitExt = int(__cdecl*)(
    unsigned long long, const wchar_t*, ID3D12Device*, int, const void*);
using PfnNrCreate = int(__cdecl*)(
    ID3D12GraphicsCommandList*, int, const void*, void**);
using PfnNrEvaluate = int(__cdecl*)(
    ID3D12GraphicsCommandList*, const void*, const void*, void*);
using PfnNrRelease = int(__cdecl*)(void*);

struct Snippet {
  HMODULE module = nullptr;
  PfnNrInitExt init = nullptr;
  PfnNrCreate create = nullptr;
  PfnNrEvaluate evaluate = nullptr;
  PfnNrRelease release = nullptr;
  std::unordered_set<ID3D12Device*> initialisedDevices;
};

void RecordError(const char* context, int resultCode = 0);
const char* GetLastErrorString();
Snippet* LoadSnippetLocked(const wchar_t* path);

std::recursive_mutex& GetSnippetMutex();
std::unordered_map<void*, Snippet*>& GetFeatureOwners();

} // namespace nrfusion::bridge
