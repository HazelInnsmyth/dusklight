# NVIDIA Streamline Integration Guide for Dusklight

## 🎯 Overview: Streamline is the New Standard

**NVIDIA Streamline** (formerly NGX SDK) is the modern, unified framework for integrating NVIDIA technologies into games and applications. It provides:

- **DLSS 2 & 3** - Super resolution with optional frame generation
- **DLSS-D** - Denoising for ray tracing
- **Reflex** - Low-latency optimizations
- **NIS** - NVIDIA Image Scaling (open standard alternative)
- **DeepDVC** - Deep video compression
- **PCL** - Performance Collection Layer

**Key Benefits Over Old NGX:**
- ✅ Single unified API for all NVIDIA features
- ✅ Plugin-based architecture (easier to maintain)
- ✅ Open-sourced (source available on GitHub)
- ✅ Better debugging and profiling tools
- ✅ Cross-platform (DirectX 11/12, Vulkan)
- ✅ Easier integration than legacy NGX

---

## 📋 Prerequisites

### Hardware
- NVIDIA RTX GPU (RTX 2060 or newer for DLSS)
- Windows 10 20H1+ or Linux with latest NVIDIA drivers (512.15+)

### Software
- NVIDIA Streamline SDK (latest version from GitHub)
- DirectX 11/12 SDK (Windows)
- Vulkan SDK 1.2.162+ (Linux)

---

## 🚀 Phase 1: Setting Up Streamline Integration

### Step 1: Clone and Build Streamline

```bash
# Clone Streamline repo
git clone https://github.com/NVIDIA-RTX/Streamline.git
cd Streamline

# Windows
./setup.bat
./build.bat -production

# Linux (similar process with shell scripts)
./setup.sh
./build.sh -production

# Built artifacts in: _artifacts/sl.*/<Config>/
```

### Step 2: Download Pre-built Binaries

