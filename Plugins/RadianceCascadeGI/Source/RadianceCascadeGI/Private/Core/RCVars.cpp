
#include "RCVars.h"

namespace RC
{
		TAutoConsoleVariable<int32> CVarScreenspaceEnabled(
			TEXT("r.RC.ScreenSpaceEnabled"),
			1,
			TEXT("Enable Screen Space RC \n")
			TEXT(" 0: OFF;")
			TEXT(" 1: ON."),
			ECVF_RenderThreadSafe);
		TAutoConsoleVariable<int32> CVarDisplayCascade(
			TEXT("r.RC.DisplayCascade"),
			0,
			TEXT("Display a specific cascade \n"),
			ECVF_RenderThreadSafe);
		TAutoConsoleVariable<int32> CVarRayCount(
			TEXT("r.RC.RayCount"),
			4,
			TEXT("Raycount must be square i.e. 4, 16... \n"),
			ECVF_RenderThreadSafe);
		TAutoConsoleVariable<float> CVarIntervalMult(
			TEXT("r.RC.IntervalMult"),
			1,
			TEXT("Multiply intervals\n"),
			ECVF_RenderThreadSafe);
		TAutoConsoleVariable<int32> CVarTileSize(
			TEXT("r.RC.TileSize"),
			4,
			TEXT("Size of Cascade 0 tiles. i.e. base resolution \n"),
			ECVF_RenderThreadSafe);
		TAutoConsoleVariable<float> CVarWallThickness(
			TEXT("r.RC.WallThickness"),
			300,
			TEXT("Assumed depth of visible walls\n"),
			ECVF_RenderThreadSafe);
		TAutoConsoleVariable<float> CVarIntensity(
			TEXT("r.RC.Intensity"),
			1,
			TEXT("Multiplier of GI\n"),
			ECVF_RenderThreadSafe);
}