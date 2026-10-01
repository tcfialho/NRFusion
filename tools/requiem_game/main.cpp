// NRFusion Requiem: a Direct3D 12 testbed for looking at the neural pass on a real frame.
//
// The original was lost on 14/09/2026 and its source exists nowhere. This is a rebuild. The
// flags, the keys and the console banner match what the session log preserved of the old one,
// so the scripts that drove it still work; the body is new.
//
// Renders the OFF image into the upscaler input, evaluates NGX every frame and presents its
// computed output. TAB selects input, computed output or the NVIDIA JPEG reference.
// NGX success alone does not prove NR: the proxy may choose another upscaler or bypass NR.
// measure_requiem.py checks host NR evidence and keeps whole-NGX timings separately labelled.
//
// --fixed-scene | --deterministic-motion
// --width 1280 --height 720 | --width 1920 --height 1080
// --frames 720 --warmup 120 --csv <path> --capture <path.ppm>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <d3d12sdklayers.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

#include <wrl/client.h>

#include "image.hpp"
#include "ngx_dlss.hpp"
#include "frame_resources.hpp"
#include "guide_copy_counter.hpp"
#include "streamline_mfg.hpp"
#include "nrfusion/NrDiagnosticsApi.hpp"
#include <memory>
#include <charconv>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT kBackBuffers = 3;

HWND g_hwnd = nullptr;
bool g_running = true;
// Test-only deterministic scene mode. It keeps the render inputs, camera and lighting fixed so
// captures made at different WorkingScale values can be compared without a time-dependent scene.
bool g_fixedScene = false;
// Test-only deterministic motion mode. It advances animation from the rendered-frame number so
// two runs follow the same motion sequence even when wall-clock pacing differs.
bool g_deterministicMotion = false;
bool g_debugLayer = false;
bool g_typelessGuides = false;
unsigned g_view = 1; // 0 input, 1 calculated, 2 NVIDIA reference
std::uint64_t g_width = 1920, g_height = 1080;
std::uint64_t g_warmup = 120;
std::string g_csv, g_capture;
std::string g_kernelCsv;
bool g_requireNr = false;
bool g_nrLifecycle = false;
bool g_requireNrfusionProxy = false;
bool g_openMenu = false;
bool g_requireMenu = false;
std::uint32_t g_mfgMultiplier = 0;
bool g_requireMfg = false;
std::uint64_t g_frameLimit = 0;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE) g_running = false;
        if (wparam == VK_TAB) g_view = (g_view + 1) % 3;
        return 0;
    case WM_DESTROY:
        g_running = false;
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

void Check(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        std::cout << "[ERRO] " << what << " (0x" << std::hex << hr << std::dec << ")" << std::endl;
        std::exit(1);
    }
}

// A full-screen pass over the reference frame. The slow pan is not decoration: an upscaler with
// no motion at all takes a path it never takes in a game, and the neural pass would be judged
// on a problem it does not actually face.
const char* kShader = R"(
Texture2D<float4> g_image : register(t0);
SamplerState      g_sampler : register(s0);
cbuffer Frame : register(b0) { float4 pan; };   // xy: offset, z: zoom, w: unused

struct VSOut { float4 position : SV_Position; float2 uv : TEXCOORD; };

VSOut VSMain(uint vertex : SV_VertexID) {
    float2 corner = float2((vertex << 1) & 2, vertex & 2);
    VSOut result;
    result.position = float4(corner * float2(2, -2) + float2(-1, 1), 0, 1);
    result.uv = corner * pan.z + pan.xy;
    return result;
}

float4 PSMain(VSOut input) : SV_Target {
    return g_image.Sample(g_sampler, input.uv);
}
)";

struct FrameConstants {
    float pan[4];
};

std::uint64_t ParseCount(const char* argument) {
    std::uint64_t count = 0;
    const char* end = argument + std::strlen(argument);
    const auto result = std::from_chars(argument, end, count);
    if (result.ec != std::errc{} || result.ptr != end)
        throw std::runtime_error("Invalid unsigned integer: " + std::string(argument));
    return count;
}

} // namespace

int Run(int argc, char* argv[]) {
#include "requiem_run_args.inc"
    std::cout << "==========================================================" << std::endl;
    std::cout << " NRFusion - Resident Evil Requiem (D3D12 Testbed Game)    " << std::endl;
    std::cout << "==========================================================" << std::endl;

#include "requiem_run_setup.inc"
#include "requiem_run_loop.inc"
}

int main(int argc, char* argv[]) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* ep) -> LONG {
        void* addr = ep->ExceptionRecord->ExceptionAddress;
        HMODULE hMod = nullptr;
        wchar_t modPath[MAX_PATH] = L"<unknown>";
        uintptr_t offset = 0;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(addr), &hMod) && hMod) {
            GetModuleFileNameW(hMod, modPath, MAX_PATH);
            offset = reinterpret_cast<uintptr_t>(addr) - reinterpret_cast<uintptr_t>(hMod);
        }
        std::fprintf(stderr, "[FATAL] Unhandled exception 0x%08lX at address %p (%ls + 0x%llX)\n",
                     static_cast<unsigned long>(ep->ExceptionRecord->ExceptionCode),
                     addr, modPath, static_cast<unsigned long long>(offset));
        HANDLE hDump = CreateFileW(L"crash.dmp", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hDump != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mei{};
            mei.ThreadId = GetCurrentThreadId();
            mei.ExceptionPointers = ep;
            mei.ClientPointers = FALSE;
            MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hDump, MiniDumpNormal, &mei, nullptr, nullptr);
            CloseHandle(hDump);
            std::fprintf(stderr, "[FATAL] Wrote crash.dmp\n");
        }
        std::fflush(stderr);
        std::fflush(stdout);
        return EXCEPTION_CONTINUE_SEARCH;
    });
    try { return Run(argc, argv); }
    catch (const std::exception& error) {
        std::cerr << "[ERRO] " << error.what() << std::endl;
        return 1;
    }
}
