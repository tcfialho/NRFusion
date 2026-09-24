#include "nrfusion/VulkanCarrierExecutor.hpp"

namespace nrfusion {
namespace {

VkImageLayout NativeLayout(VulkanImageLayoutIntent layout) noexcept {
    switch (layout) {
    case VulkanImageLayoutIntent::General:
        return VK_IMAGE_LAYOUT_GENERAL;
    case VulkanImageLayoutIntent::ShaderRead:
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    case VulkanImageLayoutIntent::TransferSource:
        return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    case VulkanImageLayoutIntent::TransferDestination:
        return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    case VulkanImageLayoutIntent::Undefined:
        return VK_IMAGE_LAYOUT_UNDEFINED;
    }
    return VK_IMAGE_LAYOUT_UNDEFINED;
}

bool CanCompose(
    const VulkanCommandDispatch& dispatch,
    const VulkanCarrierExecutionPlan& plan) noexcept {
    if (!dispatch.pipelineBarrier) return false;
    const bool sameExtent =
        plan.colorResolution == plan.outputResolution;
    return sameExtent
        ? dispatch.copyImage || dispatch.blitImage
        : dispatch.blitImage != nullptr;
}

bool RecordLocalTransition(
    const VulkanCommandDispatch& dispatch,
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout) noexcept {
    if (oldLayout == newLayout) return true;
    return RecordVulkanImageTransition(
        dispatch, commandBuffer, image, oldLayout, newLayout);
}

bool RecordAcquire(
    const VulkanCarrierExecutionContext& context,
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageLayout initialLayout,
    VkImageLayout executionLayout,
    bool externalInterop) noexcept {
    if (externalInterop) {
        return RecordVulkanExternalImageAcquire(
            context.commands, commandBuffer, image,
            initialLayout, executionLayout,
            context.native.queueFamilyIndex);
    }
    return RecordLocalTransition(
        context.commands, commandBuffer, image,
        initialLayout, executionLayout);
}

bool RecordRelease(
    const VulkanCarrierExecutionContext& context,
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageLayout executionLayout,
    VkImageLayout initialLayout,
    bool externalInterop) noexcept {
    if (externalInterop) {
        return RecordVulkanExternalImageRelease(
            context.commands, commandBuffer, image,
            executionLayout, initialLayout,
            context.native.queueFamilyIndex);
    }
    return RecordLocalTransition(
        context.commands, commandBuffer, image,
        executionLayout, initialLayout);
}

} // namespace

VulkanCarrierExecuteResult VulkanCarrierExecutor::Execute(
    VkCommandBuffer commandBuffer,
    VulkanCarrierSession& session,
    const VulkanCarrierExecutionContext& context,
    const VulkanCarrierFrameResult& frame,
    const VulkanCarrierWork& work,
    const VulkanCarrierNativeResources& resources) const noexcept {
    VulkanCarrierExecuteResult result{};

    const auto planned = BuildVulkanCarrierExecutionPlan(
        session, frame, work, resources.facts);
    if (!planned) {
        result.failure = VulkanCarrierExecuteFailure::Planning;
        result.planningFailure = planned.failure;
        return result;
    }
    result.plan = planned.plan;

    if (!context.native.Valid()) {
        result.failure = VulkanCarrierExecuteFailure::InvalidContext;
        return result;
    }
    if (commandBuffer == VK_NULL_HANDLE) {
        result.failure = VulkanCarrierExecuteFailure::InvalidCommandBuffer;
        return result;
    }
    if (!context.commands.Valid() ||
        !CanCompose(context.commands, result.plan)) {
        result.failure = VulkanCarrierExecuteFailure::InvalidDispatch;
        return result;
    }
    if (resources.color == VK_NULL_HANDLE ||
        resources.output == VK_NULL_HANDLE ||
        resources.color == resources.output) {
        result.failure = VulkanCarrierExecuteFailure::MissingImage;
        return result;
    }
    if (context.native.queueFamilyIndex != result.plan.queueFamilyIndex) {
        result.failure = VulkanCarrierExecuteFailure::QueueFamilyMismatch;
        return result;
    }

    const VkImageLayout colorInitial =
        NativeLayout(result.plan.colorInitialLayout);
    const VkImageLayout outputInitial =
        NativeLayout(result.plan.outputInitialLayout);
    const VkImageLayout colorExecution =
        NativeLayout(result.plan.colorExecutionLayout);
    const VkImageLayout outputExecution =
        NativeLayout(result.plan.outputExecutionLayout);
    if (colorInitial == VK_IMAGE_LAYOUT_UNDEFINED ||
        outputInitial == VK_IMAGE_LAYOUT_UNDEFINED ||
        colorExecution == VK_IMAGE_LAYOUT_UNDEFINED ||
        outputExecution == VK_IMAGE_LAYOUT_UNDEFINED) {
        result.failure = VulkanCarrierExecuteFailure::Planning;
        result.planningFailure =
            VulkanCarrierExecutionFailure::InvalidLayout;
        return result;
    }

    if (!session.ConsumeClaimedExecution(work)) {
        result.failure = VulkanCarrierExecuteFailure::ClaimUnavailable;
        return result;
    }
    result.claimConsumed = true;
    result.attempted = true;

    const bool recorded =
        RecordAcquire(
            context, commandBuffer, resources.color,
            colorInitial, colorExecution,
            result.plan.externalInterop) &&
        RecordAcquire(
            context, commandBuffer, resources.output,
            outputInitial, outputExecution,
            result.plan.externalInterop) &&
        RecordVulkanCopyOrBlit(
            context.commands, commandBuffer,
            resources.color,
            result.plan.colorResolution.width,
            result.plan.colorResolution.height,
            resources.output,
            result.plan.outputResolution.width,
            result.plan.outputResolution.height,
            colorExecution, outputExecution) &&
        RecordRelease(
            context, commandBuffer, resources.output,
            outputExecution, outputInitial,
            result.plan.externalInterop) &&
        RecordRelease(
            context, commandBuffer, resources.color,
            colorExecution, colorInitial,
            result.plan.externalInterop);

    if (!recorded)
        result.failure = VulkanCarrierExecuteFailure::RecordingFailed;
    return result;
}

} // namespace nrfusion
