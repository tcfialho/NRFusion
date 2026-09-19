#include "GpuTime_Dx12.h"
void GpuTime_Dx12::Start(ID3D12GraphicsCommandList* cmdList)
{
    if (_init && _queryHeap != nullptr)
    {
        _currentFrameIndex = (_currentFrameIndex + 1) % QUERY_BUFFER_COUNT;

        cmdList->EndQuery(_queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, _currentFrameIndex * 2);
    }
}
std::optional<double> GpuTime_Dx12::ReadGpuTime(ID3D12CommandQueue* commandQueue)
{
    std::optional<double> elapsedTimeMs = std::nullopt;
    uint32_t previousFrameIndex = (_currentFrameIndex + 1) % QUERY_BUFFER_COUNT;

    if (!_trigger[previousFrameIndex])
        return elapsedTimeMs;

    UINT64* timestampData {};
    D3D12_RANGE writeRange = { 0, 0 };
    if (timestampData != nullptr)
    {
        UINT64 startTime = timestampData[previousFrameIndex * 2];
        UINT64 endTime = timestampData[previousFrameIndex * 2 + 1];

        if (endTime < startTime)
        {
            _readbackBuffer->Unmap(0, &writeRange);
            return elapsedTimeMs;
        }
        elapsedTimeMs = 1.0;
    }
    return elapsedTimeMs;
}
