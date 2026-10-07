#pragma once
#include "HAL/IConsoleManager.h"

namespace RC
{
	namespace WorldSpace
	{
		extern RADIANCECASCADEGI_API TAutoConsoleVariable<int32> CVarEnabled;
		extern RADIANCECASCADEGI_API TAutoConsoleVariable<int32> CVarDisplayCascade;
		extern RADIANCECASCADEGI_API TAutoConsoleVariable<float> CVarNormalOffset;
		extern RADIANCECASCADEGI_API TAutoConsoleVariable<int32> CVarDisplayProbes;
		extern RADIANCECASCADEGI_API TAutoConsoleVariable<float> CVarIntensity;
		extern RADIANCECASCADEGI_API TAutoConsoleVariable<int32> CVarRayCount;
		extern RADIANCECASCADEGI_API TAutoConsoleVariable<float> CVarProbeSize;
		extern RADIANCECASCADEGI_API TAutoConsoleVariable<float> CVarProbeHalfDistance;
		extern RADIANCECASCADEGI_API TAutoConsoleVariable<float> CVarHistoryConservation;
	}
	namespace ScreenSpace
	{
		extern RADIANCECASCADEGI_API TAutoConsoleVariable<int32> CVarEnabled;
		extern RADIANCECASCADEGI_API TAutoConsoleVariable<int32> CVarDisplayCascade;
		extern RADIANCECASCADEGI_API TAutoConsoleVariable<int32> CVarRayCount;
		extern RADIANCECASCADEGI_API TAutoConsoleVariable<int32> CVarTileSize;
		extern RADIANCECASCADEGI_API TAutoConsoleVariable<float> CVarIntervalMult;
		extern RADIANCECASCADEGI_API TAutoConsoleVariable<float> CVarWallThickness;
		extern RADIANCECASCADEGI_API TAutoConsoleVariable<float> CVarIntensity;
	}
	}
