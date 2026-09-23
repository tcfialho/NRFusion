#include "nrfusion/SyntheticOpenGlProvider.hpp"

#include <iostream>

// Only one compiler understands a library request written in the source. Elsewhere it is an
// unknown pragma, which a build with warnings as errors refuses outright.
#if defined(_MSC_VER)
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "opengl32.lib")
#endif

namespace nrfusion {

void SyntheticOpenGlProvider::CloseSharedHandles() {
    for (auto& s : sharedSlots_) {
        if (s.colorSharedHandle) {
            CloseHandle(s.colorSharedHandle);
            s.colorSharedHandle = nullptr;
        }
        if (s.residualSharedHandle) {
            CloseHandle(s.residualSharedHandle);
            s.residualSharedHandle = nullptr;
        }

        if (gl_.hasInterop) {
            if (s.glColorTex) {
                glDeleteTextures(1, &s.glColorTex);
                s.glColorTex = 0;
            }
            if (s.glColorMem && gl_.DeleteMemoryObjectsEXT) {
                gl_.DeleteMemoryObjectsEXT(1, &s.glColorMem);
                s.glColorMem = 0;
            }
            if (s.glResidualTex) {
                glDeleteTextures(1, &s.glResidualTex);
                s.glResidualTex = 0;
            }
            if (s.glResidualMem && gl_.DeleteMemoryObjectsEXT) {
                gl_.DeleteMemoryObjectsEXT(1, &s.glResidualMem);
                s.glResidualMem = 0;
            }
            if (s.glInputSem && gl_.DeleteSemaphoresEXT) {
                gl_.DeleteSemaphoresEXT(1, &s.glInputSem);
                s.glInputSem = 0;
            }
            if (s.glOutputSem && gl_.DeleteSemaphoresEXT) {
                gl_.DeleteSemaphoresEXT(1, &s.glOutputSem);
                s.glOutputSem = 0;
            }
        }

        s.d3d12Color.Reset();
        s.d3d12Residual.Reset();
        s.inUse = false;
        s.workId = 0;
    }
}

bool SyntheticOpenGlProvider::CreateSharedResources(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return false;
    if (currentRes_.width == width && currentRes_.height == height) return true;

    CloseSharedHandles();

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC resDesc{};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resDesc.Width = width;
    resDesc.Height = height;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    resDesc.SampleDesc.Count = 1;
    resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    for (auto& s : sharedSlots_) {
        // 1. D3D12 Color Shared Texture
        if (FAILED(d3d12Device_->CreateCommittedResource(
                &heapProps, D3D12_HEAP_FLAG_SHARED, &resDesc,
                D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&s.d3d12Color)))) {
            return false;
        }
        if (FAILED(d3d12Device_->CreateSharedHandle(s.d3d12Color.Get(), nullptr, GENERIC_ALL, nullptr, &s.colorSharedHandle))) {
            return false;
        }

        // 2. D3D12 Residual Shared Texture
        if (FAILED(d3d12Device_->CreateCommittedResource(
                &heapProps, D3D12_HEAP_FLAG_SHARED, &resDesc,
                D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&s.d3d12Residual)))) {
            return false;
        }
        if (FAILED(d3d12Device_->CreateSharedHandle(s.d3d12Residual.Get(), nullptr, GENERIC_ALL, nullptr, &s.residualSharedHandle))) {
            return false;
        }

        // Query allocation sizes for OpenGL dedicated memory import
        D3D12_RESOURCE_ALLOCATION_INFO colorAllocInfo = d3d12Device_->GetResourceAllocationInfo(0, 1, &resDesc);

