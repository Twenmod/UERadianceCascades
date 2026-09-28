#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "RCSceneViewSubsystem.generated.h"

UCLASS()
class URCSceneViewSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()
public:

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	TSharedPtr<class FScreenSpaceRCSceneExtension, ESPMode::ThreadSafe> ScreenSpaceExtension;
	TSharedPtr<class FWorldSpaceRCGi, ESPMode::ThreadSafe> WorldSpaceExtension;
};