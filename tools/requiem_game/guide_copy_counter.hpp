#pragma once
#include <windows.h>
#include <d3d12.h>
#include <stdexcept>

namespace requiem {
class GuideCopyCounter {
public:
    std::uint64_t copies = 0;
    std::uint64_t bytes = 0;
    GuideCopyCounter(ID3D12GraphicsCommandList* commands, ID3D12Resource* depth,
        ID3D12Resource* motion, bool enabled) : depth_(depth), motion_(motion) {
        if (!enabled) return;
        if (active_ != nullptr) throw std::runtime_error("Guide copy counter already attached");
        auto** methods = *reinterpret_cast<void***>(commands);
        slot_ = &methods[17];
        original_ = reinterpret_cast<CopyFunction>(*slot_);
        depthBytes_ = depth->GetDesc().Width * depth->GetDesc().Height * 4;
        motionBytes_ = motion->GetDesc().Width * motion->GetDesc().Height * 8;
        active_ = this;
        Replace(reinterpret_cast<void*>(&Copy));
    }
    ~GuideCopyCounter() {
        if (!slot_) return;
        Replace(reinterpret_cast<void*>(original_));
        active_ = nullptr;
    }
    void Reset() noexcept { copies = bytes = 0; }
    void Reattach(ID3D12GraphicsCommandList* commands) {
        if (!slot_) return;
        auto** methods = *reinterpret_cast<void***>(commands);
        if (&methods[17] == slot_ && *slot_ == reinterpret_cast<void*>(&Copy)) return;
        Replace(reinterpret_cast<void*>(original_));
        slot_ = &methods[17];
        original_ = reinterpret_cast<CopyFunction>(*slot_);
        Replace(reinterpret_cast<void*>(&Copy));
    }
    GuideCopyCounter(const GuideCopyCounter&) = delete;
    GuideCopyCounter& operator=(const GuideCopyCounter&) = delete;
private:
    using CopyFunction = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12Resource*, ID3D12Resource*);
    static inline GuideCopyCounter* active_ = nullptr;
    void** slot_ = nullptr;
    CopyFunction original_ = nullptr;
    ID3D12Resource* depth_ = nullptr;
    ID3D12Resource* motion_ = nullptr;
    std::uint64_t depthBytes_ = 0, motionBytes_ = 0;
    void Replace(void* function) {
        DWORD protection = 0;
        if (!VirtualProtect(slot_, sizeof(void*), PAGE_READWRITE, &protection))
            throw std::runtime_error("Guide counter vtable protection failed");
        InterlockedExchangePointer(slot_, function);
        DWORD ignored = 0;
        if (!VirtualProtect(slot_, sizeof(void*), protection, &ignored))
            throw std::runtime_error("Guide counter vtable restore failed");
    }
    static void STDMETHODCALLTYPE Copy(ID3D12GraphicsCommandList* commands, ID3D12Resource* target, ID3D12Resource* source) {
        if (source == active_->depth_ || source == active_->motion_) {
            ++active_->copies;
            active_->bytes += source == active_->depth_ ? active_->depthBytes_ : active_->motionBytes_;
        }
        active_->original_(commands, target, source);
    }
};
}