        // 3. OpenGL Side Memory & Semaphore Import (if OpenGL context is active)
        if (gl_.hasInterop && gl_.wglGetCurrentContext && gl_.wglGetCurrentContext() != nullptr) {
            // Import Color Memory Object
            gl_.CreateMemoryObjectsEXT(1, &s.glColorMem);
            const GLint dedicated = GL_TRUE;
            gl_.MemoryObjectParameterivEXT(s.glColorMem, GL_DEDICATED_MEMORY_OBJECT_EXT, &dedicated);
            gl_.ImportMemoryWin32HandleEXT(s.glColorMem, colorAllocInfo.SizeInBytes, GL_HANDLE_TYPE_D3D12_RESOURCE_EXT, s.colorSharedHandle);

            glGenTextures(1, &s.glColorTex);
            glBindTexture(GL_TEXTURE_2D, s.glColorTex);
            gl_.TexStorageMem2DEXT(GL_TEXTURE_2D, 1, GL_RGBA16F, width, height, s.glColorMem, 0);

            // Import Residual Memory Object
            gl_.CreateMemoryObjectsEXT(1, &s.glResidualMem);
            gl_.MemoryObjectParameterivEXT(s.glResidualMem, GL_DEDICATED_MEMORY_OBJECT_EXT, &dedicated);
            gl_.ImportMemoryWin32HandleEXT(s.glResidualMem, colorAllocInfo.SizeInBytes, GL_HANDLE_TYPE_D3D12_RESOURCE_EXT, s.residualSharedHandle);

            glGenTextures(1, &s.glResidualTex);
            glBindTexture(GL_TEXTURE_2D, s.glResidualTex);
            gl_.TexStorageMem2DEXT(GL_TEXTURE_2D, 1, GL_RGBA16F, width, height, s.glResidualMem, 0);
            glBindTexture(GL_TEXTURE_2D, 0);

            // Import Shared Semaphores
            gl_.GenSemaphoresEXT(1, &s.glInputSem);
            gl_.ImportSemaphoreWin32HandleEXT(s.glInputSem, GL_HANDLE_TYPE_D3D12_FENCE_EXT, d3d12FenceSharedHandle_);

            gl_.GenSemaphoresEXT(1, &s.glOutputSem);
            gl_.ImportSemaphoreWin32HandleEXT(s.glOutputSem, GL_HANDLE_TYPE_D3D12_FENCE_EXT, d3d12FenceSharedHandle_);
        }
    }

    currentRes_ = { width, height };
    nvof_.Initialize(d3d12Device_.Get(), d3d12Queue_.Get(), width, height);
    return true;
}

bool SyntheticOpenGlProvider::RecordOpenGlInputCopy(GLuint gameColorTex, uint32_t width, uint32_t height) {
    if (!gl_.hasInterop || gameColorTex == 0) return false;
    SharedSlot& slot = sharedSlots_[currentSlot_];
    if (slot.glColorTex == 0) return false;

    // GPU-only copy from game color texture into D3D12-shared texture
    if (gl_.CopyImageSubData) {
        gl_.CopyImageSubData(gameColorTex, GL_TEXTURE_2D, 0, 0, 0, 0,
                             slot.glColorTex, GL_TEXTURE_2D, 0, 0, 0, 0,
                             static_cast<GLsizei>(width), static_cast<GLsizei>(height), 1);
    }

    // Signal shared input semaphore in OpenGL command stream
    if (gl_.SignalSemaphoreEXT && slot.glInputSem != 0) {
        gl_.SignalSemaphoreEXT(slot.glInputSem, 0, nullptr, 1, &slot.glColorTex, nullptr);
    }

    return true;
}

