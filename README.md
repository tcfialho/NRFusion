# NRFusion

**NRFusion** is an intermediate software layer integrated into OptiScaler, designed to optimize the performance of neural rendering pipelines (DLSS Ray Reconstruction / Neural Rendering) in PC games.

---

## Overview and Architecture

NRFusion intercepts graphics workload dispatches between the game engine and the graphics card (GPU).

Rather than evaluating the neural network model at full native resolution on every rendered frame, NRFusion operates through the following mechanisms:
- **Real-Time GPU Workload Telemetry:** Continuously measures per-frame execution times and the specific GPU duration consumed by the neural pass.
- **Dynamic Resolution Scaling (WorkingScale):** Dynamically adjusts the internal input resolution processed by the neural model to sustain the user-defined frame rate target (*Target FPS*).
- **Residual Data Transfer:** Projects temporal information and image residuals to reconstruct sharp visual output and detail without requiring full-scale neural model evaluation.
- **Hardware Stability and Fallback Safety:** Preserves native numerical precision formats on supported GPUs (FP8/INT8), preventing stalls and ensuring smooth resolution transitions.

---

## How to Install

1. Download the official installer executable (`NRFusionSetup.exe`).
2. Launch the installer and select the target game folder or the primary game executable (`.exe`).
3. The installer automatically detects the active graphics API (DirectX 11, DirectX 12, or Vulkan) and deploys the appropriate proxy library (such as `dxgi.dll`) without file conflicts.
4. Complete the setup wizard and launch the game normally.

---

## How to Configure and Use

After launching the game:
1. Press the **`Insert`** key to display the in-game overlay menu.
2. In the configuration panel, ensure that **Neural Rendering** is enabled:
   ```text
   Neural Rendering: [On]
   ```
3. Select the desired **Operating Mode**:
   - **`Auto` (Recommended):** The system continuously monitors GPU performance and adjusts the internal neural resolution automatically to sustain the target FPS. No manual tuning is required.
   - **`Best quality`:** Keeps the internal model resolution at higher thresholds to prioritize visual detail, indicating when the GPU cannot sustain the target frame rate.
   - **`Custom`:** Unlocks manual technical controls, including explicit precision selection, compute backends, and fixed resolution scaling.
4. Set the **Target FPS**:
   - Specify your desired frame rate target (e.g., 60, 120, or 144 FPS). The `Auto` mode uses this target to dynamically balance visual quality and frame rate stability.

---

## Uninstallation

To remove NRFusion from a game:
- Open the Windows Settings / Control Panel > *Installed Apps* (or *Programs and Features*) and uninstall NRFusion, or run the `uninstall.exe` created inside the game directory.

---

## System Requirements

- **GPU:** NVIDIA GeForce RTX Graphics Card (RTX 20, 30, or 40 Series).
- **Operating System:** Windows 10 or Windows 11 (64-bit).
- **Games:** Titles running on DirectX 11, DirectX 12, or Vulkan with DLSS support.

---

## Building and Testing (Developers)

To build the source code and run the automated test suite locally:

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```
