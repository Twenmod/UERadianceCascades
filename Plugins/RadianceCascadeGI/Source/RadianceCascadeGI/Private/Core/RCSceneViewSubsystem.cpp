#include "RCSceneViewSubsystem.h"

#include "RCVars.h"
#include "ScreenSpace/ScreenSpaceRCSceneExtension.h"
#include "SceneViewExtension.h"
#include "WorldSpace/WorldSpaceRCGi.h"

void URCSceneViewSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	ScreenSpaceExtension = FSceneViewExtensions::NewExtension<FScreenSpaceRCSceneExtension>();
}

void URCSceneViewSubsystem::Deinitialize()
{
	if (ScreenSpaceExtension)
	{
		{
			ScreenSpaceExtension->IsActiveThisFrameFunctions.Empty();

			FSceneViewExtensionIsActiveFunctor IsActiveFunctor;

			IsActiveFunctor.IsActiveFunction = [](const ISceneViewExtension* SceneViewExtension, const FSceneViewExtensionContext& Context)
			{
				return TOptional<bool>(false);
			};
			ScreenSpaceExtension->IsActiveThisFrameFunctions.Add(IsActiveFunctor);
		}
		ScreenSpaceExtension.Reset();
		ScreenSpaceExtension = nullptr;
	}
}
