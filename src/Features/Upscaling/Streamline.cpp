#include "Streamline.h"

#include <cstring>
#include <type_traits>

#include <dxgi.h>
#include <dxgi1_3.h>

#include <cstring>

#include "../../Deferred.h"
#include "../../Hooks.h"
#include "../../State.h"
#include "../../Util.h"
#include "../Upscaling.h"
#include "DX12SwapChain.h"

namespace
{
static_assert(std::is_trivially_copyable_v<sl::ViewportHandle>);

[[nodiscard]] bool AreViewportHandlesEqual(const sl::ViewportHandle& lhs, const sl::ViewportHandle& rhs) noexcept
{
	const sl::ViewportHandle kEmpty{};

	const bool lhsEmpty = std::memcmp(&lhs, &kEmpty, sizeof(kEmpty)) == 0;
	const bool rhsEmpty = std::memcmp(&rhs, &kEmpty, sizeof(kEmpty)) == 0;

	if (lhsEmpty && rhsEmpty) {
		return true;
	}

	if (lhsEmpty != rhsEmpty) {
		return false;
	}

	return std::memcmp(&lhs, &rhs, sizeof(sl::ViewportHandle)) == 0;
}
}

void LoggingCallback(sl::LogType type, const char* msg)
{
	// Remove trailing newlines from the raw message
	std::string rawMsg(msg);
	while (!rawMsg.empty() && (rawMsg.back() == '\n' || rawMsg.back() == '\r'))
		rawMsg.pop_back();

	// Remove leading bracketed metadata
	const char* p = msg;
	while (*p == '[') {
		const char* close = strchr(p, ']');
		if (!close)
			break;
		p = close + 1;
		// Skip whitespace after each bracketed section
		while (*p == ' ' || *p == '\t') ++p;
	}
	// Now p points to the first non-bracketed section (file/line info or message)
	std::string cleanMsg(p);
	// Trim leading/trailing whitespace and newlines
	size_t start = cleanMsg.find_first_not_of(" \t\r\n");
	size_t end = cleanMsg.find_last_not_of(" \t\r\n");
	if (start != std::string::npos && end != std::string::npos)
		cleanMsg = cleanMsg.substr(start, end - start + 1);
	else
		cleanMsg.clear();

	// If the cleaned message is empty or only bracketed tokens, log the raw message
	bool onlyBrackets = true;
	for (char c : cleanMsg) {
		if (c != '[' && c != ']' && c != ' ' && c != '\t') {
			onlyBrackets = false;
			break;
		}
	}
	if (cleanMsg.empty() || onlyBrackets) {
		logger::info("[StreamlineSDK:RAW] {}", rawMsg);
		return;
	}

	// Use a clear prefix
	const char* prefix = "[StreamlineSDK]";
	switch (type) {
	case sl::LogType::eInfo:
		logger::info("{} {}", prefix, cleanMsg);
		break;
	case sl::LogType::eWarn:
		logger::warn("{} {}", prefix, cleanMsg);
		break;
	case sl::LogType::eError:
		logger::error("{} {}", prefix, cleanMsg);
		break;
	}
}

std::vector<std::pair<std::string, std::string>> Streamline::dllVersions = {};

