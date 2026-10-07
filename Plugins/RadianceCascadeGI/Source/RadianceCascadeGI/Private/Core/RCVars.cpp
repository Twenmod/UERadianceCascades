
#include "RCVars.h"

namespace RC
{
	namespace WorldSpace
	{
		
	}
	namespace WorldSpace
	{

		TAutoConsoleVariable<int32> CVarEnabled(
			TEXT("r.RC.WorldSpace.Enabled"),
			1,
			TEXT("Enable World Space RC (only if GI is set to plugin)\n")
			TEXT(" 0: OFF;")
			TEXT(" 1: ON."),
			ECVF_RenderThreadSafe);
		TAutoConsoleVariable<int32> CVarDisplayCascade(
			TEXT("r.RC.WorldSpace.DisplayCascade"),
			0,
			TEXT("Display from a specific cascade \n"),
			ECVF_RenderThreadSafe);
		TAutoConsoleVariable<int32> CVarDisplayProbes(
			TEXT("r.RC.WorldSpace.DisplayProbes"),
			0,
			TEXT("Display world space probes\n")
			TEXT(" 0: OFF;")
			TEXT(" 1: ON."),
			ECVF_RenderThreadSafe);
		TAutoConsoleVariable<float> CVarNormalOffset(
			TEXT("r.RC.WorldSpace.NormalOffset"),
			1.0,
			TEXT("Ray normal offset\n"),
			ECVF_RenderThreadSafe);
		TAutoConsoleVariable<float> CVarIntensity(
			TEXT("r.RC.WorldSpace.Intensity"),
			1,
			TEXT("Multiplier of GI\n"),
			ECVF_RenderThreadSafe);
		TAutoConsoleVariable<float> CVarProbeSize(
			TEXT("r.RC.WorldSpace.ProbeSize"),
			12.0,
			TEXT("Size in CM of probes at cascade 0\n"),
			ECVF_RenderThreadSafe);
		TAutoConsoleVariable<float> CVarProbeHalfDistance(
			TEXT("r.RC.WorldSpace.ProbeHalfDistance"),
			250.0,
			TEXT("Size in CM of probes at cascade 0\n"),
			ECVF_RenderThreadSafe);
		TAutoConsoleVariable<float> CVarHistoryConservation(
			TEXT("r.RC.WorldSpace.HistoryConservation"),
			0.9,
			TEXT("How much history to preserve during temporal accumulation, higher values take longer to update but have less flickering\n"),
			ECVF_RenderThreadSafe);

	}
	namespace ScreenSpace
	{
		TAutoConsoleVariable<int32> CVarEnabled(
			TEXT("r.RC.ScreenSpace.Enabled"),
			0,
			TEXT("Enable Screen Space RC \n")
			TEXT(" 0: OFF;")
			TEXT(" 1: ON."),
			ECVF_RenderThreadSafe);

		TAutoConsoleVariable<int32> CVarDisplayCascade(
			TEXT("r.RC.ScreenSpace.DisplayCascade"),
			0,
			TEXT("Display a specific cascade \n"),
			ECVF_RenderThreadSafe);

		TAutoConsoleVariable<int32> CVarRayCount(
			TEXT("r.RC.ScreenSpace.RayCount"),
			4,
			TEXT("Raycount must be square i.e. 4, 16... \n"),
			ECVF_RenderThreadSafe);
		TAutoConsoleVariable<float> CVarIntervalMult(
			TEXT("r.RC.ScreenSpace.IntervalMult"),
			1,
			TEXT("Multiply intervals\n"),
			ECVF_RenderThreadSafe);
		TAutoConsoleVariable<int32> CVarTileSize(
			TEXT("r.RC.ScreenSpace.TileSize"),
			4,
			TEXT("Size of Cascade 0 tiles. i.e. base resolution \n"),
			ECVF_RenderThreadSafe);
		TAutoConsoleVariable<float> CVarWallThickness(
			TEXT("r.RC.ScreenSpace.WallThickness"),
			300,
			TEXT("Assumed depth of visible walls\n"),
			ECVF_RenderThreadSafe);
		TAutoConsoleVariable<float> CVarIntensity(
			TEXT("r.RC.ScreenSpace.Intensity"),
			1,
			TEXT("Multiplier of GI\n"),
			ECVF_RenderThreadSafe);
	}
}