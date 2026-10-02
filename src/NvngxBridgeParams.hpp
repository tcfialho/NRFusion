#pragma once

#include <d3d12.h>
#include <cstdint>

namespace nrfusion::bridge {

void SetFloatSlot(int slot);
void ProbeFloat(void* params, const char* name, float value, int slot);
void SetUInt(void* params, const char* name, unsigned int value);
void SetFloat(void* params, const char* name, float value);
void SetResource(void* params, const char* name, ID3D12Resource* res);
void SetAutomaticMask(void* params, int useAutoMask);

} // namespace nrfusion::bridge
