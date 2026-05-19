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
    pref.applicationId = 0;  // TODO: Get this from NVIDIA when you register your app
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
