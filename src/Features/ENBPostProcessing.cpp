#include "ENBPostProcessing.h"
#include "PCH.h"
#include "State.h"

void ENBPostProcessing::SaveSettings(json& o_json)
{
	o_json["Enabled"] = settings.Enabled;
	o_json["EffectPath"] = settings.EffectPath;
}

void ENBPostProcessing::LoadSettings(json& o_json)
{
	if (o_json["Enabled"].is_boolean())
		settings.Enabled = o_json["Enabled"];

	if (o_json["EffectPath"].is_string())
		settings.EffectPath = o_json["EffectPath"];
}

void ENBPostProcessing::RestoreDefaultSettings()
{
	settings.Enabled = false;
	settings.EffectPath = "";
}

void ENBPostProcessing::DrawSettings()
{
	GetEffectManager().RenderImGui();
}

void ENBPostProcessing::SetupResources()
{
	GetEffectManager().Initialize();
}

void ENBPostProcessing::Reset()
{
	// Reset effect state if needed
}

struct Main_HDRTonemapBlendCinematic_Render
{
	static void thunk(RE::ImageSpaceManager*, RE::ImageSpaceEffect*, uint32_t, uint32_t, RE::ImageSpaceShaderParam*)
	{
		auto renderer = globals::game::renderer;

		auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

		auto& imageSpaceTempCopy = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kIMAGESPACE_TEMP_COPY];
		auto& imageSpaceTempCopy2 = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kIMAGESPACE_TEMP_COPY2];

		globals::features::enbPostProcessing.GetEffectManager().ExecuteEffects(main, imageSpaceTempCopy, imageSpaceTempCopy2);

		globals::d3d::context->CopyResource(imageSpaceTempCopy2.texture, imageSpaceTempCopy.texture);
		//func(a1, a2, a3, a4, a5);
	}

	static inline REL::Relocation<decltype(thunk)> func;
};

void ENBPostProcessing::PostPostLoad()
{
	stl::write_thunk_call<Main_HDRTonemapBlendCinematic_Render>(REL::RelocationID(99023, 105674).address() + REL::Relocate(0x1EA, 0x178));
	if (REL::Module::IsSE())
		stl::write_thunk_call<Main_HDRTonemapBlendCinematic_Render>(REL::RelocationID(99023, 105674).address() + REL::Relocate(0x230, 0x178));
}
