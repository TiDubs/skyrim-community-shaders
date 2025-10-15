#pragma once

#include <DirectXMath.h>

namespace GazeProvider
{
    struct GazeUV
    {
        bool hasGaze = false;
        DirectX::XMFLOAT2 leftUV{ 0.5f, 0.5f };
        DirectX::XMFLOAT2 rightUV{ 0.5f, 0.5f };
    };

    GazeUV GetCurrentGazeUV();
}
