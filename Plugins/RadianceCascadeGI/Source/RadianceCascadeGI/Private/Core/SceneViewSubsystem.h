#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "SceneViewSubsystem.generated.h"

 UCLASS()
class USceneViewSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()
public:

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	TSharedPtr<class FScreenSpaceRCSceneExtension, ESPMode::ThreadSafe> ScreenSpaceExtension;
	//TSharedPtr<class FScreenSpaceRCSceneExtension, ESPMode::ThreadSafe> WorldSpaceExtension;
};