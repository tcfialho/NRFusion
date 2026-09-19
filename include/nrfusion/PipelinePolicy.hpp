#pragma once
#include "nrfusion/ProviderPolicy.hpp"
#include "nrfusion/TransportPolicy.hpp"

namespace nrfusion {

// Orthogonal pipeline classification. Provider answers where frame-contract data comes from;
// transport answers how the process reaches the runtime; API remains independent from both.
struct PipelineDecision {
    FrameProvider provider = FrameProvider::Unsupported;
    ProcessTransport transport = ProcessTransport::InProcess;
    GraphicsApi api = GraphicsApi::Unknown;
    MotionSource motion = MotionSource::Zero;
    NrPlacement placement = NrPlacement::Auto;
    bool supported = false;
};

class PipelinePolicy {
public:
    PipelineDecision Choose(const GameContext& game, const FrameContext& frame,
                            const RuntimeCapabilities& capabilities,
                            std::optional<FrameProvider> preferredProvider = std::nullopt) const;


private:
    ProviderPolicy provider_;
    TransportPolicy transport_;
};

} // namespace nrfusion
