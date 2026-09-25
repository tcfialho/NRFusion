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
        if (slot.outputSharedHandle) {
            CloseHandle(slot.outputSharedHandle);
            slot.outputSharedHandle = nullptr;
        }

        if (gl_.hasInterop && contextCurrent) {
            if (slot.glColorTex) glDeleteTextures(1, &slot.glColorTex);
            if (slot.glColorMem)
                gl_.DeleteMemoryObjectsEXT(1, &slot.glColorMem);
            if (slot.glOutputTex)
                glDeleteTextures(1, &slot.glOutputTex);
            if (slot.glOutputMem)
                gl_.DeleteMemoryObjectsEXT(1, &slot.glOutputMem);
            if (slot.glInputSem)
                gl_.DeleteSemaphoresEXT(1, &slot.glInputSem);
            if (slot.glOutputSem)
                gl_.DeleteSemaphoresEXT(1, &slot.glOutputSem);
        }

        slot.glColorTex = 0;
        slot.glColorMem = 0;
        slot.glOutputTex = 0;
        slot.glOutputMem = 0;
        slot.glInputSem = 0;
        slot.glOutputSem = 0;
        slot.d3d12Color.Reset();
        slot.d3d12Output.Reset();
        slot.sync = {};
        slot.innerSlot = SyntheticDx12Provider::kRingSlots;
        slot.inputRecorded = false;
        slot.outputPublished = false;
        slot.outputConsumed = false;
        slot.inUse = false;
    }
    currentRes_ = {};
}

bool SyntheticOpenGlProvider::CanRecreateSharedResources() const noexcept {
    for (const auto& slot : sharedSlots_) {
        if (!slot.inUse) continue;
        if (!slot.outputConsumed ||
            !slot.sync.Valid(kMaxInFlight) ||
            !slot.fence ||
            slot.fence->GetCompletedValue() <
                slot.sync.releaseSignalValue) {
            return false;
        }
    }
    return true;
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
    if (currentRes_.Valid() && !CanRecreateSharedResources())
        return false;

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
                IID_PPV_ARGS(&slot.d3d12Output))) ||
            FAILED(d3d12Device_->CreateSharedHandle(
                slot.d3d12Output.Get(), nullptr, GENERIC_ALL, nullptr,
                &slot.outputSharedHandle))) {
            CloseSharedHandles();
            return false;
        }

        gl_.CreateMemoryObjectsEXT(1, &slot.glColorMem);
        gl_.CreateMemoryObjectsEXT(1, &slot.glOutputMem);
        if (!slot.glColorMem || !slot.glOutputMem) {
            CloseSharedHandles();
            return false;
        }

        const GLint dedicated = GL_TRUE;
        gl_.MemoryObjectParameterivEXT(
            slot.glColorMem, GL_DEDICATED_MEMORY_OBJECT_EXT, &dedicated);
        gl_.MemoryObjectParameterivEXT(
            slot.glOutputMem, GL_DEDICATED_MEMORY_OBJECT_EXT, &dedicated);
        gl_.ImportMemoryWin32HandleEXT(
            slot.glColorMem, 0,
            GL_HANDLE_TYPE_D3D12_RESOURCE_EXT, slot.colorSharedHandle);
        gl_.ImportMemoryWin32HandleEXT(
            slot.glOutputMem, 0,
            GL_HANDLE_TYPE_D3D12_RESOURCE_EXT, slot.outputSharedHandle);

        glGenTextures(1, &slot.glColorTex);
        glBindTexture(GL_TEXTURE_2D, slot.glColorTex);
        gl_.TexStorageMem2DEXT(
            GL_TEXTURE_2D, 1, GL_RGBA16F,
            width, height, slot.glColorMem, 0);
        glGenTextures(1, &slot.glOutputTex);
        glBindTexture(GL_TEXTURE_2D, slot.glOutputTex);
        gl_.TexStorageMem2DEXT(
            GL_TEXTURE_2D, 1, GL_RGBA16F,
            width, height, slot.glOutputMem, 0);
        glBindTexture(GL_TEXTURE_2D, 0);

        gl_.GenSemaphoresEXT(1, &slot.glInputSem);
        gl_.GenSemaphoresEXT(1, &slot.glOutputSem);
        if (!slot.glInputSem || !slot.glOutputSem) {
            CloseSharedHandles();
            return false;
        }
        if (!slot.fence || !slot.fenceSharedHandle) {
            CloseSharedHandles();
            return false;
        }
        gl_.ImportSemaphoreWin32HandleEXT(
            slot.glInputSem, GL_HANDLE_TYPE_D3D12_FENCE_EXT,
            slot.fenceSharedHandle);
        gl_.ImportSemaphoreWin32HandleEXT(
            slot.glOutputSem, GL_HANDLE_TYPE_D3D12_FENCE_EXT,
            slot.fenceSharedHandle);
    }

    currentRes_ = {width, height};
    nvof_.Initialize(
        d3d12Device_.Get(), d3d12Queue_.Get(), width, height);
    return true;
}

