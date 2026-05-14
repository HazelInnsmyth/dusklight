# RTX Raytracing and DLSS 2 Implementation Guide

This guide walks through adding NVIDIA RTX raytracing and DLSS 2 support to Dusklight for PC (Windows) and Linux platforms.

## Table of Contents
1. [Overview & Architecture](#overview--architecture)
2. [Phase 1: DLSS 2 Integration (Recommended First)](#phase-1-dlss-2-integration)
3. [Phase 2: RTX Raytracing](#phase-2-rtx-raytracing)
4. [Requirements & Setup](#requirements--setup)
5. [Implementation Steps](#implementation-steps)

---

## Overview & Architecture

### Why DLSS First?
- **Easier to implement** - wraps around existing rendering
- **Performance benefit immediately** - upscales your current output
- **Less invasive** - doesn't require rewriting rendering pipeline
- **Works with current Aurora layer** - minimal changes needed

### Why RTX Raytracing Second?
- **More complex** - requires Aurora layer modifications
- **Architectural change** - affects rendering pipeline
- **Long-term improvement** - enables better visuals
- **Benefits from DLSS** - combine both for best results

### Current Architecture
```
Dusklight Application
    ↓
Aurora Graphics Abstraction Layer
    ├→ D3D12 Backend (Windows)
    ├→ Vulkan Backend (Linux)
    └→ Metal Backend (macOS - skip for now)
```

---

## Phase 1: DLSS 2 Integration

### What is DLSS 2?
DLSS uses AI to upscale lower-resolution rendered images to higher resolution with quality maintained.

**Benefits:**
- Increases performance (render at 1440p, upscale to 4K)
- Works with existing rendering pipeline
- No shader changes required

### Requirements for DLSS 2

Your rendering pipeline must provide:
1. **Color Buffer** - Your rendered frame (any resolution)
2. **Depth Buffer** - Linear depth information
3. **Motion Vectors** - Per-pixel movement from previous frame
4. **Exposure/Luminance** - Scene brightness info

### Step-by-Step Implementation

#### Step 1: Set Up Development Environment

**Install NVIDIA NGX SDK:**
```bash
# Download from: https://developer.nvidia.com/dlss
# Extract to a known location, e.g., C:\NVIDIA\NGX or ~/nvidia/ngx
```

**Add to CMakeLists.txt:**
```cmake
# At the top level of CMakeLists.txt, after other find_package calls:

# DLSS/NGX Setup
set(NGX_SDK_PATH "" CACHE PATH "Path to NVIDIA NGX SDK")

if(NGX_SDK_PATH)
    message(STATUS "dusklight: DLSS/NGX SDK found at ${NGX_SDK_PATH}")
    
    # For D3D12
    if(WIN32)
        find_library(NGX_LIB 
            NAMES nvsdk_ngx nvsdk_ngx_d3d12
            PATHS "${NGX_SDK_PATH}/lib" "${NGX_SDK_PATH}/lib/x64"
        )
        if(NGX_LIB)
            message(STATUS "dusklight: NGX library found: ${NGX_LIB}")
            list(APPEND GAME_LIBS ${NGX_LIB})
            list(APPEND GAME_INCLUDE_DIRS "${NGX_SDK_PATH}/include")
            list(APPEND GAME_COMPILE_DEFS DUSK_ENABLE_DLSS=1)
        endif()
    endif()
    
    # For Vulkan on Linux
    if(CMAKE_SYSTEM_NAME STREQUAL Linux)
        find_library(NGX_VULKAN_LIB
            NAMES nvsdk_ngx_vk
            PATHS "${NGX_SDK_PATH}/lib"
        )
        if(NGX_VULKAN_LIB)
            message(STATUS "dusklight: NGX Vulkan library found: ${NGX_VULKAN_LIB}")
            list(APPEND GAME_LIBS ${NGX_VULKAN_LIB})
            list(APPEND GAME_INCLUDE_DIRS "${NGX_SDK_PATH}/include")
            list(APPEND GAME_COMPILE_DEFS DUSK_ENABLE_DLSS=1)
        endif()
    endif()
else()
    message(STATUS "dusklight: NGX_SDK_PATH not set; DLSS will be disabled")
endif()
```

#### Step 2: Create DLSS Wrapper Classes

**File: `src/dusk/graphics/dlss.hpp`**
```cpp
#pragma once

#ifdef DUSK_ENABLE_DLSS

#include <cstdint>

namespace Dusk::Graphics {

enum class DLSSQuality {
    Performance,    // 50% render scale
    Balanced,       // 59% render scale
    Quality,        // 67% render scale
    UltraQuality,   // 77% render scale
};

struct DLSSContext {
    bool initialized = false;
    DLSSQuality quality = DLSSQuality::Balanced;
    
    // Resolution tracking
    uint32_t display_width = 0;
    uint32_t display_height = 0;
    uint32_t render_width = 0;
    uint32_t render_height = 0;
};

// Backend-agnostic DLSS interface
class DLSSFeature {
public:
    virtual ~DLSSFeature() = default;
    
    virtual bool Initialize(uint32_t display_w, uint32_t display_h, 
                           DLSSQuality quality) = 0;
    virtual void Shutdown() = 0;
    virtual bool IsSupported() const = 0;
    virtual void GetRenderResolution(uint32_t& out_w, uint32_t& out_h) const = 0;
};

} // namespace Dusk::Graphics

#endif // DUSK_ENABLE_DLSS
```

**File: `src/dusk/graphics/dlss_d3d12.hpp`** (Windows specific)
```cpp
#pragma once

#ifdef DUSK_ENABLE_DLSS

#include "dlss.hpp"
#include <d3d12.h>
#include <memory>

namespace Dusk::Graphics::D3D12 {

class DLSSD3D12 : public DLSSFeature {
public:
    DLSSD3D12();
    ~DLSSD3D12();
    
    bool Initialize(uint32_t display_w, uint32_t display_h, 
                   DLSSQuality quality) override;
    void Shutdown() override;
    bool IsSupported() const override;
    void GetRenderResolution(uint32_t& out_w, uint32_t& out_h) const override;
    
    // D3D12-specific evaluation
    bool Evaluate(ID3D12GraphicsCommandList* cmd_list,
                  ID3D12Resource* color_input,
                  ID3D12Resource* depth,
                  ID3D12Resource* motion_vectors,
                  ID3D12Resource* color_output);
    
private:
    DLSSContext context_;
    void* ngx_feature_ = nullptr;  // Opaque NGX feature handle
};

} // namespace Dusk::Graphics::D3D12

#endif // DUSK_ENABLE_DLSS
```

**File: `src/dusk/graphics/dlss_vulkan.hpp`** (Linux specific)
```cpp
#pragma once

#ifdef DUSK_ENABLE_DLSS

#include "dlss.hpp"
#include <vulkan/vulkan.h>
#include <memory>

namespace Dusk::Graphics::Vulkan {

class DLSSVulkan : public DLSSFeature {
public:
    DLSSVulkan();
    ~DLSSVulkan();
    
    bool Initialize(uint32_t display_w, uint32_t display_h, 
                   DLSSQuality quality) override;
    void Shutdown() override;
    bool IsSupported() const override;
    void GetRenderResolution(uint32_t& out_w, uint32_t& out_h) const override;
    
    // Vulkan-specific evaluation
    bool Evaluate(VkCommandBuffer cmd_buffer,
                  VkImage color_input,
                  VkImage depth,
                  VkImage motion_vectors,
                  VkImage color_output);
    
private:
    DLSSContext context_;
    void* ngx_feature_ = nullptr;  // Opaque NGX feature handle
};

} // namespace Dusk::Graphics::Vulkan

#endif // DUSK_ENABLE_DLSS
```

#### Step 3: Implement D3D12 DLSS Backend

**File: `src/dusk/graphics/dlss_d3d12.cpp`**
```cpp
#ifdef DUSK_ENABLE_DLSS

#include "dlss_d3d12.hpp"
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_d3d12.h>
#include <nvsdk_ngx_helpers.h>
#include <fmt/format.h>

namespace Dusk::Graphics::D3D12 {

DLSSD3D12::DLSSD3D12() = default;

DLSSD3D12::~DLSSD3D12() {
    Shutdown();
}

bool DLSSD3D12::Initialize(uint32_t display_w, uint32_t display_h, 
                          DLSSQuality quality) {
    if (context_.initialized) {
        return true;
    }
    
    context_.display_width = display_w;
    context_.display_height = display_h;
    context_.quality = quality;
    
    // Calculate render resolution based on quality preset
    float scale_factor = 1.0f;
    switch (quality) {
        case DLSSQuality::Performance:
            scale_factor = 0.50f;
            break;
        case DLSSQuality::Balanced:
            scale_factor = 0.59f;
            break;
        case DLSSQuality::Quality:
            scale_factor = 0.67f;
            break;
        case DLSSQuality::UltraQuality:
            scale_factor = 0.77f;
            break;
    }
    
    context_.render_width = static_cast<uint32_t>(display_w * scale_factor);
    context_.render_height = static_cast<uint32_t>(display_h * scale_factor);
    
    fmt::print("DLSS: Initialized for {}x{} display, rendering at {}x{}\n",
               display_w, display_h, context_.render_width, context_.render_height);
    
    context_.initialized = true;
    return true;
}

void DLSSD3D12::Shutdown() {
    if (!context_.initialized) {
        return;
    }
    
    // Destroy NGX feature if it was created
    if (ngx_feature_) {
        // NGX cleanup would go here
        ngx_feature_ = nullptr;
    }
    
    context_.initialized = false;
}

bool DLSSD3D12::IsSupported() const {
    // Check if system supports DLSS (requires RTX GPU)
    // This would query NGX capabilities
    return true;  // Placeholder
}

void DLSSD3D12::GetRenderResolution(uint32_t& out_w, uint32_t& out_h) const {
    out_w = context_.render_width;
    out_h = context_.render_height;
}

bool DLSSD3D12::Evaluate(ID3D12GraphicsCommandList* cmd_list,
                        ID3D12Resource* color_input,
                        ID3D12Resource* depth,
                        ID3D12Resource* motion_vectors,
                        ID3D12Resource* color_output) {
    if (!context_.initialized) {
        return false;
    }
    
    // TODO: Implement actual DLSS evaluation using NGX SDK
    // This requires:
    // 1. Create NGX feature if not already created
    // 2. Fill in NVSDK_NGX_D3D12_DLSS_Eval_Params with input buffers
    // 3. Call NGX_D3D12_EvaluateFeature_DLSS()
    
    return true;
}

} // namespace Dusk::Graphics::D3D12

#endif // DUSK_ENABLE_DLSS
```

#### Step 4: Implement Vulkan DLSS Backend

**File: `src/dusk/graphics/dlss_vulkan.cpp`**
```cpp
#ifdef DUSK_ENABLE_DLSS

#include "dlss_vulkan.hpp"
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_vk.h>
#include <nvsdk_ngx_helpers.h>
#include <fmt/format.h>

namespace Dusk::Graphics::Vulkan {

DLSSVulkan::DLSSVulkan() = default;

DLSSVulkan::~DLSSVulkan() {
    Shutdown();
}

bool DLSSVulkan::Initialize(uint32_t display_w, uint32_t display_h, 
                           DLSSQuality quality) {
    if (context_.initialized) {
        return true;
    }
    
    context_.display_width = display_w;
    context_.display_height = display_h;
    context_.quality = quality;
    
    float scale_factor = 1.0f;
    switch (quality) {
        case DLSSQuality::Performance:
            scale_factor = 0.50f;
            break;
        case DLSSQuality::Balanced:
            scale_factor = 0.59f;
            break;
        case DLSSQuality::Quality:
            scale_factor = 0.67f;
            break;
        case DLSSQuality::UltraQuality:
            scale_factor = 0.77f;
            break;
    }
    
    context_.render_width = static_cast<uint32_t>(display_w * scale_factor);
    context_.render_height = static_cast<uint32_t>(display_h * scale_factor);
    
    fmt::print("DLSS (Vulkan): Initialized for {}x{} display, rendering at {}x{}\n",
               display_w, display_h, context_.render_width, context_.render_height);
    
    context_.initialized = true;
    return true;
}

void DLSSVulkan::Shutdown() {
    if (!context_.initialized) {
        return;
    }
    
    if (ngx_feature_) {
        ngx_feature_ = nullptr;
    }
    
    context_.initialized = false;
}

bool DLSSVulkan::IsSupported() const {
    return true;  // Placeholder
}

void DLSSVulkan::GetRenderResolution(uint32_t& out_w, uint32_t& out_h) const {
    out_w = context_.render_width;
    out_h = context_.render_height;
}

bool DLSSVulkan::Evaluate(VkCommandBuffer cmd_buffer,
                         VkImage color_input,
                         VkImage depth,
                         VkImage motion_vectors,
                         VkImage color_output) {
    if (!context_.initialized) {
        return false;
    }
    
    // TODO: Implement actual DLSS evaluation using NGX SDK for Vulkan
    // Similar to D3D12 but using Vulkan resources
    
    return true;
}

} // namespace Dusk::Graphics::Vulkan

#endif // DUSK_ENABLE_DLSS
```

#### Step 5: Integration Point

Add to your main render loop handler:

**File: `src/dusk/graphics/RenderContext.hpp` (or similar)**
```cpp
// Add member:
#ifdef DUSK_ENABLE_DLSS
std::unique_ptr<DLSSFeature> dlss_feature_;
#endif

// Add method:
void EnableDLSS(DLSSQuality quality) {
    #ifdef DUSK_ENABLE_DLSS
    if (!dlss_feature_) {
        // Create appropriate backend
        #if defined(WIN32)
        dlss_feature_ = std::make_unique<D3D12::DLSSD3D12>();
        #elif defined(__linux__)
        dlss_feature_ = std::make_unique<Vulkan::DLSSVulkan>();
        #endif
    }
    
    dlss_feature_->Initialize(display_width_, display_height_, quality);
    #endif
}
```

---

## Phase 2: RTX Raytracing

### What is RTX Raytracing?
Simulates light rays bouncing in your scene for realistic reflections, shadows, and lighting.

### Requirements for Raytracing

1. **Acceleration Structures (AS)**
   - **BLAS** (Bottom-Level): One per object/mesh
   - **TLAS** (Top-Level): Scene hierarchy

2. **Ray Tracing Pipeline**
   - Ray generation shader
   - Hit shaders (closest hit, any hit, miss)

3. **Shader Binding Table (SBT)**
   - Links geometry to shaders

### Implementation Steps for RTX

#### Step 1: Create Raytracing Headers

**File: `src/dusk/graphics/raytracing.hpp`**
```cpp
#pragma once

#ifdef DUSK_ENABLE_RAYTRACING

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

namespace Dusk::Graphics {

struct RayTracingMesh {
    uint32_t vertex_buffer;
    uint32_t index_buffer;
    uint32_t index_count;
    glm::mat4 transform;
};

struct RayTracingScene {
    std::vector<RayTracingMesh> meshes;
    
    // Acceleration structures (backend-specific)
    void* blas_list = nullptr;  // List of BLAS handles
    void* tlas = nullptr;       // Top-level AS handle
};

class RayTracingEngine {
public:
    virtual ~RayTracingEngine() = default;
    
    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;
    virtual bool BuildAccelerationStructures(const RayTracingScene& scene) = 0;
    virtual bool TraceRays(uint32_t width, uint32_t height) = 0;
};

} // namespace Dusk::Graphics

#endif // DUSK_ENABLE_RAYTRACING
```

#### Step 2: D3D12 Raytracing Implementation

**File: `src/dusk/graphics/raytracing_d3d12.hpp`**
```cpp
#pragma once

#ifdef DUSK_ENABLE_RAYTRACING

#include "raytracing.hpp"
#include <d3d12.h>
#include <memory>

namespace Dusk::Graphics::D3D12 {

class RayTracingEngineD3D12 : public RayTracingEngine {
public:
    RayTracingEngineD3D12(ID3D12Device5* device, ID3D12CommandQueue* queue);
    ~RayTracingEngineD3D12();
    
    bool Initialize() override;
    void Shutdown() override;
    bool BuildAccelerationStructures(const RayTracingScene& scene) override;
    bool TraceRays(uint32_t width, uint32_t height) override;
    
private:
    ID3D12Device5* device_;
    ID3D12CommandQueue* queue_;
    
    // Raytracing pipeline
    ID3D12StateObject* rt_state_object_ = nullptr;
    ID3D12RootSignature* global_root_sig_ = nullptr;
};

} // namespace Dusk::Graphics::D3D12

#endif // DUSK_ENABLE_RAYTRACING
```

**File: `src/dusk/graphics/raytracing_d3d12.cpp`**
```cpp
#ifdef DUSK_ENABLE_RAYTRACING

#include "raytracing_d3d12.hpp"
#include <fmt/format.h>

namespace Dusk::Graphics::D3D12 {

RayTracingEngineD3D12::RayTracingEngineD3D12(ID3D12Device5* device, 
                                            ID3D12CommandQueue* queue)
    : device_(device), queue_(queue) {
}

RayTracingEngineD3D12::~RayTracingEngineD3D12() {
    Shutdown();
}

bool RayTracingEngineD3D12::Initialize() {
    // Check for raytracing support
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
    if (FAILED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)))) {
        fmt::print("ERROR: Device does not support DirectX Raytracing\n");
        return false;
    }
    
    if (options5.RaytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED) {
        fmt::print("ERROR: DirectX Raytracing is not supported on this GPU\n");
        return false;
    }
    
    fmt::print("RTX Raytracing: Supported (Tier {})\n", 
               static_cast<int>(options5.RaytracingTier));
    
    // TODO: Create root signatures, state objects, and shader binding tables
    
    return true;
}

void RayTracingEngineD3D12::Shutdown() {
    if (rt_state_object_) {
        rt_state_object_->Release();
        rt_state_object_ = nullptr;
    }
    if (global_root_sig_) {
        global_root_sig_->Release();
        global_root_sig_ = nullptr;
    }
}

bool RayTracingEngineD3D12::BuildAccelerationStructures(const RayTracingScene& scene) {
    // TODO: Build BLAS for each mesh
    // TODO: Build TLAS for scene
    fmt::print("RTX: Building acceleration structures for {} meshes\n", scene.meshes.size());
    return true;
}

bool RayTracingEngineD3D12::TraceRays(uint32_t width, uint32_t height) {
    // TODO: Dispatch rays
    return true;
}

} // namespace Dusk::Graphics::D3D12

#endif // DUSK_ENABLE_RAYTRACING
```

#### Step 3: Vulkan Raytracing Implementation

**File: `src/dusk/graphics/raytracing_vulkan.hpp`**
```cpp
#pragma once

#ifdef DUSK_ENABLE_RAYTRACING

#include "raytracing.hpp"
#include <vulkan/vulkan.h>
#include <memory>

namespace Dusk::Graphics::Vulkan {

class RayTracingEngineVulkan : public RayTracingEngine {
public:
    RayTracingEngineVulkan(VkDevice device, VkQueue queue, VkPhysicalDevice physical_device);
    ~RayTracingEngineVulkan();
    
    bool Initialize() override;
    void Shutdown() override;
    bool BuildAccelerationStructures(const RayTracingScene& scene) override;
    bool TraceRays(uint32_t width, uint32_t height) override;
    
private:
    VkDevice device_;
    VkQueue queue_;
    VkPhysicalDevice physical_device_;
    
    // Raytracing resources
    VkPipeline rt_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout rt_pipeline_layout_ = VK_NULL_HANDLE;
};

} // namespace Dusk::Graphics::Vulkan

#endif // DUSK_ENABLE_RAYTRACING
```

**File: `src/dusk/graphics/raytracing_vulkan.cpp`**
```cpp
#ifdef DUSK_ENABLE_RAYTRACING

#include "raytracing_vulkan.hpp"
#include <fmt/format.h>

namespace Dusk::Graphics::Vulkan {

RayTracingEngineVulkan::RayTracingEngineVulkan(VkDevice device, VkQueue queue, 
                                              VkPhysicalDevice physical_device)
    : device_(device), queue_(queue), physical_device_(physical_device) {
}

RayTracingEngineVulkan::~RayTracingEngineVulkan() {
    Shutdown();
}

bool RayTracingEngineVulkan::Initialize() {
    // Check for raytracing extensions
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rt_props{};
    rt_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
    
    VkPhysicalDeviceProperties2 props{};
    props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props.pNext = &rt_props;
    
    // vkGetPhysicalDeviceProperties2(physical_device_, &props);
    
    fmt::print("RTX Raytracing (Vulkan): Initialized\n");
    return true;
}

void RayTracingEngineVulkan::Shutdown() {
    if (rt_pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, rt_pipeline_, nullptr);
        rt_pipeline_ = VK_NULL_HANDLE;
    }
    if (rt_pipeline_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, rt_pipeline_layout_, nullptr);
        rt_pipeline_layout_ = VK_NULL_HANDLE;
    }
}

bool RayTracingEngineVulkan::BuildAccelerationStructures(const RayTracingScene& scene) {
    fmt::print("RTX: Building acceleration structures for {} meshes (Vulkan)\n", scene.meshes.size());
    return true;
}

bool RayTracingEngineVulkan::TraceRays(uint32_t width, uint32_t height) {
    return true;
}

} // namespace Dusk::Graphics::Vulkan

#endif // DUSK_ENABLE_RAYTRACING
```

---

## Requirements & Setup

### Hardware Requirements
- **RTX GPU**: NVIDIA RTX 2000 series or newer (RTX 2060 minimum)
- **Driver**: Latest NVIDIA driver (500+ series recommended)
- **OS**: Windows 10+ (1809 or later) or Linux

### Software Requirements

#### For DLSS:
```bash
# Download NGX SDK from: https://developer.nvidia.com/dlss
# Minimum version: DLSS SDK 3.0
```

#### For Raytracing:
```bash
# D3D12: Windows 10 SDK (1809+) with DXR support
# Vulkan: Vulkan SDK 1.2.162+ with VK_KHR_ray_tracing_pipeline
```

### Build Setup

#### Windows (Visual Studio 2019+)
```bash
cmake . -B build \
  -DNGX_SDK_PATH="C:\NVIDIA\NGX" \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build --config Release
```

#### Linux
```bash
cmake . -B build \
  -DNGX_SDK_PATH="$HOME/nvidia/ngx" \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build --config Release
```

---

## Implementation Steps

### Step 1: Prepare Development Environment
- [ ] Install latest NVIDIA drivers
- [ ] Download NVIDIA NGX SDK
- [ ] Set up SDK paths in CMakeLists.txt

### Step 2: Integrate DLSS (Phase 1)
- [ ] Create DLSS wrapper classes
- [ ] Implement D3D12 DLSS backend
- [ ] Implement Vulkan DLSS backend
- [ ] Add motion vector generation to render pipeline
- [ ] Add depth buffer tracking
- [ ] Test DLSS upscaling at different quality levels

### Step 3: Verify DLSS Works
- [ ] Enable DLSS at startup
- [ ] Compare render resolutions
- [ ] Verify performance improvements
- [ ] Test on different GPUs

### Step 4: Integrate Raytracing (Phase 2)
- [ ] Create raytracing interface
- [ ] Implement D3D12 raytracing backend
- [ ] Implement Vulkan raytracing backend
- [ ] Build acceleration structures
- [ ] Create ray tracing pipelines
- [ ] Implement ray generation shaders

### Step 5: Combine Both Features
- [ ] Render raytraced output at lower resolution
- [ ] Apply DLSS to raytraced output
- [ ] Measure performance gains

---

## Recommended Next Steps

1. **Start with DLSS** - it's simpler and gives immediate performance wins
2. **Get comfortable with NGX SDK** - understand how to feed buffers to DLSS
3. **Implement motion vector generation** - required by DLSS
4. **Once DLSS works, move to raytracing** - more complex but more rewarding
5. **Combine both** - raytracing + DLSS upscaling for best results

---

## Resources

### DLSS
- [NVIDIA DLSS Developer Portal](https://developer.nvidia.com/dlss)
- [NGX SDK Documentation](https://developer.nvidia.com/rtx/ngx)
- [DLSS SDK Samples](https://github.com/NVIDIA/DLSS)

### Raytracing - DirectX 12
- [Microsoft DXR Documentation](https://learn.microsoft.com/en-us/windows/win32/direct3d12/directx-raytracing)
- [NVIDIA DXR Tutorials](https://developer.nvidia.com/rtx/raytracing/dxr/dxr-sdk)
- [Microsoft DirectX Samples](https://github.com/microsoft/DirectX-Graphics-Samples)

### Raytracing - Vulkan
- [Khronos Ray Tracing Overview](https://www.khronos.org/blog/ray-tracing-in-vulkan)
- [NVIDIA Vulkan Ray Tracing Tutorial](https://nvpro-samples.github.io/vk_raytracing_tutorial_KHR/)
- [NVIDIA vk_raytracing_tutorial_KHR](https://github.com/nvpro-samples/vk_raytracing_tutorial_KHR)

---

## Common Pitfalls

1. **Motion vectors not generated correctly** - DLSS quality depends on accurate motion data
2. **Depth buffer format issues** - DLSS needs linear or specific depth formats
3. **Acceleration structure build failures** - Geometry must be valid and within bounds
4. **Missing shader compilation flags** - Ray tracing shaders need specific compiler flags
5. **NGX library path issues** - Ensure LD_LIBRARY_PATH includes NGX lib directory (Linux)

---

## Questions?

If you have questions during implementation:
1. Check NVIDIA's official samples first
2. Review the docs for your graphics API
3. Use debuggers like NVIDIA Nsight for profiling
4. Test on actual RTX hardware

Good luck! 🚀
