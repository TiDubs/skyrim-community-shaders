#pragma once

#include "EffectManager.h"
#include "ENBEffect.h"
#include "ENBBloom.h"

/**
 * @brief Registry for all available Effect11 implementations
 * 
 * This class handles registration of all available effect types
 * with the EffectManager singleton.
 */
class EffectRegistry
{
public:
    static void RegisterAllEffects();
    
private:
    EffectRegistry() = default;
};