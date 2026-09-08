#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "RCSettings.generated.h"

UCLASS(config = Engine, defaultconfig, meta = (DisplayName = "Radiance Cascades"))
class RADIANCECASCADEGI_API URCSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	URCSettings(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, config, Category = "Screen Space",
		meta = (ConsoleVariable = "r.RCScreenSpaceEnabled", DisplayName = "Enable Screen Space RC"))
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, config, Category = "Screen Space",
		meta = (ConsoleVariable = "	r.RCDisplayCascade", DisplayName = "Display Cascade"))
	int DisplayCascade = 0;

	virtual FName GetCategoryName() const { return FName("Plugins"); }
};