From [Streamline Releases](https://github.com/NVIDIA-RTX/Streamline/releases):
```bash
# Download the ZIP file containing:
# - sl.interposer.dll (the main interposer)
# - sl.dlss.dll (DLSS feature plugin)
# - sl.common.dll (common utilities)
# - Include headers (sl.h, sl_dlss.h, etc.)
```

### Step 3: Add to Your CMakeLists.txt

```cmake
# At top-level CMakeLists.txt after other dependencies

# Streamline SDK Setup
set(STREAMLINE_SDK_PATH "" CACHE PATH "Path to NVIDIA Streamline SDK")

if(STREAMLINE_SDK_PATH)
    message(STATUS "dusklight: Streamline SDK found at ${STREAMLINE_SDK_PATH}")
    
    # Add include directories
    list(APPEND GAME_INCLUDE_DIRS "${STREAMLINE_SDK_PATH}/include")
    
    # Add to compile definitions
    list(APPEND GAME_COMPILE_DEFS DUSK_ENABLE_STREAMLINE=1)
    
    # For Windows, link the interposer
    if(WIN32)
        find_library(STREAMLINE_LIB
            NAMES sl.interposer
            PATHS "${STREAMLINE_SDK_PATH}/lib/x64"
        )
        if(STREAMLINE_LIB)
            message(STATUS "dusklight: Streamline library found: ${STREAMLINE_LIB}")
            list(APPEND GAME_LIBS ${STREAMLINE_LIB})
        endif()
    elseif(CMAKE_SYSTEM_NAME STREQUAL Linux)
        find_library(STREAMLINE_LIB
            NAMES sl.interposer
            PATHS "${STREAMLINE_SDK_PATH}/lib"
        )
        if(STREAMLINE_LIB)
            message(STATUS "dusklight: Streamline library found: ${STREAMLINE_LIB}")
            list(APPEND GAME_LIBS ${STREAMLINE_LIB})
        endif()
    endif()
else()
    message(STATUS "dusklight: STREAMLINE_SDK_PATH not set; Streamline features will be disabled")
endif()
```

### Build Command

```bash
# Windows
cmake . -B build -DSTREAMLINE_SDK_PATH="C:\path\to\Streamline" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# Linux
cmake . -B build -DSTREAMLINE_SDK_PATH="/path/to/Streamline" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

---

## 🔧 Phase 2: Implementing Streamline DLSS

### Step 1: Create Streamline Wrapper Class

**File: `src/dusk/graphics/streamline_dlss.hpp`**

```cpp
#pragma once

#ifdef DUSK_ENABLE_STREAMLINE

#include <cstdint>
#include <memory>
#include <sl.h>
#include <sl_dlss.h>

namespace Dusk::Graphics {

enum class StreamlineDLSSMode {
    Off,           // DLSS disabled
    Performance,   // 50% render scale
    Balanced,      // 59% render scale
    Quality,       // 67% render scale
    UltraQuality,  // 77% render scale
};

struct StreamlineDLSSConfig {
    StreamlineDLSSMode mode = StreamlineDLSSMode::Balanced;
    uint32_t display_width = 1920;
    uint32_t display_height = 1080;
    float sharpness = 0.0f;  // -1.0 (blurry) to +1.0 (sharp)
    bool hdr = false;
    bool auto_exposure = true;
};

class StreamlineDLSS {
public:
    StreamlineDLSS();
    ~StreamlineDLSS();
    
    // Initialize Streamline (call once at startup, BEFORE graphics API initialization)
    static bool InitializeStreamline();
    static void ShutdownStreamline();
    
    // Check if DLSS is supported
    bool IsSupported() const;
    
    // Initialize DLSS for a specific viewport
    bool Initialize(const StreamlineDLSSConfig& config);
    void Shutdown();
    
    // Update DLSS settings
    bool UpdateSettings(const StreamlineDLSSConfig& config);
    
    // Get recommended render resolution
    void GetRenderResolution(uint32_t& out_w, uint32_t& out_h) const;
    uint32_t GetRenderWidth() const { return render_width_; }
    uint32_t GetRenderHeight() const { return render_height_; }
    
    // Get optimal DLSS settings for a given resolution
    static bool GetOptimalSettings(uint32_t display_w, uint32_t display_h,
                                   StreamlineDLSSMode mode,
                                   uint32_t& out_render_w, uint32_t& out_render_h,
                                   float& out_sharpness);
    
private:
    bool initialized_ = false;
    StreamlineDLSSConfig current_config_{};
    uint32_t render_width_ = 0;
    uint32_t render_height_ = 0;
    sl::ViewportHandle viewport_handle_{0};
    
    // Convert our enum to Streamline enum
    sl::DLSSMode ToStreamlineMode(StreamlineDLSSMode mode) const;
    sl::DLSSPreset ToStreamlinePreset(StreamlineDLSSMode mode) const;
};

} // namespace Dusk::Graphics

#endif // DUSK_ENABLE_STREAMLINE
```

### Step 2: Implement Streamline Initialization

**File: `src/dusk/graphics/streamline_dlss.cpp`**

```cpp
#ifdef DUSK_ENABLE_STREAMLINE

#include "streamline_dlss.hpp"
#include <sl_consts.h>
#include <fmt/format.h>

namespace Dusk::Graphics {

// Static initialization flag
static bool g_streamline_initialized = false;

bool StreamlineDLSS::InitializeStreamline() {
    if (g_streamline_initialized) {
        return true;
    }
    
    // Prepare Streamline preferences
    sl::Preferences pref{};
    pref.showConsole = false;  // Set to true for debugging
    pref.logLevel = sl::LogLevel::eDefault;
    pref.applicationId = 0;  // TODO: Get this from NVIDIA
    pref.engineType = sl::EngineType::eCustom;
    
    // Specify features to load
    sl::Feature features[] = {sl::kFeatureDLSS};
    pref.featuresToLoad = features;
    pref.numFeaturesToLoad = 1;
    
    // Enable OTA updates for future-proofing
    pref.flags = sl::PreferenceFlags::eDisableCLStateTracking 
               | sl::PreferenceFlags::eAllowOTA 
               | sl::PreferenceFlags::eLoadDownloadedPlugins;
    
    // Initialize Streamline
    if (SL_FAILED(slInit(pref))) {
        fmt::print("ERROR: Failed to initialize Streamline\n");
        return false;
    }
    
    fmt::print("SUCCESS: Streamline initialized\n");
    g_streamline_initialized = true;
    return true;
}

void StreamlineDLSS::ShutdownStreamline() {
    if (!g_streamline_initialized) {
        return;
    }
    
    if (SL_FAILED(slShutdown())) {
        fmt::print("WARNING: Failed to shutdown Streamline gracefully\n");
    }
    
    g_streamline_initialized = false;
}

StreamlineDLSS::StreamlineDLSS() = default;

StreamlineDLSS::~StreamlineDLSS() {
    Shutdown();
}

bool StreamlineDLSS::IsSupported() const {
    // Check if DLSS is supported on this system
    sl::AdapterInfo adapter_info{};  // Null adapter for general compatibility check
    
    if (SL_FAILED(slIsFeatureSupported(sl::kFeatureDLSS, adapter_info))) {
        fmt::print("DLSS is not supported on this system\n");
        return false;
    }
    
    return true;
}

bool StreamlineDLSS::Initialize(const StreamlineDLSSConfig& config) {
    if (initialized_) {
        return true;
    }
    
    if (!IsSupported()) {
        return false;
    }
    
    current_config_ = config;
    
    // Get optimal settings for this config
    if (!GetOptimalSettings(config.display_width, config.display_height,
                            config.mode, render_width_, render_height_,
                            current_config_.sharpness)) {
        fmt::print("ERROR: Failed to get DLSS optimal settings\n");
        return false;
    }
    
    // Create a unique viewport handle
    viewport_handle_ = {1};  // In real implementation, generate unique IDs
    
    fmt::print("Streamline DLSS: Initialized for {}x{} display, rendering at {}x{}\n",
               config.display_width, config.display_height,
               render_width_, render_height_);
    
    initialized_ = true;
    return true;
}

void StreamlineDLSS::Shutdown() {
    if (!initialized_) {
        return;
    }
    
    // Free resources
    if (SL_FAILED(slFreeResources(sl::kFeatureDLSS, viewport_handle_))) {
        fmt::print("WARNING: Failed to free DLSS resources\n");
    }
    
    initialized_ = false;
}

bool StreamlineDLSS::UpdateSettings(const StreamlineDLSSConfig& config) {
    if (!initialized_) {
        return false;
    }
    
    // Set DLSS options
    sl::DLSSOptions dlss_options{};
    dlss_options.mode = ToStreamlineMode(config.mode);
    dlss_options.outputWidth = config.display_width;
    dlss_options.outputHeight = config.display_height;
    dlss_options.sharpness = config.sharpness;
    dlss_options.colorBuffersHDR = config.hdr ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    dlss_options.useAutoExposure = config.auto_exposure ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    dlss_options.qualityPreset = ToStreamlinePreset(config.mode);
    
    if (SL_FAILED(slDLSSSetOptions(viewport_handle_, dlss_options))) {
        fmt::print("ERROR: Failed to set DLSS options\n");
        return false;
    }
    
    current_config_ = config;
    return true;
}

void StreamlineDLSS::GetRenderResolution(uint32_t& out_w, uint32_t& out_h) const {
    out_w = render_width_;
    out_h = render_height_;
}

bool StreamlineDLSS::GetOptimalSettings(uint32_t display_w, uint32_t display_h,
                                        StreamlineDLSSMode mode,
                                        uint32_t& out_render_w, uint32_t& out_render_h,
                                        float& out_sharpness) {
    // Prepare settings request
    sl::DLSSOptions dlss_options{};
    dlss_options.mode = ToStreamlineMode(mode);
    dlss_options.outputWidth = display_w;
    dlss_options.outputHeight = display_h;
    
    // Get optimal settings
    sl::DLSSOptimalSettings optimal_settings{};
    if (SL_FAILED(slDLSSGetOptimalSettings(dlss_options, optimal_settings))) {
        fmt::print("ERROR: Failed to get DLSS optimal settings\n");
        return false;
    }
    
    out_render_w = optimal_settings.renderWidth;
    out_render_h = optimal_settings.renderHeight;
    out_sharpness = optimal_settings.sharpness;
    
    fmt::print("Streamline DLSS: Optimal settings for {}x{} -> {}x{}\n",
               display_w, display_h, out_render_w, out_render_h);
    
    return true;
}

sl::DLSSMode StreamlineDLSS::ToStreamlineMode(StreamlineDLSSMode mode) const {
    switch (mode) {
        case StreamlineDLSSMode::Off:
            return sl::eDLSSModeOff;
        case StreamlineDLSSMode::Performance:
            return sl::eDLSSModePerformance;
        case StreamlineDLSSMode::Balanced:
            return sl::eDLSSModeBalanced;
        case StreamlineDLSSMode::Quality:
            return sl::eDLSSModeQuality;
        case StreamlineDLSSMode::UltraQuality:
            return sl::eDLSSModeUltraQuality;
        default:
            return sl::eDLSSModeBalanced;
    }
}

sl::DLSSPreset StreamlineDLSS::ToStreamlinePreset(StreamlineDLSSMode mode) const {
    switch (mode) {
        case StreamlineDLSSMode::Performance:
            return sl::DLSSPreset::ePresetL;
        case StreamlineDLSSMode::Balanced:
            return sl::DLSSPreset::ePresetM;
        case StreamlineDLSSMode::Quality:
            return sl::DLSSPreset::ePresetK;
        case StreamlineDLSSMode::UltraQuality:
            return sl::DLSSPreset::ePresetK;
        default:
            return sl::DLSSPreset::ePresetM;
    }
}

} // namespace Dusk::Graphics

#endif // DUSK_ENABLE_STREAMLINE
```

---

## 🔌 Phase 3: Integrating into Your Render Pipeline

### Step 1: Initialize at Engine Startup

**In your main initialization code (BEFORE graphics API setup):**

```cpp
#include <dusk/graphics/streamline_dlss.hpp>

int main() {
    // IMPORTANT: Initialize Streamline VERY early, before any graphics APIs
    if (!Dusk::Graphics::StreamlineDLSS::InitializeStreamline()) {
        // Handle error - Streamline not available
        fmt::print("WARNING: Streamline not available, proceeding without DLSS\n");
    }
    
    // ... Now create your graphics device (D3D12, Vulkan, etc.)
    
    return 0;
}
```

### Step 2: Create Streamline Device Association

**After your graphics device is created:**

```cpp
// For D3D12
if (SL_FAILED(slSetD3DDevice(myD3D12Device))) {
    fmt::print("ERROR: Failed to set D3D12 device for Streamline\n");
}

// For Vulkan
sl::VulkanInfo vk_info{};
vk_info.device = myVulkanDevice;
vk_info.instance = myVulkanInstance;
vk_info.physicalDevice = myVulkanPhysicalDevice;
if (!slSetVulkanInfo(vk_info)) {
    fmt::print("ERROR: Failed to set Vulkan info for Streamline\n");
}
```

### Step 3: Set Up DLSS in Your Renderer

```cpp
class MyRenderer {
private:
    std::unique_ptr<Dusk::Graphics::StreamlineDLSS> dlss_;
    
public:
    bool Initialize() {
        // Create DLSS instance
        dlss_ = std::make_unique<Dusk::Graphics::StreamlineDLSS>();
        
        // Configure DLSS
        Dusk::Graphics::StreamlineDLSSConfig config;
        config.mode = Dusk::Graphics::StreamlineDLSSMode::Balanced;
        config.display_width = 1920;
        config.display_height = 1080;
        config.hdr = true;
        config.auto_exposure = false;
        
        // Initialize
        if (!dlss_->Initialize(config)) {
            fmt::print("WARNING: DLSS initialization failed\n");
            return false;
        }
        
        // Get render resolution
        uint32_t render_w, render_h;
        dlss_->GetRenderResolution(render_w, render_h);
        
        // Setup your viewport to render at render_w x render_h
        SetupViewport(render_w, render_h);
        
        return true;
    }
    
    void Render() {
        // Render your scene at reduced resolution (render_w x render_h)
        RenderScene();
        
        // Tag your resources for Streamline
        TagResourcesForStreamline();
        
        // Let Streamline upscale to display resolution
        EvaluateStreamlineDLSS();
    }
    
private:
    void TagResourcesForStreamline() {
        // Get current frame token
        sl::FrameToken frame_token{};
        if (SL_FAILED(slGetNewFrameToken(&frame_token))) {
            return;
        }
        
        // Tag required resources
        sl::Resource color_in{sl::ResourceType::eTex2d, myColorBuffer};
        sl::Resource color_out{sl::ResourceType::eTex2d, myUpscaledBuffer};
        sl::Resource depth{sl::ResourceType::eTex2d, myDepthBuffer};
        sl::Resource motion_vecs{sl::ResourceType::eTex2d, myMotionVectors};
        
        sl::ResourceTag tags[] = {
            sl::ResourceTag{&color_in, sl::kBufferTypeScalingInputColor, 
                           sl::ResourceLifecycle::eOnlyValidNow, nullptr},
            sl::ResourceTag{&color_out, sl::kBufferTypeScalingOutputColor,
                           sl::ResourceLifecycle::eOnlyValidNow, nullptr},
            sl::ResourceTag{&depth, sl::kBufferTypeDepth,
                           sl::ResourceLifecycle::eValidUntilPresent, nullptr},
            sl::ResourceTag{&motion_vecs, sl::kBufferTypeMotionVectors,
                           sl::ResourceLifecycle::eOnlyValidNow, nullptr},
        };
        
        sl::ViewportHandle viewport{1};
        if (SL_FAILED(slSetTagForFrame(frame_token, viewport, tags, 
                                       _countof(tags), myCommandList))) {
            fmt::print("ERROR: Failed to tag resources for Streamline\n");
        }
    }
    
    void EvaluateStreamlineDLSS() {
        // Set common constants (camera matrices, motion vector scale, etc.)
        sl::Constants consts{};
        consts.mvecScale = {1.0f, 1.0f};  // Adjust based on your motion vector format
        
        sl::FrameToken frame_token{};
        sl::ViewportHandle viewport{1};
        
        if (SL_FAILED(slSetConstants(consts, frame_token, viewport))) {
            fmt::print("ERROR: Failed to set Streamline constants\n");
            return;
        }
        
        // Evaluate DLSS
        const sl::BaseStructure* inputs[] = {&viewport};
        if (SL_FAILED(slEvaluateFeature(sl::kFeatureDLSS, frame_token, 
                                        inputs, 1, myCommandList))) {
            fmt::print("ERROR: Failed to evaluate DLSS\n");
            return;
        }
        
        // IMPORTANT: Restore command list state after Streamline evaluation
        RestoreCommandListState(myCommandList);
    }
};
```

---

## 📊 Key Implementation Steps Summary

1. ✅ **Clone and build Streamline** from GitHub
2. ✅ **Add CMakeLists.txt** integration
3. ✅ **Create wrapper class** `StreamlineDLSS`
4. ✅ **Initialize Streamline** before graphics API (CRITICAL!)
5. ✅ **Set graphics device** (D3D12 or Vulkan)
6. ✅ **Render at lower resolution** (get from DLSS)
7. ✅ **Generate motion vectors** (needed for DLSS quality)
8. ✅ **Tag all required buffers** with Streamline API
9. ✅ **Call slEvaluateFeature()** to upscale
10. ✅ **Restore command list state** after evaluation

---

## 🎮 UI Considerations

**Show to users:**
- [ ] DLSS toggle (On/Off)
- [ ] Quality mode selector (Performance / Balanced / Quality / Ultra Quality)
- [ ] Sharpness slider (-1.0 to +1.0)
- [ ] Current render resolution vs display resolution

**Example UI code:**
```cpp
void ShowDLSSSettings() {
    if (ImGui::Begin("Graphics Settings")) {
        bool dlss_enabled = current_dlss_mode != StreamlineDLSSMode::Off;
        if (ImGui::Checkbox("Enable DLSS", &dlss_enabled)) {
            current_dlss_mode = dlss_enabled ? StreamlineDLSSMode::Balanced 
                                             : StreamlineDLSSMode::Off;
            dlss_->UpdateSettings({current_dlss_mode, ...});
        }
        
        if (dlss_enabled) {
            int mode = static_cast<int>(current_dlss_mode);
            if (ImGui::Combo("DLSS Mode", &mode, 
                            "Performance\0Balanced\0Quality\0Ultra Quality\0")) {
                current_dlss_mode = static_cast<StreamlineDLSSMode>(mode);
                dlss_->UpdateSettings({current_dlss_mode, ...});
            }
            
            if (ImGui::SliderFloat("Sharpness", &dlss_sharpness, -1.0f, 1.0f)) {
                dlss_->UpdateSettings({current_dlss_mode, ..., dlss_sharpness});
            }
        }
        
        uint32_t render_w, render_h;
        dlss_->GetRenderResolution(render_w, render_h);
        ImGui::Text("Render Resolution: %ux%u", render_w, render_h);
        ImGui::Text("Display Resolution: %ux%u", display_width, display_height);
        
        ImGui::End();
    }
}
```

---

## 🐛 Troubleshooting Common Issues

| Problem | Cause | Solution |
|---------|-------|----------|
| Streamline fails to initialize | Initialized after graphics API | Move `InitializeStreamline()` to very start of `main()` |
| DLSS appears blurry | Motion vectors incorrect | Verify motion vector format and scaling in `sl::Constants::mvecScale` |
| Crash when evaluating DLSS | Resources not properly tagged | Ensure depth, color, and motion vectors are tagged before evaluation |
| DLSS not upscaling | Device not set | Call `slSetD3DDevice()` or `slSetVulkanInfo()` after device creation |
| Plugin load failures | Missing plugin DLLs | Ensure `sl.dlss.dll` is in your bin directory or properly referenced |

---

## 📚 Official Documentation & Resources

- [Streamline GitHub](https://github.com/NVIDIA-RTX/Streamline)
- [Streamline Programming Guide](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuide.md)
- [DLSS Programming Guide](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS.md)
- [NVIDIA Developer Portal](https://developer.nvidia.com/rtx/streamline)
- [Sample Application](https://github.com/NVIDIA-RTX/Streamline_Sample)

---

## ✅ Next Steps

1. Clone Streamline repo and get familiar with its structure
2. Download pre-built binaries or build from source
3. Implement `StreamlineDLSS` wrapper class
4. Integrate into Dusklight's render pipeline
5. Test DLSS with different quality modes
6. Add UI for user control
7. Benchmark performance improvements

---

## 🎯 Future Enhancements

Once DLSS is working:
- Add **DLSS Frame Generation** (DLSS-G) for even more performance
- Integrate **DLSS-D** for denoising ray-traced outputs
- Add **Reflex** support for reduced input latency
- Explore **NIS** (NVIDIA Image Scaling) as alternative
- Profile with **NVIDIA Nsight Systems** for optimization

Good luck with the integration! 🚀
