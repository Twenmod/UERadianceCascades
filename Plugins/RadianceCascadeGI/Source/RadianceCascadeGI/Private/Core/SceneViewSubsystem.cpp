#include "SceneViewSubsystem.h"

#include "RCVars.h"
#include "ScreenSpace/ScreenSpaceRCSceneExtension.h"
#include "SceneViewExtension.h"
#include "WorldSpace/WorldSpaceRCSceneExtension.h"

void USceneViewSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	//ScreenSpaceExtension = FSceneViewExtensions::NewExtension<FScreenSpaceRCSceneExtension>();
	WorldSpaceExtension = FSceneViewExtensions::NewExtension<FWorldSpaceRCSceneExtension>();
}

void USceneViewSubsystem::Deinitialize()
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
