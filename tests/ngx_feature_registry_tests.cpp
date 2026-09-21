#include "nrfusion/NgxFeatureRegistry.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

namespace {

std::size_t gAllocations = 0;

struct FakeNrExecutor {
    std::uint64_t calls = 0;

    void Evaluate(const nrfusion::NgxFeatureRegistry& registry,
                  std::uint64_t contextId, std::uintptr_t handle) noexcept {
        if (registry.ActionFor(contextId, handle) == nrfusion::NgxEvaluateAction::NeuralRendering)
            ++calls;
    }
};

}

void* operator new(std::size_t size) {
    ++gAllocations;
    if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
    throw std::bad_alloc();
}

void operator delete(void* p) noexcept {
    std::free(p);
}

void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}

void* operator new[](std::size_t size) {
    ++gAllocations;
    if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
    throw std::bad_alloc();
}

void operator delete[](void* p) noexcept {
    std::free(p);
}

void operator delete[](void* p, std::size_t) noexcept {
    std::free(p);
}

int main() {
    using namespace nrfusion;

    NgxFeatureRegistry registry;
    assert(registry.Size() == 0);

    const auto failed = registry.RecordCreate(
        {0x10, 1, 1, false});
    assert(!failed);
    assert(registry.Size() == 0);
    assert(!registry.Lookup(1, 0x10));

    assert(!registry.RecordCreate(
        {0, 1, 1, true}));
    assert(!registry.RecordCreate(
        {0x10, 0, 1, true}));
    assert(ClassifyNgxFeatureId(1) == NgxFeatureKind::SuperResolution);
    assert(ClassifyNgxFeatureId(11) == NgxFeatureKind::FrameGeneration);
    assert(ClassifyNgxFeatureId(13) == NgxFeatureKind::RayReconstruction);
    assert(ClassifyNgxFeatureId(999) == NgxFeatureKind::Unknown);
    assert(registry.Size() == 0);

    const auto sr = registry.RecordCreate(
        {0x100, 1, 1, true});
    const auto fg = registry.RecordCreate(
        {0x101, 1, 11, true});
    const auto rr = registry.RecordCreate(
        {0x102, 1, 13, true});
    const auto unknown = registry.RecordCreate(
        {0x103, 1, 999, true});
    assert(sr && fg && rr && unknown);

    FakeNrExecutor fakeNr;
    fakeNr.Evaluate(registry, 1, sr.token.handle);
    assert(fakeNr.calls == 1);
    fakeNr.Evaluate(registry, 1, fg.token.handle);
    assert(fakeNr.calls == 1);
    fakeNr.Evaluate(registry, 1, rr.token.handle);
    assert(fakeNr.calls == 2);
    fakeNr.Evaluate(registry, 1, unknown.token.handle);
    fakeNr.Evaluate(registry, 1, 0xFFFF);
    assert(fakeNr.calls == 2);
    assert(registry.ActionFor(sr.token.handle) == NgxEvaluateAction::NeuralRendering);
    assert(registry.ActionFor(fg.token.handle) == NgxEvaluateAction::PassThrough);
    assert(registry.ActionFor(0xFFFF) == NgxEvaluateAction::PassThrough);

    const auto failedReuse = registry.RecordCreate(
        {sr.token.handle, sr.token.contextId, 11, false});
    assert(!failedReuse);
    assert(registry.Lookup(1, sr.token.handle).kind == NgxFeatureKind::SuperResolution);

    registry.Clear();
    const auto viewportSr = registry.RecordCreate(
        {0x200, 11, 1, true});
    const auto viewportFg = registry.RecordCreate(
        {0x200, 12, 11, true});
    assert(viewportSr && viewportFg);
    assert(registry.Lookup(11, 0x200).kind == NgxFeatureKind::SuperResolution);
    assert(registry.Lookup(12, 0x200).kind == NgxFeatureKind::FrameGeneration);
    assert(!registry.LookupUnique(0x200));
    assert(registry.ActionFor(0x200) == NgxEvaluateAction::PassThrough);

    registry.Clear();
    const auto oldIdentity = registry.RecordCreate(
        {0x300, 21, 1, true});
    const auto duplicateCreate = registry.RecordCreate(
        {0x300, 21, 11, true});
    assert(oldIdentity);
    assert(!duplicateCreate);
    assert(registry.Size() == 1);
    assert(registry.Lookup(21, 0x300).kind == NgxFeatureKind::SuperResolution);

    assert(registry.RecordRelease(oldIdentity.token));
    const auto reusedIdentity = registry.RecordCreate(
        {0x300, 21, 11, true});
    assert(reusedIdentity);
    assert(oldIdentity.token.generation != reusedIdentity.token.generation);
    assert(registry.Lookup(21, 0x300).kind == NgxFeatureKind::FrameGeneration);
    assert(!registry.RecordRelease(oldIdentity.token));
    assert(registry.RecordRelease(reusedIdentity.token));
    assert(registry.Size() == 0);
    assert(!registry.RecordRelease(reusedIdentity.token));

    const auto beforeClear = registry.RecordCreate(
        {0x301, 21, 13, true});
    registry.Clear();
    const auto afterClear = registry.RecordCreate(
        {0x301, 21, 13, true});
    assert(beforeClear.token.generation != afterClear.token.generation);
    assert(!registry.RecordRelease(beforeClear.token));
    assert(registry.RecordRelease(afterClear.token));

    std::array<NgxFeatureToken, NgxFeatureRegistry::kCapacity> capacityTokens{};
    for (std::size_t i = 0; i < capacityTokens.size(); ++i) {
        const auto created = registry.RecordCreate(
            {0x1000 + i, 31, 999, true});
        assert(created);
        capacityTokens[i] = created.token;
    }
    assert(registry.Size() == NgxFeatureRegistry::kCapacity);
    assert(!registry.RecordCreate(
        {0xFFFF, 31, 1, true}));
    for (const auto token : capacityTokens) assert(registry.RecordRelease(token));
    assert(registry.Size() == 0);

    constexpr std::uint32_t kLifecycleIterations = 100000;
    for (std::uint32_t i = 0; i < kLifecycleIterations; ++i) {
        const std::int32_t featureId = (i & 1u) == 0 ? 1 : 11;
        const auto created = registry.RecordCreate({0x400, 41, featureId, true});
        assert(created);
        assert(registry.Lookup(41, 0x400).kind == ClassifyNgxFeatureId(featureId));
        assert(registry.RecordRelease(created.token));
    }
    assert(registry.Size() == 0);

    const auto hotSr = registry.RecordCreate(
        {0x500, 51, 1, true});
    const auto hotFg = registry.RecordCreate(
        {0x501, 51, 11, true});
    assert(hotSr && hotFg);

    const std::size_t allocationsBefore = gAllocations;
    std::uint64_t checksum = 0;
    constexpr std::uint32_t kLookupIterations = 1000000;
    for (std::uint32_t i = 0; i < kLookupIterations; ++i) {
        const auto handle = (i & 1u) == 0 ? hotSr.token.handle : hotFg.token.handle;
        const auto identity = registry.Lookup(51, handle);
        checksum += identity.token.generation;
        checksum += registry.ActionFor(51, handle) == NgxEvaluateAction::NeuralRendering ? 1u : 0u;
    }
    assert(checksum != 0);
    assert(gAllocations == allocationsBefore);

    return 0;
}