void Streamline::LoadInterposer()
{
	triedInitialization = true;

	std::wstring interposerPath = std::wstring(Streamline::PluginDir) + L"\\sl.interposer.dll";
	interposer = LoadLibraryW(interposerPath.c_str());
	if (interposer == nullptr) {
		DWORD errorCode = GetLastError();
		logger::info("[Streamline] Failed to load interposer: Error Code {0:x}", errorCode);
		return;
	} else {
		logger::info("[Streamline] Interposer loaded at address: {0:p}", static_cast<void*>(interposer));
	}

	// Dynamically log all DLL versions in the Streamline plugin directory
	std::filesystem::path pluginDir = std::filesystem::path(Streamline::PluginDir);
	Streamline::dllVersions.clear();
	for (const auto& entry : std::filesystem::directory_iterator(pluginDir)) {
		if (entry.is_regular_file() && entry.path().extension() == L".dll") {
			const auto& path = entry.path();
			auto version = Util::GetDllVersion(path.c_str());
			auto name = path.filename().string();
			std::string versionStr = version ? Util::GetFormattedVersion(*version) : "Unknown";
			Streamline::dllVersions.emplace_back(name, versionStr);
			if (version)
				logger::info("[Streamline] {} version: {}", name, versionStr);
			else
				logger::info("[Streamline] {} version: Unknown", name);
		}
	}

	logger::info("[Streamline] Initializing Streamline");

	sl::Preferences pref;

	sl::Feature featuresToLoad[] = { sl::kFeatureDLSS };
	sl::Feature featuresToLoadVR[] = { sl::kFeatureDLSS };

	pref.featuresToLoad = REL::Module::IsVR() ? featuresToLoadVR : featuresToLoad;
	pref.numFeaturesToLoad = REL::Module::IsVR() ? _countof(featuresToLoadVR) : _countof(featuresToLoad);

	// Set log level from settings
	switch (globals::features::upscaling.settings.streamlineLogLevel) {
	case 2:
		pref.logLevel = sl::LogLevel::eVerbose;
		break;
	case 1:
		pref.logLevel = sl::LogLevel::eDefault;
		break;
	case 0:
	default:
		pref.logLevel = sl::LogLevel::eOff;
		break;
	}
	pref.logMessageCallback = LoggingCallback;
	pref.showConsole = false;

	pref.engine = sl::EngineType::eCustom;
	pref.engineVersion = "1.0.0";
	pref.projectId = "f8776929-c969-43bd-ac2b-294b4de58aac";

	pref.renderAPI = sl::RenderAPI::eD3D11;
	pref.flags = sl::PreferenceFlags::eUseManualHooking;

	// Hook up all of the functions exported by the SL Interposer Library
	slInit = (PFun_slInit*)GetProcAddress(interposer, "slInit");
	slShutdown = (PFun_slShutdown*)GetProcAddress(interposer, "slShutdown");
	slIsFeatureSupported = (PFun_slIsFeatureSupported*)GetProcAddress(interposer, "slIsFeatureSupported");
	slIsFeatureLoaded = (PFun_slIsFeatureLoaded*)GetProcAddress(interposer, "slIsFeatureLoaded");
	slSetFeatureLoaded = (PFun_slSetFeatureLoaded*)GetProcAddress(interposer, "slSetFeatureLoaded");
	slEvaluateFeature = (PFun_slEvaluateFeature*)GetProcAddress(interposer, "slEvaluateFeature");
	slAllocateResources = (PFun_slAllocateResources*)GetProcAddress(interposer, "slAllocateResources");
	slFreeResources = (PFun_slFreeResources*)GetProcAddress(interposer, "slFreeResources");
	slSetTag = (PFun_slSetTag*)GetProcAddress(interposer, "slSetTag");
	slGetFeatureRequirements = (PFun_slGetFeatureRequirements*)GetProcAddress(interposer, "slGetFeatureRequirements");
	slGetFeatureVersion = (PFun_slGetFeatureVersion*)GetProcAddress(interposer, "slGetFeatureVersion");
	slUpgradeInterface = (PFun_slUpgradeInterface*)GetProcAddress(interposer, "slUpgradeInterface");
	slSetConstants = (PFun_slSetConstants*)GetProcAddress(interposer, "slSetConstants");
	slGetNativeInterface = (PFun_slGetNativeInterface*)GetProcAddress(interposer, "slGetNativeInterface");
	slGetFeatureFunction = (PFun_slGetFeatureFunction*)GetProcAddress(interposer, "slGetFeatureFunction");
	slGetNewFrameToken = (PFun_slGetNewFrameToken*)GetProcAddress(interposer, "slGetNewFrameToken");
	slSetD3DDevice = (PFun_slSetD3DDevice*)GetProcAddress(interposer, "slSetD3DDevice");

	if (SL_FAILED(res, slInit(pref, sl::kSDKVersion))) {
		logger::critical("[Streamline] Failed to initialize Streamline");
	} else {
		initialized = true;
		logger::info("[Streamline] Successfully initialized Streamline");
	}
}

void Streamline::CheckFeatures(IDXGIAdapter* a_adapter)
{
	logger::info("[Streamline] Checking features");
	DXGI_ADAPTER_DESC adapterDesc;
	a_adapter->GetDesc(&adapterDesc);

	sl::AdapterInfo adapterInfo;
	adapterInfo.deviceLUID = (uint8_t*)&adapterDesc.AdapterLuid;
	adapterInfo.deviceLUIDSizeInBytes = sizeof(LUID);

	slIsFeatureLoaded(sl::kFeatureDLSS, featureDLSS);
	if (featureDLSS) {
		logger::info("[Streamline] DLSS feature is loaded");
		featureDLSS = slIsFeatureSupported(sl::kFeatureDLSS, adapterInfo) == sl::Result::eOk;
	} else {
		logger::info("[Streamline] DLSS feature is not loaded");
		sl::FeatureRequirements featureRequirements;
		sl::Result result = slGetFeatureRequirements(sl::kFeatureDLSS, featureRequirements);
		if (result != sl::Result::eOk) {
			logger::info("[Streamline] DLSS feature failed to load due to: {}", magic_enum::enum_name(result));
		}
	}

	logger::info("[Streamline] DLSS {} available", featureDLSS ? "is" : "is not");
}

