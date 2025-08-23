#pragma once

struct ENBPostProcessing : Feature
{
public:
	struct Settings
	{
		bool Enabled = false;
		std::string EffectPath = "";
	};

	Settings settings;

	virtual inline std::string GetName() override { return "ENB Post Processing"; }
	virtual inline std::string GetShortName() override { return "ENBPostProcessing"; }
	virtual std::string_view GetCategory() const override { return "Post-Processing"; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"ENB Post Processing provides a framework for loading and executing ENBSeries-compatible FX effect files.\n"
			"This allows for advanced post-processing effects and visual enhancements using DirectX 11 Effect (.fx) files.",
			{ "ENBSeries-compatible FX support",
				"DirectX 11 Effect file loading",
				"Advanced post-processing pipeline",
				"Custom technique execution",
				"Dynamic UI variable system" }
		};
	}

	struct alignas(16) PerFrame
	{
		float GradientIntensity;
		float GradientDesaturation;
		float GradientTopIntensity;
		float GradientTopCurve;

		float GradientMiddleIntensity;
		float GradientMiddleCurve;
		float GradientHorizonIntensity;
		float GradientHorizonCurve;

		float CloudsIntensity;
		float CloudsCurve;
		float CloudsDesaturation;
		float CloudsOpacity;

		float DirectLightingIntensity;
		float DirectLightingCurve;
		float DirectLightingDesaturation;
		float pad0;

		float3 DirectLightingColorFilter;
		float DirectLightingColorFilterAmount;

		float AmbientLightingIntensity;
		float AmbientLightingDesaturation;
		float2 pad1;

		float ColorPow;
		float3 pad2;

		float FogColorMultiplier;
		float FogColorCurve;
		float FogAmountMultiplier;
		float FogCurveMultiplier;

		float3 FogColorFilter;
		float FogColorFilterAmount;

		float IBLMultiplicativeAmount;
		float3 pad3;

		float VolumetricFogIntensity;
		float VolumetricFogCurve;
		float VolumetricFogOpacity;
		float pad4;

		float VolumetricRaysIntensity;
		float VolumetricRaysRangeFactor;
		float VolumetricRaysDesaturation;
		float pad5;
	};

	PerFrame GetCommonBufferData();

	virtual void SaveSettings(json&) override;
	virtual void LoadSettings(json&) override;
	virtual void RestoreDefaultSettings() override;
	virtual void DrawSettings() override;
	virtual void SetupResources() override;
	virtual void Reset() override;
	virtual void PostPostLoad() override;
};