#include "ENBBloom.h"
#include <imgui.h>

void ENBBloom::Execute(RE::BSGraphics::RenderTargetData& input,
	RE::BSGraphics::RenderTargetData& swap,
	RE::BSGraphics::RenderTargetData& output)
{
	ExecuteTechniqueSequence(GetSelectedTechnique(), input, swap, output);
}

void ENBBloom::UpdateEffectVariables()
{
}