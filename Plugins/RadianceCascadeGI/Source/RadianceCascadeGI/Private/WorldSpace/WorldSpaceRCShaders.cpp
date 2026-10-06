#include "WorldSpaceRCShaders.h"

IMPLEMENT_GLOBAL_SHADER(FWorldSpaceRCGather, "/Plugins/RadianceCascadeGI/WorldSpace/WorldSpaceProbeGather.usf", "MainCS",
	SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FWorldSpaceRCFillCascade, "/Plugins/RadianceCascadeGI/WorldSpace/WorldSpaceFillCascade.usf", "MainCS",
	SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FWorldSpaceRCRayBin, "/Plugins/RadianceCascadeGI/WorldSpace/WorldSpaceRayBin.usf", "MainRG", SF_RayGen);
IMPLEMENT_GLOBAL_SHADER(FWorldSpaceRCMerging, "/Plugins/RadianceCascadeGI/WorldSpace/WorldSpaceMerging.usf", "MainCS",
	SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FWorldSpaceRCApply, "/Plugins/RadianceCascadeGI/WorldSpace/WorldSpaceApply.usf", "MainCS",
	SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FWorldSpaceRCBuildFreeList, "/Plugins/RadianceCascadeGI/WorldSpace/WorldSpaceBuildFreelist.usf", "MainCS",
	SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FWorldSpaceRCClearFreeList, "/Plugins/RadianceCascadeGI/WorldSpace/WorldSpaceClearFreelist.usf", "MainCS",
	SF_Compute);