#include "Streamline.h"

#include <algorithm>
#include <cstring>
#include <type_traits>
#include <cfloat>
#include <vector>
#include <cstdint>
#include <string>
#include <filesystem>

#include <dxgi.h>
#include <dxgi1_3.h>
#include <d3d11.h>

#include <magic_enum.hpp>

#include "../../Deferred.h"
#include "../../Hooks.h"
#include "../../State.h"
#include "../../Util.h"
#include "../Upscaling.h"
#include "DX12SwapChain.h"

static ID3D11Texture2D* gDlssOutTex = nullptr; // output texture for upscaling

namespace
{
static_assert(std::is_trivially_copyable_v<sl::ViewportHandle>);

[[nodiscard]] bool IsViewportAllocated(const sl::ViewportHandle& handle) noexcept
{
	// Consider both all-zero and all-ones bit patterns as invalid/unallocated
	sl::ViewportHandle zero{};
	sl::ViewportHandle ones{};
	std::memset(&ones, 0xFF, sizeof(ones));

	if (std::memcmp(&handle, &zero, sizeof(handle)) == 0) {
		return false;
	}
	if (std::memcmp(&handle, &ones, sizeof(handle)) == 0) {
		return false;
	}
	return true;
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
pref.logLevel = sl::LogLevel::eVerbose;  //TEMP
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
	if (!featureDLSS) {
		return;
	}

	// 1) Bind the D3D11 device to Streamline (required for manual hooking)
	if (slSetD3DDevice && globals::d3d::context) {
		ID3D11Device* dev = nullptr;
		globals::d3d::context->GetDevice(&dev);
		if (dev) {
			sl::Result r = slSetD3DDevice(dev);
			if (r != sl::Result::eOk) {
				logger::error("[Streamline] slSetD3DDevice failed: {}", (int)r);
			}
			dev->Release();
		} else {
			logger::error("[Streamline] D3D11 device is null; cannot set for Streamline");
		}
	}

	// 2) Fetch DLSS entry points
	slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSGetOptimalSettings", (void*&)slDLSSGetOptimalSettings);
	slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSGetState", (void*&)slDLSSGetState);
	slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSSetOptions", (void*&)slDLSSSetOptions);

	// 3) Assign stable per-eye viewport handles (no allocation here)
	const uint32_t eyeCount = globals::game::isVR ? 2u : 1u;
	for (uint32_t eyeIndex = 0; eyeIndex < eyeCount; ++eyeIndex) {
		EnsureViewportAllocated(eyeIndex);
	}
}

/**
 * @brief Updates and sets camera and frame constants for the current Streamline frame.
 *
 * Populates and submits camera parameters, projection matrices, motion vector settings, and other per-frame constants to the Streamline SDK for the current frame. Uses cached framebuffer data and global state to ensure correct configuration for upscaling and frame generation features.
 */
void Streamline::CheckFrameConstants()
{
	if (!frameChecker.IsNewFrame() || !globals::features::upscaling.streamline.initialized) {
		return;
	}

	slGetNewFrameToken(frameToken, &globals::state->frameCount);

	auto state = globals::state;
	const bool isVR = globals::game::isVR;
	const uint32_t eyeCount = isVR ? 2u : 1u;

	// Ensure at least the left-eye viewport exists
	if (!EnsureViewportAllocated(0)) {
		return;
	}

	// If ANY eye hasn't been initialized yet, assert reset once for both
	bool resetThisFrame = false;
	for (uint32_t i = 0; i < eyeCount; ++i) {
		if (!eyes[i].constantsInitialized) {
			resetThisFrame = true;
			break;
		}
	}

	const float totalWidth = state->screenSize.x;
	const float totalHeight = state->screenSize.y;
	const float safeHeight = totalHeight > 0.0f ? totalHeight : 1.0f;

	const float cameraFov = Util::GetVerticalFOVRad();
	const float cameraNear = *globals::game::cameraNear;
	const float cameraFar = *globals::game::cameraFar;
	const auto jitter = globals::features::upscaling.jitter;

	// Build a single set of constants for the frame
	sl::Constants slConstants{};

	// IMPORTANT: "common" values must be IDENTICAL for all viewports, and set once
	const float perEyeWidth = isVR ? (totalWidth * 0.5f) : totalWidth;
	const float aspectCommon = perEyeWidth / safeHeight;
	slConstants.cameraAspectRatio = aspectCommon;
	slConstants.cameraFOV = cameraFov;
	slConstants.cameraNear = cameraNear;
	slConstants.cameraFar = cameraFar;

	slConstants.cameraMotionIncluded = sl::Boolean::eTrue;
	slConstants.cameraPinholeOffset = { 0.f, 0.f };
	slConstants.depthInverted = sl::Boolean::eFalse;
	slConstants.jitterOffset = { -jitter.x, -jitter.y };
	slConstants.reset = resetThisFrame ? sl::Boolean::eTrue : sl::Boolean::eFalse;

	slConstants.mvecScale = { isVR ? 0.5f : 1.0f, 1.0f };
	slConstants.motionVectors3D = sl::Boolean::eFalse;
	slConstants.motionVectorsInvalidValue = FLT_MIN;
	slConstants.orthographicProjection = sl::Boolean::eFalse;
	slConstants.motionVectorsDilated = sl::Boolean::eFalse;
	slConstants.motionVectorsJittered = sl::Boolean::eFalse;

	// Use LEFT eye (0) for the matrices we must provide; right eye will use tags/extents for its region
	const uint32_t refEye = 0;
	const auto viewMatrix = globals::game::frameBufferCached.GetCameraViewInverse(refEye).Transpose();
	slConstants.cameraRight = { viewMatrix._11, viewMatrix._12, viewMatrix._13 };
	slConstants.cameraUp = { viewMatrix._21, viewMatrix._22, viewMatrix._23 };
	slConstants.cameraFwd = { viewMatrix._31, viewMatrix._32, viewMatrix._33 };

	const auto& cameraPosAdjust = globals::game::frameBufferCached.GetCameraPosAdjust(refEye);
	slConstants.cameraPos = { cameraPosAdjust.x, cameraPosAdjust.y, cameraPosAdjust.z };

	const auto cameraViewToClip = globals::game::frameBufferCached.GetCameraProjUnjittered(refEye).Transpose();
	std::memcpy(&slConstants.cameraViewToClip, &cameraViewToClip, sizeof(slConstants.cameraViewToClip));

	recalculateCameraMatrices(slConstants);

	sl::Result setConstantsResult = slSetConstants(slConstants, *frameToken, eyes[refEye].viewport);
	if (setConstantsResult != sl::Result::eOk) {
		logger::error("[Streamline] slSetConstants failed (err={})", (int)setConstantsResult);
		return;
	}

	// Mark both eyes initialized for this frame so Upscale() can proceed
	for (uint32_t i = 0; i < eyeCount; ++i) {
		eyes[i].constantsInitialized = true;
		EnsureViewportAllocated(i);
		AllocateResourcesIfNeeded(i);
	}
}

void Streamline::Upscale(ID3D11Resource* a_upscalingTexture, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_motionVectors, sl::DLSSPreset a_preset)
{
	CheckFrameConstants();

	// Keep stereo consistent if any eye is missing constants
	for (uint32_t i = 0, n = globals::game::isVR ? 2u : 1u; i < n; ++i) {
		if (!eyes[i].constantsInitialized) {
			logger::warn("[DLSSVR] Skipping frame: constants not initialized for eye {}", (int)i);
			return;
		}
	}

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

	//Ensure (or create) the private atlas-sized DLSS output texture once per frame
	if (!a_upscalingTexture || !a_motionVectors || !depthTexture.texture) {
		logger::warn("[DLSSVR] Missing required resource(s): color={}, mvec={}, depth={}",
			(void*)a_upscalingTexture, (void*)a_motionVectors, (void*)depthTexture.texture);
		return;
	}

	{
		ID3D11Device* dev = nullptr;
		globals::d3d::context->GetDevice(&dev);

		ID3D11Texture2D* inTex2D = nullptr;
		HRESULT qi = a_upscalingTexture ? a_upscalingTexture->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&inTex2D) : E_POINTER;

		if (dev && SUCCEEDED(qi) && inTex2D) {
			D3D11_TEXTURE2D_DESC inDesc{};
			inTex2D->GetDesc(&inDesc);
			
			if (inDesc.SampleDesc.Count > 1) {
				logger::warn("[DLSSVR] Input color is MSAA ({}x). DLSS expects non-MSAA color.", inDesc.SampleDesc.Count);
			}

			bool needNew = (gDlssOutTex == nullptr);
			
			DXGI_FORMAT resourceFormat = inDesc.Format; // Keep the resource format identical to the input atlas so CopySubresourceRegion is trivially compatible.

			if (!needNew) {
				D3D11_TEXTURE2D_DESC outDesc{};
				gDlssOutTex->GetDesc(&outDesc);
				if (outDesc.Width != totalOutputWidth ||
					outDesc.Height != totalOutputHeight ||
					outDesc.Format != resourceFormat ||
					outDesc.SampleDesc.Count != 1) {
					gDlssOutTex->Release();
					gDlssOutTex = nullptr;
					needNew = true;
				}
			}

			if (needNew) {
				D3D11_TEXTURE2D_DESC outDesc = inDesc;
				outDesc.Width = totalOutputWidth;
				outDesc.Height = totalOutputHeight;
				outDesc.Format = resourceFormat;  // match input atlas
				outDesc.MipLevels = 1;

				// Non-MSAA + UAV-capable
				outDesc.SampleDesc.Count = 1;
				outDesc.SampleDesc.Quality = 0;
				outDesc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;

				HRESULT hr = dev->CreateTexture2D(&outDesc, nullptr, &gDlssOutTex);
				if (FAILED(hr) || !gDlssOutTex) {
					// Fall back to a guaranteed UAV format if input format was not UAV-capable
					outDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
					hr = dev->CreateTexture2D(&outDesc, nullptr, &gDlssOutTex);
					if (FAILED(hr) || !gDlssOutTex) {
						logger::error("[DLSSVR] Failed to create DLSS output texture even after FP16 fallback (hr=0x{:08X})", (unsigned)hr);
						if (inTex2D)
							inTex2D->Release();
						if (dev)
							dev->Release();
						return;// bail this frame
					}
				} 
			}
		}

		if (inTex2D)
			inTex2D->Release();
		if (dev)
			dev->Release();

		if (!gDlssOutTex) {
			logger::error("[DLSSVR] gDlssOutTex is null; cannot run DLSS this frame");
			return;
		}
	}

