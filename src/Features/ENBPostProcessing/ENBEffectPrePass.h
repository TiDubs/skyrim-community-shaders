#pragma once

#include "Effect.h"

class ENBEffectPrePass : public Effect
{
public:
	virtual std::string GetName() const override { return "enbeffectprepass.fx"; }

	virtual void Execute() override;

	virtual void UpdateEffectVariables() override;
};