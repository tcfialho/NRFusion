#pragma once
#include "NrKernelProfileD3D12.hpp"

#include <memory>
#include <vector>

namespace nrfusion::kernelprofile {
using ModuleImage = std::shared_ptr<const std::vector<std::uint8_t>>;
void ConfigureAbiDiscovery();
ModuleImage CopyAbiModule(const void* image, std::uint32_t bytes);
void SelectAbiFunction(const FunctionIdentity& identity, ModuleImage image);
void ObservePackedArguments(const LaunchRecord& record, const void* parameters) noexcept;
bool FlushAbiDiscovery();
}