void Streamline::PostDevice()
{
	// Hook up all of the feature functions using the sl function slGetFeatureFunction

	if (featureDLSS) {
		slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSGetOptimalSettings", (void*&)slDLSSGetOptimalSettings);
		slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSGetState", (void*&)slDLSSGetState);
		slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSSetOptions", (void*&)slDLSSSetOptions);
	}
}

/**
 * @brief Updates and sets camera and frame constants for the current Streamline frame.
 *
 * Populates and submits camera parameters, projection matrices, motion vector settings, and other per-frame constants to the Streamline SDK for the current frame. Uses cached framebuffer data and global state to ensure correct configuration for upscaling and frame generation features.
 */
void Streamline::CheckFrameConstants()
{
	if (frameChecker.IsNewFrame() && globals::features::upscaling.streamline.initialized) {
		slGetNewFrameToken(frameToken, &globals::state->frameCount);

		auto state = globals::state;
		const bool isVR = globals::game::isVR;
		const uint32_t eyeCount = isVR ? 2u : 1u;

		sl::Constants slConstants = {};

		if (isVR) {
			slConstants.cameraAspectRatio = (state->screenSize.x * 0.5f) / state->screenSize.y;
		} else {
			slConstants.cameraAspectRatio = state->screenSize.x / state->screenSize.y;
		}

		slConstants.cameraFOV = Util::GetVerticalFOVRad();
		slConstants.cameraNear = *globals::game::cameraNear;
		slConstants.cameraFar = *globals::game::cameraFar;

		auto viewMatrix = globals::game::frameBufferCached.GetCameraViewInverse().Transpose();
		auto cameraViewToClip = globals::game::frameBufferCached.GetCameraProjUnjittered().Transpose();

		slConstants.cameraMotionIncluded = sl::Boolean::eTrue;
		slConstants.cameraPinholeOffset = { 0.f, 0.f };
		slConstants.cameraRight = { viewMatrix._11, viewMatrix._12, viewMatrix._13 };
		slConstants.cameraUp = { viewMatrix._21, viewMatrix._22, viewMatrix._23 };
		slConstants.cameraFwd = { viewMatrix._31, viewMatrix._32, viewMatrix._33 };
		slConstants.cameraPos = *(sl::float3*)&globals::game::frameBufferCached.GetCameraPosAdjust();
		slConstants.cameraViewToClip = *(sl::float4x4*)&cameraViewToClip;
		slConstants.depthInverted = sl::Boolean::eFalse;

		recalculateCameraMatrices(slConstants);

		auto& upscaling = globals::features::upscaling;
		auto jitter = upscaling.jitter;
		slConstants.jitterOffset = { -jitter.x, -jitter.y };
		slConstants.reset = sl::Boolean::eFalse;

		slConstants.mvecScale = { (isVR ? 0.5f : 1.0f), 1 };
		slConstants.motionVectors3D = sl::Boolean::eFalse;
		slConstants.motionVectorsInvalidValue = FLT_MIN;
		slConstants.orthographicProjection = sl::Boolean::eFalse;
		slConstants.motionVectorsDilated = sl::Boolean::eFalse;
		slConstants.motionVectorsJittered = sl::Boolean::eFalse;

		for (uint32_t eyeIndex = 0; eyeIndex < eyeCount; ++eyeIndex) {
			auto& activeViewport = viewports[eyeIndex];
			if (!IsViewportAllocated(activeViewport)) {
				continue;
			}

			sl::Result setConstantsResult = slSetConstants(slConstants, *frameToken, activeViewport);
			if (setConstantsResult != sl::Result::eOk) {
				logger::error("[Streamline] Could not set constants for viewport {}", eyeIndex);
			}
		}
	}
}

