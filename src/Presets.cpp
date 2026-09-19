#include "nrfusion/Presets.hpp"
#include <algorithm>
#include <cmath>

namespace nrfusion {

PerformanceConfig MakePerformanceConfig(PerformancePreset preset, double targetFps) {
    PerformanceConfig c;
    c.targetFps = std::clamp(std::isfinite(targetFps) ? targetFps : 120.0, 30.0, 1000.0);
    c.automaticNrBudget = true;

    switch (preset) {
    case PerformancePreset::Aggressive:
        c.minScale = 0.35f;
        c.maxScale = 0.75f;
        c.scaleSteps = {0.75f, 0.67f, 0.58f, 0.50f, 0.42f, 0.35f};
        c.nrBudgetFrameFraction = 0.24;
        c.minNrBudgetMs = 0.75;
        c.maxNrBudgetMs = 3.0;
        c.scaleDownSustainSeconds = 0.40;
        c.scaleUpSustainSeconds = 6.0;
        c.cooldownSeconds = 2.0;
        c.frameNrMaterialRatio = 0.50;
        c.maxPredictiveStepDrop = 3;
        break;
    case PerformancePreset::Performance:
        // Mesmo teto dos outros modos adaptativos: com folga, a qualidade sobe ate 100%. O que
        // muda e o piso, mais baixo, e a disposicao de descer ate ele mais cedo.
        c.minScale = 0.35f;
        c.maxScale = 1.0f;
        c.scaleSteps = {1.0f, 0.85f, 0.75f, 0.67f, 0.58f, 0.50f, 0.42f, 0.35f};
        c.nrBudgetFrameFraction = 0.28;
        c.minNrBudgetMs = 0.85;
        c.maxNrBudgetMs = 5.0;
        c.scaleDownSustainSeconds = 0.75;
        c.scaleUpSustainSeconds = 5.0;
        c.cooldownSeconds = 2.0;
        c.frameNrMaterialRatio = 0.65;
        c.nrOverBudgetRatio = 1.10;
        c.maxPredictiveStepDrop = 2;
        break;
    case PerformancePreset::Quality:
        // Piso alto: prefere perder o alvo de FPS a descer a resolucao neural abaixo de 67%.
        c.minScale = 0.67f;
        c.maxScale = 1.0f;
        c.scaleSteps = {1.0f, 0.85f, 0.75f, 0.67f};
        c.nrBudgetFrameFraction = 0.45;
        c.minNrBudgetMs = 1.2;
        c.maxNrBudgetMs = 6.0;
        c.scaleUpSustainSeconds = 3.0;
        c.frameNrMaterialRatio = 0.90;
        c.maxPredictiveStepDrop = 1;
        break;
    case PerformancePreset::Balanced:
        c.minScale = 0.50f;
        c.maxScale = 1.0f;
        c.scaleSteps = {1.0f, 0.85f, 0.75f, 0.67f, 0.58f, 0.50f};
        c.nrBudgetFrameFraction = 0.35;
        c.minNrBudgetMs = 1.0;
        c.maxNrBudgetMs = 5.0;
        c.frameNrMaterialRatio = 0.75;
        c.maxPredictiveStepDrop = 2;
        break;
    case PerformancePreset::Auto:
    default:
        c.minScale = 0.42f;
        c.maxScale = 1.0f;
        c.scaleSteps = {1.0f, 0.85f, 0.75f, 0.67f, 0.58f, 0.50f, 0.42f};
        c.nrBudgetFrameFraction = 0.30;
        c.minNrBudgetMs = 0.9;
        // Ada/4050 validation shows a normal NR pass close to 5 ms at 60 Hz. Do not clamp the
        // 30%-of-frame budget below that measured value, and do not react to a single clock/pacing
        // excursion as if it were sustained overload. Higher targets still tighten the budget
        // through the frame-fraction calculation.
        c.maxNrBudgetMs = 6.0;
        c.scaleDownSustainSeconds = 1.75;
        c.scaleUpSustainSeconds = 5.0;
        c.cooldownSeconds = 3.0;
        c.frameNrMaterialRatio = 0.85;
        // A 4050's measured 0.50x pass oscillates around 5--6 ms while the 60-FPS budget is
        // 5 ms. Treat that band as tolerable headroom usage; only a sustained median above 6.25 ms
        // should spend the next quality rung.
        c.nrOverBudgetRatio = 1.25;
        c.maxPredictiveStepDrop = 1;
        break;
    }
    return c;
}

} // namespace nrfusion
