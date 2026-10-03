// Reflex latency percentile calculations and extraction
StreamlineMfg::ReflexLatencyStats StreamlineMfg::GetReflexLatencyStats() const noexcept {
    ReflexLatencyStats stats{};
#if defined(NRFUSION_REQUIEM_STREAMLINE)
    if (!impl_->reflexReportEverAvailable || impl_->reflexData.empty()) return stats;
    stats.available = true;
    stats.samples = impl_->reflexData.size();

    auto calc = [](std::vector<double>& v) -> ReflexMetricPercentiles {
        if (v.empty()) return {};
        std::sort(v.begin(), v.end());
        const size_t n = v.size();
        return {v[n * 50 / 100], v[n * 95 / 100], v[n * 99 / 100]};
    };

    std::vector<double> sim, rsub, pstart, pend, drv, osq, gpu;
    sim.reserve(stats.samples); rsub.reserve(stats.samples);
    pstart.reserve(stats.samples); pend.reserve(stats.samples);
    drv.reserve(stats.samples); osq.reserve(stats.samples); gpu.reserve(stats.samples);

    for (const auto& dp : impl_->reflexData) {
        if (dp.simStart > 0.0) sim.push_back(dp.simStart);
        if (dp.renderSubmitStart > 0.0) rsub.push_back(dp.renderSubmitStart);
        if (dp.presentStart > 0.0) pstart.push_back(dp.presentStart);
        if (dp.presentEnd > 0.0) pend.push_back(dp.presentEnd);
        if (dp.driverEnd > 0.0) drv.push_back(dp.driverEnd);
        if (dp.osQueueEnd > 0.0) osq.push_back(dp.osQueueEnd);
        if (dp.gpuRenderEnd > 0.0) gpu.push_back(dp.gpuRenderEnd);
    }

    stats.inputToSimStart = calc(sim);
    stats.inputToRenderSubmitStart = calc(rsub);
    stats.inputToPresentStart = calc(pstart);
    stats.inputToPresentEnd = calc(pend);
    stats.inputToDriverEnd = calc(drv);
    stats.inputToOsQueueEnd = calc(osq);
    stats.inputToGpuRenderEnd = calc(gpu);
#endif
    return stats;
}
