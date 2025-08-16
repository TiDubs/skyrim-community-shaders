#include "EffectRegistry.h"

void EffectRegistry::RegisterAllEffects()
{
    auto& manager = EffectManager::GetSingleton();
    
    manager.RegisterEffect<ENBBloom>("enbbloom");
    manager.RegisterEffect<ENBEffect>("enbeffect");
}