bool SyntheticOpenGlProvider::RecordOpenGlInputCopy(
    SharedSlot& slot,
    GLuint gameColorTex,
    uint32_t width,
    uint32_t height) {
    if (!ready_ || !gl_.hasInterop ||
        !slot.sync.Valid(kMaxInFlight) ||
        !gl_.wglGetCurrentContext ||
        gl_.wglGetCurrentContext() == nullptr ||
        gameColorTex == 0 || width == 0 || height == 0 ||
        !slot.glColorTex || !slot.glInputSem) {
        return false;
    }

    const uint64_t inputValue = slot.sync.inputSignalValue;
    gl_.SemaphoreParameterui64vEXT(
        slot.glInputSem, GL_D3D12_FENCE_VALUE_EXT, &inputValue);
    gl_.CopyImageSubData(
        gameColorTex, GL_TEXTURE_2D, 0, 0, 0, 0,
        slot.glColorTex, GL_TEXTURE_2D, 0, 0, 0, 0,
        static_cast<GLsizei>(width), static_cast<GLsizei>(height), 1);
    const GLenum layout = GL_LAYOUT_GENERAL_EXT;
    gl_.SignalSemaphoreEXT(
        slot.glInputSem, 0, nullptr, 1, &slot.glColorTex, &layout);
    slot.inputRecorded = true;
    return true;
}

SyntheticOpenGlProvider::SharedSlot*
SyntheticOpenGlProvider::FindSlot(
    const SyntheticWorkHandle& handle) noexcept {
    if (!handle.valid || handle.workId == 0) return nullptr;
    for (auto& slot : sharedSlots_) {
        if (slot.inUse &&
            slot.sync.workId == handle.workId &&
            slot.sync.outputSignalValue == handle.fenceValue) {
            return &slot;
        }
    }
    return nullptr;
}

bool SyntheticOpenGlProvider::RecordOpenGlOutputConsume(
    const SyntheticWorkHandle& handle,
    GLuint gameDestTex,
    uint32_t width,
    uint32_t height) {
    std::scoped_lock lock(mutex_);
    if (!ready_ || !gl_.hasInterop ||
        !gl_.wglGetCurrentContext ||
        gl_.wglGetCurrentContext() == nullptr ||
        gameDestTex == 0 || width == 0 || height == 0) {
        return false;
    }

    SharedSlot* slot = FindSlot(handle);
    if (!slot || !slot->outputPublished || slot->outputConsumed ||
        !slot->glOutputTex || !slot->glOutputSem ||
        !slot->glInputSem) {
        return false;
    }

    const GLenum layout = GL_LAYOUT_GENERAL_EXT;
    const uint64_t outputValue = slot->sync.outputSignalValue;
    gl_.SemaphoreParameterui64vEXT(
        slot->glOutputSem, GL_D3D12_FENCE_VALUE_EXT, &outputValue);
    gl_.WaitSemaphoreEXT(
        slot->glOutputSem, 0, nullptr,
        1, &slot->glOutputTex, &layout);
    gl_.CopyImageSubData(
        slot->glOutputTex, GL_TEXTURE_2D, 0, 0, 0, 0,
        gameDestTex, GL_TEXTURE_2D, 0, 0, 0, 0,
        static_cast<GLsizei>(width), static_cast<GLsizei>(height), 1);

    const uint64_t releaseValue = slot->sync.releaseSignalValue;
    gl_.SemaphoreParameterui64vEXT(
        slot->glInputSem, GL_D3D12_FENCE_VALUE_EXT, &releaseValue);
    const GLuint textures[] = {
        slot->glColorTex, slot->glOutputTex};
    const GLenum layouts[] = {
        GL_LAYOUT_GENERAL_EXT, GL_LAYOUT_GENERAL_EXT};
    gl_.SignalSemaphoreEXT(
        slot->glInputSem, 0, nullptr, 2, textures, layouts);
    slot->outputConsumed = true;
    return true;
}

} // namespace nrfusion
