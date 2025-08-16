#pragma once

#include "Effect11.h"
#include <memory>
#include <unordered_map>
#include <vector>
#include <string>
#include <functional>
#include <filesystem>
#include <d3d11.h>
#include <Effects11/d3dx11effect.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

class EffectManager
{
public:
    static EffectManager& GetSingleton();

    // Effect registration and management
    template<typename T>
    void RegisterEffect(const std::string& name);
    
    bool LoadEffect(const std::string& name, const std::filesystem::path& filePath);
    void UnloadEffect(const std::string& name);
    void UnloadAllEffects();

    // Effect execution
    void ExecuteEffect(const std::string& name, RE::BSGraphics::RenderTargetData& input, 
                      RE::BSGraphics::RenderTargetData& swap, RE::BSGraphics::RenderTargetData& output);
    void ExecuteAllEffects(RE::BSGraphics::RenderTargetData& input, 
                          RE::BSGraphics::RenderTargetData& swap, RE::BSGraphics::RenderTargetData& output);

    // Effect access
    Effect11* GetEffect(const std::string& name);
    std::vector<std::string> GetLoadedEffectNames() const;
    
    // UI Integration
    void RenderImGui();
    
    // Lifecycle
    void Initialize();
    void Reset();
    
    // Shared resource access
    ID3D11Buffer* GetSharedQuadVertexBuffer() const { return sharedQuadVertexBuffer.Get(); }
    ID3D11InputLayout* GetSharedInputLayout() const { return sharedInputLayout.Get(); }
    ID3D11RasterizerState* GetSharedRasterizerState() const { return sharedRasterizerState.Get(); }
    ID3D11BlendState* GetSharedBlendState() const { return sharedBlendState.Get(); }
    
    // Common variable management
    void UpdateAllCommonVariables();
    void UpdateCommonVariablesForEffect(ID3DX11Effect* effect);

    struct EffectEntry {
        std::unique_ptr<Effect11> effect;
        std::string type;
        bool isLoaded = false;
        bool isEnabled = true;
    };

    std::unordered_map<std::string, EffectEntry> effects;
    
    // Factory function type for creating effects
    using EffectFactory = std::function<std::unique_ptr<Effect11>()>;
    std::unordered_map<std::string, EffectFactory> factories;

    // Common resources shared across effects
    void InitializeSharedResources();
    void CleanupSharedResources();
    
    // Shared D3D resources
    ComPtr<ID3D11Buffer> sharedQuadVertexBuffer;
    ComPtr<ID3D11InputLayout> sharedInputLayout;
    ComPtr<ID3D11RasterizerState> sharedRasterizerState;
    ComPtr<ID3D11BlendState> sharedBlendState;
    
    void CreateSharedQuadGeometry();
    void CreateSharedRenderStates();
    
    // Common textures and variables
    struct Texture {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11RenderTargetView> rtv;
        ComPtr<ID3D11ShaderResourceView> srv;
    };
    
    std::unordered_map<std::string, Texture> commonTextureCache;
    
    // Common variable data (updated once, applied to all effects)
    struct CommonVariableData {
        float timer[4];
        float screenSize[4];
        float weather[4];
        float timeOfDay1[4];
        float timeOfDay2[4];
        float eNightDayFactor;
        float eInteriorFactor;
    } commonData;
    
    void SetupCommonTextures();
    void UpdateCommonData();
};

// Template implementation
template<typename T>
void EffectManager::RegisterEffect(const std::string& name)
{
    static_assert(std::is_base_of_v<Effect11, T>, "T must derive from Effect11");
    
    factories[name] = []() -> std::unique_ptr<Effect11> {
        return std::make_unique<T>();
    };
}