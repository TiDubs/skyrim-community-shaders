#pragma once

#include "../../Buffer.h"
#include "../../State.h"

#include <d3d11_4.h>
#include <d3d12.h>

#include <array>
#include <limits>

#define NV_WINDOWS

#pragma warning(push)
#pragma warning(disable: 4471)
#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss.h>
#include <sl_matrix_helpers.h>
#include <sl_version.h>
#pragma warning(pop)

class Streamline
{
public:
	static constexpr const wchar_t* PluginDir = L"Data\\Shaders\\Upscaling\\Streamline";

	Streamline() = default;

	inline std::string GetShortName() { return "Streamline"; }

	bool enabledAtBoot = false;
	bool initialized = false;
	bool triedInitialization = false;

	bool featureDLSS = false;

        struct EyeState
        {
                sl::ViewportHandle viewport{};
                bool constantsInitialized = false;
                bool resetThisFrame = false;
                uint32_t lastRenderWidth = 0;
                uint32_t lastRenderHeight = 0;
                uint32_t lastRenderOffsetX = 0;
                uint32_t lastOutputWidth = 0;
                uint32_t lastOutputHeight = 0;
                uint32_t lastOutputOffsetX = 0;
                uint32_t lastQualityMode = std::numeric_limits<uint32_t>::max();
                sl::DLSSPreset lastPreset = sl::DLSSPreset::eDefault;
        };

	std::array<EyeState, 2> eyes{};

	HMODULE interposer = NULL;

	// SL Interposer Functions
	PFun_slInit* slInit{};
	PFun_slShutdown* slShutdown{};
	PFun_slIsFeatureSupported* slIsFeatureSupported{};
	PFun_slIsFeatureLoaded* slIsFeatureLoaded{};
	PFun_slSetFeatureLoaded* slSetFeatureLoaded{};
	PFun_slEvaluateFeature* slEvaluateFeature{};
	PFun_slAllocateResources* slAllocateResources{};
	PFun_slFreeResources* slFreeResources{};
	PFun_slSetTag* slSetTag{};
	PFun_slGetFeatureRequirements* slGetFeatureRequirements{};
	PFun_slGetFeatureVersion* slGetFeatureVersion{};
	PFun_slUpgradeInterface* slUpgradeInterface{};
	PFun_slSetConstants* slSetConstants{};
	PFun_slGetNativeInterface* slGetNativeInterface{};
	PFun_slGetFeatureFunction* slGetFeatureFunction{};
	PFun_slGetNewFrameToken* slGetNewFrameToken{};
	PFun_slSetD3DDevice* slSetD3DDevice{};

	// DLSS specific functions
	PFun_slDLSSGetOptimalSettings* slDLSSGetOptimalSettings{};
	PFun_slDLSSGetState* slDLSSGetState{};
	PFun_slDLSSSetOptions* slDLSSSetOptions{};

	Util::FrameChecker frameChecker;
	sl::FrameToken* frameToken = nullptr;

	// Cached DLL version info for Streamline plugin directory
	static std::vector<std::pair<std::string, std::string>> dllVersions;

	void LoadInterposer();

	void CheckFeatures(IDXGIAdapter* a_adapter);

	void PostDevice();

	void CheckFrameConstants();

	void Upscale(ID3D11Resource* a_upscalingTexture, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_motionVectors, sl::DLSSPreset a_preset);

	float GetInputResolutionScale(uint32_t outputWidth, uint32_t outputHeight, uint32_t qualityPreset);

	void DestroyDLSSResources();

	private:
		bool EnsureViewportAllocated(uint32_t eyeIndex);
		void ResetEyeState(uint32_t eyeIndex);
};