	// Probe actual resource sizes. We will build low res extents from these
	D3D11_TEXTURE2D_DESC colorInDesc{};
	{
		ID3D11Texture2D* t = nullptr;
		if (!a_upscalingTexture || FAILED(a_upscalingTexture->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t)) || !t) {
			logger::error("[DLSSVR] Invalid color input resource");
			return;
		}
		t->GetDesc(&colorInDesc);
		t->Release();
	}

	D3D11_TEXTURE2D_DESC mvDesc{};
	if (a_motionVectors) {
		ID3D11Texture2D* t = nullptr;
		if (SUCCEEDED(a_motionVectors->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t)) && t) {
			t->GetDesc(&mvDesc);
			t->Release();
		}
	}

	D3D11_TEXTURE2D_DESC depthDesc{};
	if (depthTexture.texture) {
		ID3D11Texture2D* t = nullptr;
		if (SUCCEEDED(depthTexture.texture->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t)) && t) {
			t->GetDesc(&depthDesc);
			t->Release();
		}
	}

	// Are we actually upscaling? (input atlas smaller than output atlas)
	const bool isUpscaling = (colorInDesc.Width < totalOutputWidth) || (colorInDesc.Height < totalOutputHeight);
	const bool isDLAA = (baseDlssOptions.mode == sl::DLSSMode::eDLAA);

	// Per-eye work
	for (uint32_t eyeIndex = 0; eyeIndex < eyeCount; ++eyeIndex) {
		if (!EnsureViewportAllocated(eyeIndex)) {
			if (eyeCount > 1) {
				logger::warn("[DLSSVR] Skipping DLSS for eye {} (no viewport)", (int)eyeIndex);
			}
			return;  // keep both eyes consistent
		}

		auto& activeViewport = eyes[eyeIndex].viewport;

		// Per eye OUTPUT extents (final atlas)
		const uint32_t outBaseW = eyeCount ? (totalOutputWidth / eyeCount) : totalOutputWidth;
		const uint32_t outOffX = isVR ? outBaseW * eyeIndex : 0u;
		const uint32_t outCurrW = (isVR && eyeIndex == eyeCount - 1) ? (totalOutputWidth - outOffX) : (isVR ? outBaseW : totalOutputWidth);
		const uint32_t outEndX = std::min(outOffX + outCurrW, totalOutputWidth);
		const sl::Extent fullExtent{ outOffX, 0, outEndX, totalOutputHeight };

		// Per eye INPUT (low-res) extents. derive from actual input atlas size
		const uint32_t lowAtlasW = colorInDesc.Width;
		const uint32_t lowAtlasH = colorInDesc.Height;
		const uint32_t lowBaseW = eyeCount ? (lowAtlasW / eyeCount) : lowAtlasW;
		const uint32_t lowOffX = isVR ? lowBaseW * eyeIndex : 0u;
		const uint32_t lowCurrW = (isVR && eyeIndex == eyeCount - 1) ? (lowAtlasW - lowOffX) : (isVR ? lowBaseW : lowAtlasW);
		const uint32_t lowEndX = std::min(lowOffX + lowCurrW, lowAtlasW);
		const sl::Extent lowExtentColor{ lowOffX, 0, lowEndX, lowAtlasH };

		// Motion vectors / depth may live at different (low-res) atlases; prefer their own sizes if provided
		sl::Extent lowExtentMV = lowExtentColor;
		if (mvDesc.Width && mvDesc.Height) {
			const uint32_t mvBaseW = eyeCount ? (mvDesc.Width / eyeCount) : mvDesc.Width;
			const uint32_t mvOffX = isVR ? mvBaseW * eyeIndex : 0u;
			const uint32_t mvCurrW = (isVR && eyeIndex == eyeCount - 1) ? (mvDesc.Width - mvOffX) : (isVR ? mvBaseW : mvDesc.Width);
			const uint32_t mvEndX = std::min(mvOffX + mvCurrW, mvDesc.Width);
			lowExtentMV = sl::Extent{ mvOffX, 0, mvEndX, mvDesc.Height };
		}

		sl::Extent lowExtentDepth = lowExtentColor;
		if (depthDesc.Width && depthDesc.Height) {
			const uint32_t dBaseW = eyeCount ? (depthDesc.Width / eyeCount) : depthDesc.Width;
			const uint32_t dOffX = isVR ? dBaseW * eyeIndex : 0u;
			const uint32_t dCurrW = (isVR && eyeIndex == eyeCount - 1) ? (depthDesc.Width - dOffX) : (isVR ? dBaseW : depthDesc.Width);
			const uint32_t dEndX = std::min(dOffX + dCurrW, depthDesc.Width);
			lowExtentDepth = sl::Extent{ dOffX, 0, dEndX, depthDesc.Height };
		}

		// Tag resources
		sl::Resource colorIn = { sl::ResourceType::eTex2d, a_upscalingTexture, 0 };
		sl::Resource colorOut = { sl::ResourceType::eTex2d, gDlssOutTex, 0 };
		sl::Resource depth = { sl::ResourceType::eTex2d, depthTexture.texture, 0 };
		sl::Resource mvec = { sl::ResourceType::eTex2d, a_motionVectors, 0 };

		std::vector<sl::ResourceTag> tags;
		tags.reserve(6);
		tags.push_back({ &colorIn, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &lowExtentColor });
		tags.push_back({ &colorOut, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &fullExtent });
		tags.push_back({ &depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilEvaluate, &lowExtentDepth });
		tags.push_back({ &mvec, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilEvaluate, &lowExtentMV });

		if (a_reactiveMask) {
			static sl::Resource reactive{};
			reactive = { sl::ResourceType::eTex2d, a_reactiveMask, 0 };
			tags.push_back({ &reactive, sl::kBufferTypeBiasCurrentColorHint, sl::ResourceLifecycle::eValidUntilEvaluate, &lowExtentColor });
		}
		if (a_transparencyCompositionMask) {
			static sl::Resource transp{};
			transp = { sl::ResourceType::eTex2d, a_transparencyCompositionMask, 0 };
			tags.push_back({ &transp, sl::kBufferTypeTransparencyHint, sl::ResourceLifecycle::eValidUntilEvaluate, &lowExtentColor });
		}

		slSetTag(activeViewport, tags.data(), (uint32_t)tags.size(), globals::d3d::context);

		if (!AllocateResourcesIfNeeded(eyeIndex)) {
			logger::warn("[DLSSVR] Failed to allocate Streamline resources for eye {}", (int)eyeIndex);
			return;  // keep both eyes consistent
		}

		// Debug: show whether we are upscaling and the per-eye sizes
		logger::info("[DLSSVR] eye={} isDLAA={} isUpscaling={} inAtlas={}x{} perEyeInW={} perEyeOutW={} outAtlas={}x{}",
			(int)eyeIndex, isDLAA ? 1 : 0, isUpscaling ? 1 : 0, lowAtlasW, lowAtlasH, lowEndX - lowOffX, outEndX - outOffX, totalOutputWidth, totalOutputHeight);

		// Options: per-eye OUTPUT size; choose DLAA only if per-eye input == per-eye output
		sl::DLSSOptions dlssOptions = baseDlssOptions;

		// Per-eye same res check (DLAA only when low == out)
		const bool sameResPerEye = (lowCurrW == outCurrW) && (lowAtlasH == totalOutputHeight);
		if (sameResPerEye) {
			dlssOptions.mode = sl::DLSSMode::eDLAA;  // identical res then DLAA - should never get hit unless DLAA is already chosen, but is required by streamline
		}

		// Set the final output size we are asking DLSS for (per-eye width, full height)
		dlssOptions.outputWidth = outCurrW;
		dlssOptions.outputHeight = totalOutputHeight;

		logger::info("[DLSSVR] eye={} mode={} out={}x{} low={}x{}",
			(int)eyeIndex,
			(int)dlssOptions.mode,
			dlssOptions.outputWidth, dlssOptions.outputHeight,
			lowCurrW, lowAtlasH);

		sl::Result setOptionsResult = slDLSSSetOptions(activeViewport, dlssOptions);
		if (setOptionsResult != sl::Result::eOk) {
			logger::critical("[DLSSVR] SetOptions failed eye={} err={}", (int)eyeIndex, (int)setOptionsResult);
			return;  // keep both eyes consistent
		}

		// Evaluate
		sl::ViewportHandle view(activeViewport);
		const sl::BaseStructure* inputs[] = { &view };
		sl::Result evalRes = slEvaluateFeature(sl::kFeatureDLSS, *frameToken, inputs, _countof(inputs), globals::d3d::context);
		if (evalRes != sl::Result::eOk) {
			logger::warn("[DLSSVR] slEvaluateFeature returned {}", (int)evalRes);
			return;  // keep both eyes consistent
		}

		// Copy back the upscaled sub-rect to the game's atlas
		D3D11_BOX box{};
		box.left = outOffX;
		box.right = outEndX;
		box.top = 0;
		box.bottom = totalOutputHeight;
		box.front = 0;
		box.back = 1;

		globals::d3d::context->CopySubresourceRegion(a_upscalingTexture, 0, outOffX, 0, 0, gDlssOutTex, 0, &box);
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
	dlssOptions.colorBuffersHDR = sl::Boolean::eTrue; /* could be a bad assumption, but Streamline::Upscale already tags our input color as HDR when we actually invoke DLSS, because the render target we hand over is the HDR scene buffer. Once we started reusing GetInputResolutionScale() for the VR per-eye path, I mirrored that flag inside the optimal-settings query so slDLSSGetOptimalSettings runs with the same assumptions as the real DLSS evaluation. Without explicitly setting colorBuffersHDR there, the helper would treat the request as an LDR workload and could suggest a mismatched render size.*/

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
	if (gDlssOutTex) {
		gDlssOutTex->Release();
		gDlssOutTex = nullptr;
	}

	bool anyAllocated = false;
	for (const auto& eye : eyes) {
		if (IsViewportAllocated(eye.viewport)) {
			anyAllocated = true;
			break;
		}
	}

	if (!anyAllocated) {
		return;
	}

	sl::DLSSOptions dlssOptions{};
	dlssOptions.mode = sl::DLSSMode::eOff;

	const uint32_t eyeCount = static_cast<uint32_t>(eyes.size());
	for (uint32_t eyeIndex = 0; eyeIndex < eyeCount; ++eyeIndex) {
		auto& eye = eyes[eyeIndex];
		if (!IsViewportAllocated(eye.viewport)) {
			ResetEyeState(eyeIndex);
			continue;
		}

		slDLSSSetOptions(eye.viewport, dlssOptions);

		// No manual slFreeResources for D3D11, interposer manages this.

		ResetEyeState(eyeIndex);
	}
}

