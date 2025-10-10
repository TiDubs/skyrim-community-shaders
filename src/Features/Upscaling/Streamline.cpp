#include "Streamline.h"

#include <dxgi.h>
#include <dxgi1_3.h>

#include "../../Deferred.h"
#include "../../Hooks.h"
#include "../../State.h"
#include "../../Util.h"
#include "../Upscaling.h"
#include "DX12SwapChain.h"

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

	sl::Feature featuresToLoad[] = { sl::kFeatureDLSS, sl::kFeatureNIS };
	sl::Feature featuresToLoadVR[] = { sl::kFeatureDLSS, sl::kFeatureNIS };

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

	slIsFeatureLoaded(sl::kFeatureNIS, featureNIS);
	if (featureNIS) {
		logger::info("[Streamline] NIS feature is loaded");
		featureNIS = slIsFeatureSupported(sl::kFeatureNIS, adapterInfo) == sl::Result::eOk;
	} else {
		logger::info("[Streamline] NIS feature is not loaded");
		sl::FeatureRequirements featureRequirements;
		sl::Result result = slGetFeatureRequirements(sl::kFeatureNIS, featureRequirements);
		if (result != sl::Result::eOk) {
			logger::info("[Streamline] NIS feature failed to load due to: {}", magic_enum::enum_name(result));
		}
	}

	logger::info("[Streamline] DLSS {} available", featureDLSS ? "is" : "is not");
	logger::info("[Streamline] NIS {} available", featureNIS ? "is" : "is not");
}

void Streamline::PostDevice()
{
	// Hook up all of the feature functions using the sl function slGetFeatureFunction

	if (featureDLSS) {
		slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSGetOptimalSettings", (void*&)slDLSSGetOptimalSettings);
		slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSGetState", (void*&)slDLSSGetState);
		slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSSetOptions", (void*&)slDLSSSetOptions);
	}

	if (featureNIS) {
		slGetFeatureFunction(sl::kFeatureNIS, "slNISSetOptions", (void*&)slNISSetOptions);
		slGetFeatureFunction(sl::kFeatureNIS, "slNISGetState", (void*&)slNISGetState);
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

        sl::Constants slConstants = {};

        auto& upscaling = globals::features::upscaling;
        auto jitter = upscaling.jitter;

        slConstants.jitterOffset = { -jitter.x, -jitter.y };
        slConstants.reset = sl::Boolean::eFalse;
        slConstants.depthInverted = sl::Boolean::eFalse;
        slConstants.mvecScale = { 1.0f, 1.0f };
        slConstants.motionVectors3D = sl::Boolean::eFalse;
        slConstants.motionVectorsInvalidValue = FLT_MIN;
        slConstants.orthographicProjection = sl::Boolean::eFalse;
        slConstants.motionVectorsDilated = sl::Boolean::eFalse;
        slConstants.motionVectorsJittered = sl::Boolean::eFalse;

        if (globals::game::isVR) {
            auto screenSize = state->screenSize;
            auto renderSize = Util::ConvertToDynamic(screenSize);

            slConstants.cameraAspectRatio = (state->screenSize.x * 0.5f) / state->screenSize.y;
        } else {
            slConstants.cameraAspectRatio = state->screenSize.x / state->screenSize.y;
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
            recalculateCameraMatrices(slConstants);
        }

		if (SL_FAILED(res, slSetConstants(slConstants, *frameToken, viewport))) {
			logger::error("[Streamline] Could not set constants");
		}
	}
}

