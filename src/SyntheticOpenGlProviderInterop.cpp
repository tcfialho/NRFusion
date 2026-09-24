#include "nrfusion/SyntheticOpenGlProvider.hpp"

namespace nrfusion {

void SyntheticOpenGlProvider::CloseSharedHandles() {
    const bool contextCurrent =
        gl_.wglGetCurrentContext &&
        gl_.wglGetCurrentContext() != nullptr;

    for (auto& slot : sharedSlots_) {
        if (slot.colorSharedHandle) {
            CloseHandle(slot.colorSharedHandle);
            slot.colorSharedHandle = nullptr;
        }
        if (slot.residualSharedHandle) {
            CloseHandle(slot.residualSharedHandle);
            slot.residualSharedHandle = nullptr;
        }

        if (gl_.hasInterop && contextCurrent) {
            if (slot.glColorTex) glDeleteTextures(1, &slot.glColorTex);
            if (slot.glColorMem)
                gl_.DeleteMemoryObjectsEXT(1, &slot.glColorMem);
            if (slot.glResidualTex)
                glDeleteTextures(1, &slot.glResidualTex);
            if (slot.glResidualMem)
                gl_.DeleteMemoryObjectsEXT(1, &slot.glResidualMem);
            if (slot.glInputSem)
                gl_.DeleteSemaphoresEXT(1, &slot.glInputSem);
            if (slot.glOutputSem)
                gl_.DeleteSemaphoresEXT(1, &slot.glOutputSem);
        }

        slot.glColorTex = 0;
        slot.glColorMem = 0;
        slot.glResidualTex = 0;
        slot.glResidualMem = 0;
        slot.glInputSem = 0;
        slot.glOutputSem = 0;
        slot.d3d12Color.Reset();
        slot.d3d12Residual.Reset();
        slot.inUse = false;
        slot.workId = 0;
        slot.producerFenceValue = 0;
    }
    currentRes_ = {};
}

bool SyntheticOpenGlProvider::CreateSharedResources(
    uint32_t width, uint32_t height) {
    if (!ready_ || !gl_.hasInterop ||
        !gl_.wglGetCurrentContext ||
        gl_.wglGetCurrentContext() == nullptr ||
        width == 0 || height == 0) {
        return false;
    }
    if (currentRes_.width == width && currentRes_.height == height)
        return true;

    CloseSharedHandles();

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    const auto allocation =
        d3d12Device_->GetResourceAllocationInfo(0, 1, &desc);
    if (allocation.SizeInBytes == 0) return false;

    for (auto& slot : sharedSlots_) {
        if (FAILED(d3d12Device_->CreateCommittedResource(
                &heapProps, D3D12_HEAP_FLAG_SHARED, &desc,
                D3D12_RESOURCE_STATE_COMMON, nullptr,
                IID_PPV_ARGS(&slot.d3d12Color))) ||
            FAILED(d3d12Device_->CreateSharedHandle(
                slot.d3d12Color.Get(), nullptr, GENERIC_ALL, nullptr,
                &slot.colorSharedHandle)) ||
            FAILED(d3d12Device_->CreateCommittedResource(
                &heapProps, D3D12_HEAP_FLAG_SHARED, &desc,
                D3D12_RESOURCE_STATE_COMMON, nullptr,
                IID_PPV_ARGS(&slot.d3d12Residual))) ||
            FAILED(d3d12Device_->CreateSharedHandle(
                slot.d3d12Residual.Get(), nullptr, GENERIC_ALL, nullptr,
                &slot.residualSharedHandle))) {
            CloseSharedHandles();
            return false;
        }

        gl_.CreateMemoryObjectsEXT(1, &slot.glColorMem);
        gl_.CreateMemoryObjectsEXT(1, &slot.glResidualMem);
        if (!slot.glColorMem || !slot.glResidualMem) {
            CloseSharedHandles();
            return false;
        }

        const GLint dedicated = GL_TRUE;
        gl_.MemoryObjectParameterivEXT(
            slot.glColorMem, GL_DEDICATED_MEMORY_OBJECT_EXT, &dedicated);
        gl_.MemoryObjectParameterivEXT(
            slot.glResidualMem, GL_DEDICATED_MEMORY_OBJECT_EXT, &dedicated);
        gl_.ImportMemoryWin32HandleEXT(
            slot.glColorMem, allocation.SizeInBytes,
            GL_HANDLE_TYPE_D3D12_RESOURCE_EXT, slot.colorSharedHandle);
        gl_.ImportMemoryWin32HandleEXT(
            slot.glResidualMem, allocation.SizeInBytes,
            GL_HANDLE_TYPE_D3D12_RESOURCE_EXT, slot.residualSharedHandle);

        glGenTextures(1, &slot.glColorTex);
        glBindTexture(GL_TEXTURE_2D, slot.glColorTex);
        gl_.TexStorageMem2DEXT(
            GL_TEXTURE_2D, 1, GL_RGBA16F,
            width, height, slot.glColorMem, 0);
        glGenTextures(1, &slot.glResidualTex);
        glBindTexture(GL_TEXTURE_2D, slot.glResidualTex);
        gl_.TexStorageMem2DEXT(
            GL_TEXTURE_2D, 1, GL_RGBA16F,
            width, height, slot.glResidualMem, 0);
        glBindTexture(GL_TEXTURE_2D, 0);

        gl_.GenSemaphoresEXT(1, &slot.glInputSem);
        gl_.GenSemaphoresEXT(1, &slot.glOutputSem);
        if (!slot.glInputSem || !slot.glOutputSem) {
            CloseSharedHandles();
            return false;
        }
        gl_.ImportSemaphoreWin32HandleEXT(
            slot.glInputSem, GL_HANDLE_TYPE_D3D12_FENCE_EXT,
            d3d12FenceSharedHandle_);
        gl_.ImportSemaphoreWin32HandleEXT(
            slot.glOutputSem, GL_HANDLE_TYPE_D3D12_FENCE_EXT,
            d3d12FenceSharedHandle_);
    }

    currentRes_ = {width, height};
    nvof_.Initialize(
        d3d12Device_.Get(), d3d12Queue_.Get(), width, height);
    return true;
}

bool SyntheticOpenGlProvider::RecordOpenGlInputCopy(
    GLuint gameColorTex, uint32_t width, uint32_t height) {
    if (!ready_ || !gl_.hasInterop || gameColorTex == 0 ||
        width == 0 || height == 0) {
        return false;
    }
    SharedSlot& slot = sharedSlots_[currentSlot_];
    if (!slot.glColorTex || !slot.glInputSem) return false;

    gl_.CopyImageSubData(
        gameColorTex, GL_TEXTURE_2D, 0, 0, 0, 0,
        slot.glColorTex, GL_TEXTURE_2D, 0, 0, 0, 0,
        static_cast<GLsizei>(width), static_cast<GLsizei>(height), 1);
    gl_.SignalSemaphoreEXT(
        slot.glInputSem, 0, nullptr, 1, &slot.glColorTex, nullptr);
    return true;
}

bool SyntheticOpenGlProvider::RecordOpenGlOutputConsume(
    GLuint gameDestTex, uint32_t width, uint32_t height) {
    if (!ready_ || !gl_.hasInterop || gameDestTex == 0 ||
        width == 0 || height == 0) {
        return false;
    }
    SharedSlot& slot = sharedSlots_[currentSlot_];
    if (!slot.glResidualTex || !slot.glOutputSem) return false;

    gl_.WaitSemaphoreEXT(
        slot.glOutputSem, 0, nullptr, 1, &slot.glResidualTex, nullptr);
    gl_.CopyImageSubData(
        slot.glResidualTex, GL_TEXTURE_2D, 0, 0, 0, 0,
        gameDestTex, GL_TEXTURE_2D, 0, 0, 0, 0,
        static_cast<GLsizei>(width), static_cast<GLsizei>(height), 1);
    return true;
}

} // namespace nrfusion
