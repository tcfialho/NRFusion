#include <Config.h>
void m(){
        bool enabled = config->DlssNrEnabled.value_or_default();
        HelpMarker("Enhance lighting and material appearance with the NR model. Placement selects before or after upscaling.\nRequires nvngx_dlssnr.dll plus the included nvngx.dll_dlssnr.dll helper.");
        bool beforeSr = config->DlssNrRunBeforeSr.value_or_default();
        const auto activeFeature = State::Instance().currentFeature;
        const bool rayReconstruction = activeFeature && activeFeature->GetUpscalerType() == Upscaler::DLSSD;
        ImGui::PushItemWidth(220.0f * menuResScale);
        ImGui::SeparatorText("Performance");
            const bool reduced = config->DlssNrWorkingScale.value_or_default() < 0.999f;
        ImGui::PopItemWidth();
            static const char* sourceNames[] = { "Manual paper white", "Game exposure",
                                                 "Scanned exposure (experimental)" };

            int source = (int) config->DlssNrWhitePointSource.value_or_default();

            if (source < 0 || source > 2)
                source = 0;

            else if (haveExposure)
            {
                ImGui::TextColored(ImVec4(0.45f, 0.8f, 0.45f, 1.0f),
                                   "Game exposure is available.");
            }
}