void Streamline::SetDLSSOptions()
{
	sl::DLSSOptions dlssOptions{};

	// Map quality mode to DLSS mode
	uint32_t qualityMode = globals::features::upscaling.settings.qualityMode;
	switch (qualityMode) {
	case 1:
		dlssOptions.mode = sl::DLSSMode::eMaxQuality;
		break;
	case 2:
		dlssOptions.mode = sl::DLSSMode::eBalanced;
		break;
	case 3:
		dlssOptions.mode = sl::DLSSMode::eMaxPerformance;
		break;
	case 4:
		dlssOptions.mode = sl::DLSSMode::eUltraPerformance;
		break;
	default:
		dlssOptions.mode = sl::DLSSMode::eDLAA;
		break;
	}

	auto state = globals::state;

	uint32_t outputWidth = static_cast<uint32_t>(state->screenSize.x);
	uint32_t outputHeight = static_cast<uint32_t>(state->screenSize.y);

	if (globals::game::isVR) {
		// In VR the back buffer contains both eyes side-by-side. DLSS operates on a single eye at a time, so expose the per-eye dimensions to
		// Streamline to avoid stretching the upscaled image to the full stereo width.
		outputWidth /= 2;
	}

	dlssOptions.outputWidth = outputWidth;
	dlssOptions.outputHeight = outputHeight;
	dlssOptions.colorBuffersHDR = sl::Boolean::eTrue;
	dlssOptions.useAutoExposure = sl::Boolean::eTrue;

	dlssOptions.preExposure = 1.0f;
	dlssOptions.sharpness = 0.0f;

	// Set DLSS preset based on VR mode
	sl::DLSSPreset preset = sl::DLSSPreset::ePresetK;  // Default
	switch (globals::features::upscaling.settings.DLSSPreset) {
	case 0:
		preset = sl::DLSSPreset::ePresetF;
		break;
	case 1:
		preset = sl::DLSSPreset::ePresetJ;
		break;
	case 2:
	default:
		preset = sl::DLSSPreset::ePresetK;
		break;
	}

	dlssOptions.dlaaPreset = preset;
	dlssOptions.qualityPreset = preset;
	dlssOptions.balancedPreset = preset;
	dlssOptions.performancePreset = preset;
	dlssOptions.ultraPerformancePreset = preset;

	if (SL_FAILED(result, slDLSSSetOptions(viewport, dlssOptions))) {
		logger::critical("[Streamline] Could not enable DLSS");
	}
}

void Streamline::ReleaseVREyeTextures(EyeTextureCache& cache)
{
	cache.initialized = false;
	cache.desc = {};
	for (auto& texture : cache.textures) {
		texture = nullptr;
	}
}

void Streamline::EnsureVREyeTextures(EyeTextureCache& cache, ID3D11Texture2D* source, UINT eyeWidth, UINT eyeHeight)
{
	if (!source || eyeWidth == 0 || eyeHeight == 0) {
		ReleaseVREyeTextures(cache);
		return;
	}

	D3D11_TEXTURE2D_DESC desiredDesc{};
	source->GetDesc(&desiredDesc);
	if (desiredDesc.Width < eyeWidth * cache.textures.size() || desiredDesc.Height < eyeHeight) {
		ReleaseVREyeTextures(cache);
		return;
	}

	desiredDesc.Width = eyeWidth;
	desiredDesc.Height = eyeHeight;

	bool needsCreate = !cache.initialized ||
	cache.desc.Width != desiredDesc.Width ||
	cache.desc.Height != desiredDesc.Height ||
	cache.desc.MipLevels != desiredDesc.MipLevels ||
	cache.desc.ArraySize != desiredDesc.ArraySize ||
	cache.desc.Format != desiredDesc.Format ||
	cache.desc.SampleDesc.Count != desiredDesc.SampleDesc.Count ||
	cache.desc.SampleDesc.Quality != desiredDesc.SampleDesc.Quality ||
	cache.desc.Usage != desiredDesc.Usage ||
	cache.desc.BindFlags != desiredDesc.BindFlags ||
	cache.desc.CPUAccessFlags != desiredDesc.CPUAccessFlags ||
	cache.desc.MiscFlags != desiredDesc.MiscFlags;

	if (needsCreate) {
		ReleaseVREyeTextures(cache);
		for (auto& texture : cache.textures) {
			DX::ThrowIfFailed(globals::d3d::device->CreateTexture2D(&desiredDesc, nullptr, texture.put()));
		}
		cache.desc = desiredDesc;
		cache.initialized = true;
	}
}

void Streamline::CopyVREyeRegion(ID3D11Texture2D* source, EyeTextureCache& cache, UINT eyeWidth, UINT eyeHeight)
{
	if (!source || !cache.initialized || eyeWidth == 0 || eyeHeight == 0) {
		return;
	}

	D3D11_TEXTURE2D_DESC srcDesc{};
	source->GetDesc(&srcDesc);
	if (srcDesc.Width < eyeWidth * cache.textures.size() || srcDesc.Height < eyeHeight) {
		ReleaseVREyeTextures(cache);
		return;
	}

	D3D11_BOX srcBox{};
	srcBox.top = 0;
	srcBox.bottom = eyeHeight;
	srcBox.front = 0;
	srcBox.back = 1;

	for (uint32_t eye = 0; eye < cache.textures.size(); ++eye) {
		srcBox.left = eye * eyeWidth;
		srcBox.right = srcBox.left + eyeWidth;
		globals::d3d::context->CopySubresourceRegion(cache.textures[eye].get(), 0, 0, 0, 0, source, 0, &srcBox);
	}
}

