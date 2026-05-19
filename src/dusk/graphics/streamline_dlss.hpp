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
