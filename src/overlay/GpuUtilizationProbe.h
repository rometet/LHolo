// Optional NVIDIA NVML telemetry sampled ONLY every five seconds from the
// existing foreground FPS report. This is device-wide utilization, not a
// per-process GPU timestamp and never changes the renderer or device state.
#pragma once
#include <Windows.h>
#include <optional>
#include <cstdint>
namespace lholo::overlay::detail {
struct GpuUtilizationSample {
    std::uint32_t enginePercent{};
    std::uint32_t memoryControllerPercent{};
    bool available{};
};
class GpuUtilizationProbe {
public:
    GpuUtilizationProbe() = default;
    GpuUtilizationProbe(GpuUtilizationProbe const&) = delete;
    GpuUtilizationProbe& operator=(GpuUtilizationProbe const&) = delete;
    ~GpuUtilizationProbe() {
        if (mInitialized && mShutdown) mShutdown();
        if (mLibrary) FreeLibrary(mLibrary);
    }
    [[nodiscard]] GpuUtilizationSample sample() noexcept {
        if (!mAttempted) initialize();
        if (!mDevice || !mUtilization) return {};
        NativeUtilization values{};
        if (mUtilization(mDevice,&values) != 0) return {};
        if (values.gpu > 100 || values.memory > 100) return {};
        return {values.gpu, values.memory, true};
    }
private:
    struct NativeUtilization {
        unsigned int gpu{};
        unsigned int memory{};
    };
    using InitializeFn = int(__cdecl*)();
    using ShutdownFn = int(__cdecl*)();
    using GetDeviceFn = int(__cdecl*)(unsigned int, void**);
    using GetUtilizationFn = int(__cdecl*)(void*, NativeUtilization*);
    HMODULE mLibrary{};
    void* mDevice{};
    ShutdownFn mShutdown{};
    GetUtilizationFn mUtilization{};
    bool mAttempted{};
    bool mInitialized{};
    void initialize() noexcept {
        mAttempted=true;
        // Do not fall back to an executable, a network API, or any third-party
        // DLL search directory. The NVML library must be on the OS system path.
        wchar_t path[MAX_PATH]{};
        auto const n=GetSystemDirectoryW(path,MAX_PATH);
        if (!n || n+10 >= MAX_PATH) return;
        const wchar_t tail[]=L"\\nvml.dll";
        for (unsigned int i=0;i<sizeof(tail)/sizeof(wchar_t);++i)
            path[n+i]=tail[i];
        mLibrary=LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!mLibrary) return;
        auto const init=reinterpret_cast<InitializeFn>(
            GetProcAddress(mLibrary,"nvmlInit_v2"));
        auto const getDevice=reinterpret_cast<GetDeviceFn>(
            GetProcAddress(mLibrary,"nvmlDeviceGetHandleByIndex_v2"));
        mShutdown=reinterpret_cast<ShutdownFn>(
            GetProcAddress(mLibrary,"nvmlShutdown"));
        mUtilization=reinterpret_cast<GetUtilizationFn>(
            GetProcAddress(mLibrary,"nvmlDeviceGetUtilizationRates"));
        if (!init || !getDevice || !mShutdown || !mUtilization) return;
        if (init()!=0) return;
        mInitialized=true;
        if (getDevice(0,&mDevice)!=0) mDevice=nullptr;
    }
};
inline GpuUtilizationProbe& gpuUtilizationProbe() {
    static GpuUtilizationProbe probe;
    return probe;
}
} // namespace lholo::overlay::detail