void Streamline::Upscale(ID3D11Resource* a_upscalingTexture, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_motionVectors)
{
	CheckFrameConstants();
	SetDLSSOptions();

	auto state = globals::state;

	auto renderer = globals::game::renderer;
	auto& depthTexture = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

	auto screenSize = state->screenSize;
	auto renderSize = Util::ConvertToDynamic(screenSize);

    auto tagResources = [&](const sl::Extent& lowExtent, const sl::Extent& fullExtent, ID3D11Resource* colorInputResource) {
        if (!colorInputResource) {
            return;
        }

        sl::Resource colorIn = { sl::ResourceType::eTex2d, colorInputResource, 0 };
        sl::Resource colorOut = { sl::ResourceType::eTex2d, a_upscalingTexture, 0 };
        sl::Resource depth = { sl::ResourceType::eTex2d, depthTexture.texture, 0 };
        sl::Resource mvec = { sl::ResourceType::eTex2d, a_motionVectors, 0 };

        sl::ResourceTag colorInTag{ &colorIn, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eOnlyValidNow, &lowExtent };
        sl::ResourceTag colorOutTag{ &colorOut, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eOnlyValidNow, &fullExtent };
        sl::ResourceTag depthTag{ &depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &lowExtent };
        sl::ResourceTag mvecTag{ &mvec, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &lowExtent };

        sl::Resource reactiveMask = { sl::ResourceType::eTex2d, a_reactiveMask, 0 };
        sl::ResourceTag reactiveMaskTag{ &reactiveMask, sl::kBufferTypeBiasCurrentColorHint, sl::ResourceLifecycle::eValidUntilPresent, &lowExtent };

        sl::Resource transparencyCompositionMask = { sl::ResourceType::eTex2d, a_transparencyCompositionMask, 0 };
        sl::ResourceTag transparencyCompositionMaskTag{ &transparencyCompositionMask, sl::kBufferTypeTransparencyHint, sl::ResourceLifecycle::eValidUntilPresent, &lowExtent };

        sl::ResourceTag resourceTags[] = { colorInTag, colorOutTag, depthTag, mvecTag, reactiveMaskTag, transparencyCompositionMaskTag };

        slSetTag(viewport, resourceTags, _countof(resourceTags), globals::d3d::context);
    };

    if (globals::game::isVR) {
        winrt::com_ptr<ID3D11Texture2D> colorTexture;
        HRESULT hr = a_upscalingTexture->QueryInterface(colorTexture.put());
        if (FAILED(hr)) {
            logger::error("[Streamline] Failed to access VR color texture for eye split (HRESULT {:08X})", static_cast<unsigned int>(hr));
        }

		ID3D11Resource* colorResource = a_upscalingTexture;
		UINT perEyeRenderWidth = 0;
		UINT perEyeRenderHeight = 0;

		if (colorTexture) {
			D3D11_TEXTURE2D_DESC colorDesc{};
			colorTexture->GetDesc(&colorDesc);
			perEyeRenderWidth = colorDesc.Width / 2;
			perEyeRenderHeight = colorDesc.Height;

			EnsureVREyeTextures(vrColorEyeTextures, colorTexture.get(), perEyeRenderWidth, perEyeRenderHeight);
			if (vrColorEyeTextures.initialized) {
				CopyVREyeRegion(colorTexture.get(), vrColorEyeTextures, perEyeRenderWidth, perEyeRenderHeight);
				colorResource = vrColorEyeTextures.textures[0].get();
			}
		}

        if (perEyeRenderWidth > 0 && perEyeRenderHeight > 0 && colorResource != nullptr) {
            UINT perEyeOutputWidth = static_cast<UINT>(screenSize.x * 0.5f);
            UINT perEyeOutputHeight = static_cast<UINT>(screenSize.y);

            sl::Extent leftEyeLowExtent{ 0, 0, perEyeRenderWidth, perEyeRenderHeight };
            sl::Extent leftEyeFullExtent{ 0, 0, perEyeOutputWidth, perEyeOutputHeight };

            tagResources(leftEyeLowExtent, leftEyeFullExtent, colorResource);
        } else {
            ReleaseVREyeTextures(vrColorEyeTextures);

            sl::Extent lowResExtent{ 0, 0, (uint)renderSize.x, (uint)renderSize.y };
            sl::Extent fullExtent{ 0, 0, (uint)screenSize.x, (uint)screenSize.y };

            tagResources(lowResExtent, fullExtent, a_upscalingTexture);
        }
    } else {
        sl::Extent lowResExtent{ 0, 0, (uint)renderSize.x, (uint)renderSize.y };
        sl::Extent fullExtent{ 0, 0, (uint)screenSize.x, (uint)screenSize.y };

        tagResources(lowResExtent, fullExtent, a_upscalingTexture);
    }

	sl::ViewportHandle view(viewport);
	const sl::BaseStructure* inputs[] = { &view };
	slEvaluateFeature(sl::kFeatureDLSS, *frameToken, inputs, _countof(inputs), globals::d3d::context);
}