void Streamline::Upscale(ID3D11Resource* a_upscalingTexture, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_motionVectors, sl::DLSSPreset a_preset)
{
	CheckFrameConstants();

	auto state = globals::state;

	auto renderer = globals::game::renderer;
	auto& depthTexture = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

	sl::DLSSOptions baseDlssOptions{};

	// Map quality mode to DLSS mode
	uint32_t qualityMode = globals::features::upscaling.settings.qualityMode;
	switch (qualityMode) {
	case 1:
		baseDlssOptions.mode = sl::DLSSMode::eMaxQuality;
		break;
	case 2:
		baseDlssOptions.mode = sl::DLSSMode::eBalanced;
		break;
	case 3:
		baseDlssOptions.mode = sl::DLSSMode::eMaxPerformance;
		break;
	case 4:
		baseDlssOptions.mode = sl::DLSSMode::eUltraPerformance;
		break;
	default:
		baseDlssOptions.mode = sl::DLSSMode::eDLAA;
		break;
	}

	baseDlssOptions.colorBuffersHDR = sl::Boolean::eTrue;
	baseDlssOptions.useAutoExposure = sl::Boolean::eTrue;

	baseDlssOptions.preExposure = 1.0f;
	baseDlssOptions.sharpness = 0.0f;

	baseDlssOptions.dlaaPreset = a_preset;
	baseDlssOptions.qualityPreset = a_preset;
	baseDlssOptions.balancedPreset = a_preset;
	baseDlssOptions.performancePreset = a_preset;
	baseDlssOptions.ultraPerformancePreset = a_preset;

	const bool isVR = globals::game::isVR;
	const uint32_t eyeCount = isVR ? 2u : 1u;

	auto screenSize = state->screenSize;
	auto renderSize = Util::ConvertToDynamic(screenSize);

	const uint32_t totalOutputWidth = static_cast<uint32_t>(screenSize.x);
	const uint32_t totalOutputHeight = static_cast<uint32_t>(screenSize.y);
	const uint32_t totalRenderWidth = static_cast<uint32_t>(renderSize.x);
	const uint32_t totalRenderHeight = static_cast<uint32_t>(renderSize.y);

	const uint32_t baseOutputWidth = eyeCount > 0 ? totalOutputWidth / eyeCount : totalOutputWidth;
	const uint32_t baseRenderWidth = eyeCount > 0 ? totalRenderWidth / eyeCount : totalRenderWidth;

	for (uint32_t eyeIndex = 0; eyeIndex < eyeCount; ++eyeIndex) {
		auto& activeViewport = viewports[eyeIndex];
		if (!IsViewportAllocated(activeViewport)) {
			if (eyeCount > 1) {
				logger::warn("[Streamline] Skipping DLSS for eye {} because the viewport handle is not allocated", eyeIndex);
			}
			continue;
		}

		const uint32_t renderOffsetX = isVR ? baseRenderWidth * eyeIndex : 0u;
		const uint32_t outputOffsetX = isVR ? baseOutputWidth * eyeIndex : 0u;

		const uint32_t currentRenderWidth =
			(isVR && eyeIndex == eyeCount - 1) ? (totalRenderWidth - renderOffsetX) : (isVR ? baseRenderWidth : totalRenderWidth);
		const uint32_t currentOutputWidth =
			(isVR && eyeIndex == eyeCount - 1) ? (totalOutputWidth - outputOffsetX) : (isVR ? baseOutputWidth : totalOutputWidth);

		sl::DLSSOptions dlssOptions = baseDlssOptions;
		dlssOptions.outputWidth = currentOutputWidth;
		dlssOptions.outputHeight = totalOutputHeight;

		sl::Result setOptionsResult = slDLSSSetOptions(activeViewport, dlssOptions);
		if (setOptionsResult != sl::Result::eOk) {
			logger::critical("[Streamline] Could not enable DLSS for viewport {}", eyeIndex);
			continue;
		}

		sl::Extent lowResExtent{ renderOffsetX, 0, currentRenderWidth, totalRenderHeight };
		sl::Extent fullExtent{ outputOffsetX, 0, currentOutputWidth, totalOutputHeight };

		sl::Resource colorIn = { sl::ResourceType::eTex2d, a_upscalingTexture, 0 };
		sl::Resource colorOut = { sl::ResourceType::eTex2d, a_upscalingTexture, 0 };
		sl::Resource depth = { sl::ResourceType::eTex2d, depthTexture.texture, 0 };
		sl::Resource mvec = { sl::ResourceType::eTex2d, a_motionVectors, 0 };

		sl::ResourceTag colorInTag = sl::ResourceTag{ &colorIn, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eOnlyValidNow, &lowResExtent };
		sl::ResourceTag colorOutTag = sl::ResourceTag{ &colorOut, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eOnlyValidNow, &fullExtent };
		sl::ResourceTag depthTag = sl::ResourceTag{ &depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &lowResExtent };
		sl::ResourceTag mvecTag = sl::ResourceTag{ &mvec, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &lowResExtent };

		sl::Resource reactiveMask = { sl::ResourceType::eTex2d, a_reactiveMask, 0 };
		sl::ResourceTag reactiveMaskTag = sl::ResourceTag{ &reactiveMask, sl::kBufferTypeBiasCurrentColorHint, sl::ResourceLifecycle::eValidUntilPresent, &lowResExtent };

		sl::Resource transparencyCompositionMask = { sl::ResourceType::eTex2d, a_transparencyCompositionMask, 0 };
		sl::ResourceTag transparencyCompositionMaskTag = sl::ResourceTag{ &transparencyCompositionMask, sl::kBufferTypeTransparencyHint, sl::ResourceLifecycle::eValidUntilPresent, &lowResExtent };

		sl::ResourceTag resourceTags[] = { colorInTag, colorOutTag, depthTag, mvecTag, reactiveMaskTag, transparencyCompositionMaskTag };

		slSetTag(activeViewport, resourceTags, _countof(resourceTags), globals::d3d::context);

		sl::ViewportHandle view(activeViewport);
		const sl::BaseStructure* inputs[] = { &view };
		slEvaluateFeature(sl::kFeatureDLSS, *frameToken, inputs, _countof(inputs), globals::d3d::context);
	}
}

