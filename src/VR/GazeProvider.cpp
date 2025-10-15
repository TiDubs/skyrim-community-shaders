#include "VR/GazeProvider.h"

namespace GazeProvider
{
    GazeUV GetCurrentGazeUV()
    {
        GazeUV gaze{};

        // TODO: Integrate Varjo/OpenXR gaze runtime to populate live data.
        gaze.hasGaze = false;
        gaze.leftUV = { 0.5f, 0.5f };
        gaze.rightUV = { 0.5f, 0.5f };

        return gaze;
    }
}
