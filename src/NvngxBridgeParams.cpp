#include "NvngxBridgeParams.hpp"

#include <atomic>

namespace nrfusion::bridge {
namespace {

constexpr int kSlotSetUll = 0;
constexpr int kSlotSetUInt = 3;
using PfnSetUll = void(__cdecl*)(void*, const char*, unsigned long long);
using PfnSetUInt = void(__cdecl*)(void*, const char*, unsigned int);
using PfnSetFloat = void(__cdecl*)(void*, const char*, float);

std::atomic<int> g_floatSlot{1};

void SetUll(void* params, const char* name, unsigned long long value) {
  if (!params) return;
  void** vt = *reinterpret_cast<void***>(params);
  if (!vt || !vt[kSlotSetUll]) return;
  reinterpret_cast<PfnSetUll>(vt[kSlotSetUll])(params, name, value);
}

} // namespace

void SetFloatSlot(int slot) {
  if (slot >= 0 && slot < 8) g_floatSlot.store(slot, std::memory_order_relaxed);
}

void ProbeFloat(void* params, const char* name, float value, int slot) {
  if (!params || slot < 0 || slot >= 8) return;
  void** vt = *reinterpret_cast<void***>(params);
  if (!vt || !vt[slot]) return;
  reinterpret_cast<PfnSetFloat>(vt[slot])(params, name, value);
}

void SetUInt(void* params, const char* name, unsigned int value) {
  if (!params) return;
  void** vt = *reinterpret_cast<void***>(params);
  if (!vt || !vt[kSlotSetUInt]) return;
  reinterpret_cast<PfnSetUInt>(vt[kSlotSetUInt])(params, name, value);
}

void SetFloat(void* params, const char* name, float value) {
  if (!params) return;
  void** vt = *reinterpret_cast<void***>(params);
  const int slot = g_floatSlot.load(std::memory_order_relaxed);
  if (!vt || !vt[slot]) return;
  reinterpret_cast<PfnSetFloat>(vt[slot])(params, name, value);
}

void SetResource(void* params, const char* name, ID3D12Resource* res) {
  SetUll(params, name, reinterpret_cast<unsigned long long>(res));
}

void SetAutomaticMask(void* params, int useAutoMask) {
  if (useAutoMask < 0) return;
  SetUll(params, "DLSSNR.ControlMask", 0);
  SetUInt(params, "DLSSNR.UseAutoMask", useAutoMask != 0 ? 1u : 0u);
}

} // namespace nrfusion::bridge