float2 Streamline::GetInputResolutionScale(uint32_t outputWidth, uint32_t outputHeight, uint32_t qualityMode)
{
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
	if (globals::game::isVR) {
		// DLSS evaluation is performed per-eye in VR. Provide per-eye dimensions when querying optimal render resolution so that the recommended scale matches the actual input size.
		outputWidth /= 2;
	}

	dlssOptions.outputWidth = outputWidth;
	dlssOptions.outputHeight = outputHeight;

	sl::DLSSOptimalSettings optimalSettings{};
	sl::Result result = slDLSSGetOptimalSettings(dlssOptions, optimalSettings);
	if (result != sl::Result::eOk) {
		logger::critical("[Streamline] Failed to get DLSS optimal settings, error code: {}", (int)result);
		return { 1.0f, 1.0f };
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

	// Return separate X and Y scales for more precision
	return { scaleX, scaleY };
}

/**
 * @brief Releases DLSS resources and disables DLSS for the current viewport.
 *
 * Sets the DLSS mode to off and frees all DLSS-related resources associated with the viewport.
 */
void Streamline::DestroyDLSSResources()
{
	sl::DLSSOptions dlssOptions{};
	dlssOptions.mode = sl::DLSSMode::eOff;
	slDLSSSetOptions(viewport, dlssOptions);
	slFreeResources(sl::kFeatureDLSS, viewport);
}

void Streamline::ApplyNISSharpening(ID3D11Resource* a_texture, float sharpness)
{
	if (!featureNIS) {
		return;
	}

	CheckFrameConstants();

	sl::NISOptions nisOptions{};
	nisOptions.mode = sl::NISMode::eSharpen;
	nisOptions.sharpness = std::clamp(sharpness, 0.0f, 1.0f);
	nisOptions.hdrMode = sl::NISHDR::eNone;

	if (SL_FAILED(result, slNISSetOptions(viewport, nisOptions))) {
		logger::error("[Streamline] Could not set NIS options");
		return;
	}

	auto state = globals::state;
	sl::Extent fullExtent{ 0, 0, (uint)state->screenSize.x, (uint)state->screenSize.y };

	sl::Resource colorIn = { sl::ResourceType::eTex2d, a_texture, 0 };
	sl::Resource colorOut = { sl::ResourceType::eTex2d, a_texture, 0 };

	sl::ResourceTag colorInTag = sl::ResourceTag{ &colorIn, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eOnlyValidNow, &fullExtent };
	sl::ResourceTag colorOutTag = sl::ResourceTag{ &colorOut, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eOnlyValidNow, &fullExtent };

	sl::ResourceTag resourceTags[] = { colorInTag, colorOutTag };

	slSetTag(viewport, resourceTags, _countof(resourceTags), globals::d3d::context);

	sl::ViewportHandle view(viewport);
	const sl::BaseStructure* inputs[] = { &view };
	if (SL_FAILED(result, slEvaluateFeature(sl::kFeatureNIS, *frameToken, inputs, _countof(inputs), globals::d3d::context))) {
		logger::error("[Streamline] Failed to evaluate NIS feature");
	}
}