bool Streamline::EnsureViewportAllocated(uint32_t eyeIndex)
{
	if (eyeIndex >= static_cast<uint32_t>(eyes.size()))
		return false;
	if (!featureDLSS)
		return false;

	auto& eye = eyes[eyeIndex];

	// Already allocated? Reuse as-is
	if (IsViewportAllocated(eye.viewport))
		return true;

	// First-time assignment: stable, non-zero per-eye handle
	const uint64_t id = 0x100ull + static_cast<uint64_t>(eyeIndex);
	std::memset(&eye.viewport, 0, sizeof(eye.viewport));
	std::memcpy(&eye.viewport, &id, std::min(sizeof(eye.viewport), sizeof(id)));

	eye.resourcesAllocated = false;

	return true;
}

bool Streamline::AllocateResourcesIfNeeded(uint32_t eyeIndex)
{
	if (!featureDLSS)
		return true;
	if (eyeIndex >= static_cast<uint32_t>(eyes.size()))
		return false;

	auto& eye = eyes[eyeIndex];
	if (!IsViewportAllocated(eye.viewport))
		return false;
	if (eye.resourcesAllocated)
		return true;
	if (!slAllocateResources) {
		static bool loggedMissing = false;
		if (!loggedMissing) {
			logger::warn("[Streamline] slAllocateResources entry point is unavailable; skipping explicit allocation");
			loggedMissing = true;
		}
		return true;
	}
	if (!frameToken) {
		logger::warn("[Streamline] Cannot allocate resources without a valid frame token");
		return false;
	}
	if (!globals::d3d::context) {
		logger::error("[Streamline] Cannot allocate resources without a D3D11 context");
		return false;
	}

	sl::ViewportHandle view(eye.viewport);
	const sl::BaseStructure* inputs[] = { &view };
	sl::Result allocateResult = slAllocateResources(*frameToken, inputs, _countof(inputs), globals::d3d::context);
	if (allocateResult != sl::Result::eOk) {
		logger::error("[Streamline] slAllocateResources failed eye={} err={}", (int)eyeIndex, (int)allocateResult);
		return false;
	}

	eye.resourcesAllocated = true;
	return true;
}

void Streamline::ResetEyeState(uint32_t eyeIndex)
{
	if (eyeIndex >= static_cast<uint32_t>(eyes.size())) {
		return;
	}

	eyes[eyeIndex].viewport = {};
	eyes[eyeIndex].constantsInitialized = false;
	eyes[eyeIndex].resourcesAllocated = false;
}
