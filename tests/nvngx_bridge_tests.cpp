#include <windows.h>
#include <cassert>
#include <iostream>
#include <string>

using PfnSetFloatSlot = void(__cdecl*)(int);
using PfnProbeFloat = void(__cdecl*)(void*, const char*, float, int);
using PfnError = const char*(__cdecl*)();
using PfnRelease = void(__cdecl*)(void*);
using PfnCreate = void*(__cdecl*)(
    const wchar_t*, const wchar_t*, void*, void*, void*,
    unsigned int, unsigned int, int, float, int, float, float, float, int, int);

struct MockVTable {
  void* slots[8];
};

struct MockParams {
  MockVTable* vt = nullptr;
  std::string lastKey;
  float lastValue = 0.0f;
  unsigned int lastUInt = 0;
  unsigned long long lastUll = 0;
  int calledSlot = -1;
};

static void MockSetUll(void* self, const char* name, unsigned long long val) {
  auto* p = reinterpret_cast<MockParams*>(self);
  p->lastKey = name ? name : "";
  p->lastUll = val;
  p->calledSlot = 0;
}

static void MockSetUInt(void* self, const char* name, unsigned int val) {
  auto* p = reinterpret_cast<MockParams*>(self);
  p->lastKey = name ? name : "";
  p->lastUInt = val;
  p->calledSlot = 3;
}

static void MockSetFloat(void* self, const char* name, float val) {
  auto* p = reinterpret_cast<MockParams*>(self);
  p->lastKey = name ? name : "";
  p->lastValue = val;
  p->calledSlot = 1;
}

static void MockSetFloatSlot3(void* self, const char* name, float val) {
  auto* p = reinterpret_cast<MockParams*>(self);
  p->lastKey = name ? name : "";
  p->lastValue = val;
  p->calledSlot = 3;
}

int main() {
  HMODULE bridge = LoadLibraryW(L"nvngx.dll_dlssnr.dll");
  if (!bridge) {
    std::cerr << "Failed to load nvngx.dll_dlssnr.dll, err=" << GetLastError() << "\n";
    return 1;
  }

  auto pfnSetFloatSlot = reinterpret_cast<PfnSetFloatSlot>(
      GetProcAddress(bridge, "dlssnr_call_set_float_slot"));
  auto pfnProbeFloat = reinterpret_cast<PfnProbeFloat>(
      GetProcAddress(bridge, "dlssnr_call_probe_float"));
  auto pfnError = reinterpret_cast<PfnError>(
      GetProcAddress(bridge, "dlssnr_call_error"));
  auto pfnCreate = reinterpret_cast<PfnCreate>(
      GetProcAddress(bridge, "dlssnr_call_create"));
  auto pfnEvaluate = GetProcAddress(bridge, "dlssnr_call_evaluate_v2");
  auto pfnRelease = reinterpret_cast<PfnRelease>(
      GetProcAddress(bridge, "dlssnr_call_release"));
  auto* pLastInit = reinterpret_cast<int*>(
      GetProcAddress(bridge, "dlssnr_call_last_init"));
  auto* pLastCreate = reinterpret_cast<int*>(
      GetProcAddress(bridge, "dlssnr_call_last_create"));

  assert(pfnSetFloatSlot != nullptr);
  assert(pfnProbeFloat != nullptr);
  assert(pfnError != nullptr);
  assert(pfnCreate != nullptr);
  assert(pfnEvaluate != nullptr);
  assert(pfnRelease != nullptr);
  assert(pLastInit != nullptr);
  assert(pLastCreate != nullptr);

  MockVTable vt{};
  vt.slots[0] = reinterpret_cast<void*>(&MockSetUll);
  vt.slots[1] = reinterpret_cast<void*>(&MockSetFloat);
  vt.slots[3] = reinterpret_cast<void*>(&MockSetFloatSlot3);
  MockParams mock{};
  mock.vt = &vt;
  void* pMock = &mock;

  pfnProbeFloat(pMock, "DLSSNR.Intensity", 0.75f, 1);
  assert(mock.lastKey == "DLSSNR.Intensity");
  assert(mock.lastValue == 0.75f);
  assert(mock.calledSlot == 1);

  pfnProbeFloat(pMock, "DLSSNR.Intensity", 0.5f, 3);
  assert(mock.lastKey == "DLSSNR.Intensity");
  assert(mock.lastValue == 0.5f);
  assert(mock.calledSlot == 3);

  pfnSetFloatSlot(3);
  pfnSetFloatSlot(99);

  void* handle = pfnCreate(
      L"non_existent_snippet_path.dll", L"", nullptr, nullptr, pMock,
      1920, 1080, 0, 1.0f, 0, 0.0f, 0.0f, 0.0f, 0, 0);
  assert(handle == nullptr);
  const char* err = pfnError();
  assert(err != nullptr);
  std::cout << "Reported model error as expected: " << err << "\n";

  auto pfnSetExtras = reinterpret_cast<void(__cdecl*)(
      void*, float, void*, void*, void*, unsigned int, unsigned int,
      unsigned int, unsigned int)>(
      GetProcAddress(bridge, "dlssnr_call_set_extras"));
  assert(pfnSetExtras != nullptr);
  vt.slots[3] = reinterpret_cast<void*>(&MockSetUInt);
  pfnSetExtras(pMock, 1.0f, reinterpret_cast<void*>(1), reinterpret_cast<void*>(2),
               reinterpret_cast<void*>(3), 640, 480, 1920, 1080);
  assert(mock.lastKey == "DLSSNR.BackbufferSubrectHeight");
  assert(mock.lastUInt == 1080);
  assert(mock.calledSlot == 3);

  FreeLibrary(bridge);
  std::cout << "All NRFusion NVNGX bridge tests passed.\n";
  return 0;
}
