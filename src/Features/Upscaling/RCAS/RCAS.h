#pragma once

#include "../../../Buffer.h"
#include "../../../State.h"

#include <d3d11_4.h>
#include <winrt/base.h>

/**
 * @brief Standalone RCAS (Robust Contrast Adaptive Sharpening) implementation
 *
 * This class provides standalone RCAS sharpening functionality based on AMD FidelityFX.
 * RCAS is designed to be applied after upscaling for enhanced image clarity.
 */
class RCAS
{
public:
	RCAS() = default;

	// RCAS resources
	winrt::com_ptr<ID3D11ComputeShader> rcasComputeShader;

	ConstantBuffer* rcasConfigCB = nullptr;

	/**
	 * @brief Initialize the standalone RCAS implementation
	 */
	void Initialize();

	/**
	 * @brief Apply RCAS sharpening to a texture
	 * @param inputTexture Input texture to sharpen
	 * @param outputTexture Output texture for sharpened result
	 * @param sharpness Sharpening strength (0.0 to 1.0)
	 */
	void ApplySharpen(ID3D11ShaderResourceView* inputTexture, ID3D11UnorderedAccessView* outputUAV, float sharpness = 0.15f);

private:
	/**
	 * @brief Compile RCAS compute shader
	 */
	void CreateComputeShader();
};