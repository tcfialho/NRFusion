#include "D3D12TestHarness.hpp"

#include <fstream>
#include <iostream>

namespace nrfusion::testing {

void D3D12TestHarness::ExportTelemetryJson(const std::string& path) {
    std::ofstream out(path);
    if (!out.is_open()) return;

    out << "{\n  \"gpu\": \"" << adapterName_ << "\",\n";
    out << "  \"totalFrames\": " << metrics_.size() << ",\n";
    out << "  \"frames\": [\n";
    for (size_t i = 0; i < metrics_.size(); ++i) {
        const auto& m = metrics_[i];
        out << "    {\"frame\": " << m.frameId
            << ", \"renderMs\": " << m.renderGpuMs
            << ", \"nrMs\": " << m.nrSimGpuMs
            << ", \"frameMs\": " << m.frameGpuMs
            << ", \"overlap\": " << m.asyncOverlap
            << ", \"asyncStable\": " << (m.asyncStable ? "true" : "false")
            << ", \"scale\": " << m.resolvedScale
            << ", \"scheduler\": \"" << (m.scheduler == SchedulerMode::AsyncCompute ? "AsyncCompute" : "Serialized") << "\""
            << ", \"supported\": " << (m.autoDecisionSupported ? "true" : "false")
            << "}" << (i + 1 < metrics_.size() ? ",\n" : "\n");
    }
    out << "  ]\n}\n";
    out.close();
    std::cout << "[Harness 3D] Exported telemetry to: " << path << std::endl;
}


} // namespace nrfusion::testing