bool SyntheticOpenGlProvider::RecordOpenGlOutputConsume(GLuint gameDestTex, uint32_t width, uint32_t height) {
    if (!gl_.hasInterop || gameDestTex == 0) return false;
    SharedSlot& slot = sharedSlots_[currentSlot_];
    if (slot.glResidualTex == 0) return false;

    // GPU-only wait on D3D12 completion semaphore in OpenGL command stream (Zero CPU Wait)
    if (gl_.WaitSemaphoreEXT && slot.glOutputSem != 0) {
        gl_.WaitSemaphoreEXT(slot.glOutputSem, 0, nullptr, 1, &slot.glResidualTex, nullptr);
    }

    // Blit/Copy residual into game target buffer
    if (gl_.CopyImageSubData) {
        gl_.CopyImageSubData(slot.glResidualTex, GL_TEXTURE_2D, 0, 0, 0, 0,
                             gameDestTex, GL_TEXTURE_2D, 0, 0, 0, 0,
                             static_cast<GLsizei>(width), static_cast<GLsizei>(height), 1);
    }

    return true;
}

SyntheticWorkHandle SyntheticOpenGlProvider::Submit(const SyntheticFrameInputs& inputs, void* /*commandList*/) {
    std::scoped_lock lock(mutex_);
    SyntheticWorkHandle handle{};
    if (!ready_ || !inputs.Valid()) return handle;

    if (!CreateSharedResources(inputs.renderResolution.width, inputs.renderResolution.height)) {
        return handle;
    }

    uint32_t slotIdx = currentSlot_;
    currentSlot_ = (currentSlot_ + 1) % kMaxInFlight;

    SharedSlot& slot = sharedSlots_[slotIdx];
    slot.workId = inputs.ticket.id;
    slot.inUse = true;

    // Ensure previous GPU execution for this slot has completed before resetting its allocator
    if (slot.producerFenceValue > 0 && d3d12Fence_->GetCompletedValue() < slot.producerFenceValue) {
        HANDLE evt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (evt) {
            d3d12Fence_->SetEventOnCompletion(slot.producerFenceValue, evt);
            WaitForSingleObject(evt, 100);
            CloseHandle(evt);
        }
    }

    uint64_t fVal = nextFenceValue_++;
    slot.producerFenceValue = fVal;

    SyntheticFrameInputs d12Inputs = inputs;
    d12Inputs.color.opaqueId = reinterpret_cast<uint64_t>(slot.d3d12Color.Get());
    d12Inputs.color.resolution = inputs.renderResolution;
    d12Inputs.color.format = ResourceFormat::Rgba16Float;

    slot.alloc->Reset();
    d3d12CmdList_->Reset(slot.alloc.Get(), nullptr);

    handle = syntheticD3D12_.Submit(d12Inputs, d3d12CmdList_.Get());

    d3d12CmdList_->Close();
    ID3D12CommandList* lists[] = { d3d12CmdList_.Get() };
    d3d12Queue_->ExecuteCommandLists(1, lists);
    d3d12Queue_->Signal(d3d12Fence_.Get(), fVal);

    handle.fenceValue = fVal;
    return handle;
}

bool SyntheticOpenGlProvider::Poll(const SyntheticWorkHandle& handle) {
    if (!ready_ || !handle.valid || !d3d12Fence_) return false;
    return d3d12Fence_->GetCompletedValue() >= handle.fenceValue;
}

ResourceRef SyntheticOpenGlProvider::GetResidual(const SyntheticWorkHandle& handle) {
    std::scoped_lock lock(mutex_);
    ResourceRef ref{};
    if (!ready_ || !handle.valid) return ref;

    for (const auto& s : sharedSlots_) {
        if (s.workId == handle.workId && s.d3d12Residual) {
            ref.opaqueId = reinterpret_cast<uint64_t>(s.d3d12Residual.Get());
            ref.resolution = currentRes_;
            ref.format = ResourceFormat::Rgba16Float;
            return ref;
        }
    }
    return ref;
}

bool SyntheticOpenGlProvider::ComposeNative(const SyntheticWorkHandle& handle,
                                           const ResourceRef& originalNative,
                                           const ResourceRef& destinationNative,
                                           void* commandList,
                                           float residualWeight) {
    return syntheticD3D12_.ComposeNative(handle, originalNative, destinationNative, commandList, residualWeight);
}

} // namespace nrfusion