float Streamline::GetInputResolutionScale(uint32_t outputWidth, uint32_t outputHeight, uint32_t qualityMode)
{
	if (outputWidth == 0 || outputHeight == 0) {
		logger::error("[Streamline] Invalid output resolution {}x{} for DLSS optimal settings", outputWidth, outputHeight);
		return 1.0f;
	}

	sl::DLSSMode dlssMode;
	switch (qualityMode) {
	case 1:
		dlssMode = sl::DLSSMode::eMaxQuality;
		break;
	case 2:
		dlssMode = sl::DLSSMode::eBalanced;
		break;
	case 3:
		dlssMode = sl::DLSSMode::eMaxPerformance;
		break;
	case 4:
		dlssMode = sl::DLSSMode::eUltraPerformance;
		break;
	default:
		dlssMode = sl::DLSSMode::eDLAA;
		break;
	}

	sl::DLSSOptions dlssOptions{};
	dlssOptions.mode = dlssMode;
	dlssOptions.outputWidth = outputWidth;
	dlssOptions.outputHeight = outputHeight;
	dlssOptions.colorBuffersHDR = sl::Boolean::eTrue; /* could be a bad assumptioon, but Streamline::Upscale already tags our input color as HDR when we actually invoke DLSS, because the render target we hand over is the HDR scene buffer. Once we started reusing GetInputResolutionScale() for the VR per-eye path, I mirrored that flag inside the optimal-settings query so slDLSSGetOptimalSettings runs with the same assumptions as the real DLSS evaluation. Without explicitly setting colorBuffersHDR there, the helper would treat the request as an LDR workload and could suggest a mismatched render size.*/

	sl::DLSSOptimalSettings optimalSettings{};
	sl::Result result = slDLSSGetOptimalSettings(dlssOptions, optimalSettings);
	if (result != sl::Result::eOk) {
		logger::critical("[Streamline] Failed to get DLSS optimal settings, error code: {}", (int)result);
		return 1.0f;
	}

	float scaleX;
	float scaleY;

	if (globals::game::ui->GameIsPaused()) {
		// Calculate scale as ratio of minimum render resolution to output resolution
		scaleX = (float)optimalSettings.renderWidthMin / (float)outputWidth;
		scaleY = (float)optimalSettings.renderHeightMin / (float)outputHeight;
	} else {
		// Calculate scale as ratio of optimal render resolution to output resolution
		scaleX = (float)optimalSettings.optimalRenderWidth / (float)outputWidth;
		scaleY = (float)optimalSettings.optimalRenderHeight / (float)outputHeight;
	}

	// Use the average scale (both should be the same for uniform scaling)
	return (scaleX + scaleY) * 0.5f;
}

/**
 * @brief Releases DLSS resources and disables DLSS for the active viewports.
 *
 * Sets the DLSS mode to off and frees all DLSS-related resources associated with each viewport handle.
 */
void Streamline::DestroyDLSSResources()
{
	if (AreViewportHandlesEqual(viewport, sl::ViewportHandle{})) {
		return;
	}

	sl::DLSSOptions dlssOptions{};
	dlssOptions.mode = sl::DLSSMode::eOff;
	slDLSSSetOptions(viewport, dlssOptions);
	slFreeResources(sl::kFeatureDLSS, viewport);
}
