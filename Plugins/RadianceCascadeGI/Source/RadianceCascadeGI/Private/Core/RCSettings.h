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
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UPROPERTY(EditAnywhere, config, Category = "Screen Space",
		meta = (ConsoleVariable = "r.RC.ScreenSpaceEnabled",
			DisplayName = "Enable Screen Space RC"))
	bool bScreenSpaceEnabled = true;

	UPROPERTY(EditAnywhere, config, Category = "Screen Space",
		meta = (ConsoleVariable = "r.RC.Intensity",
			DisplayName = "Intensity",
			ToolTip = "Multiplier of GI contribution.",
			ClampMin = "0.0", UIMin = "0.0", UIMax = "10.0",
			EditCondition = "bScreenSpaceEnabled"))
	float Intensity = 1.0f;

	UPROPERTY(EditAnywhere, config, Category = "Quality",
		meta = (ConsoleVariable = "r.RC.RayCount",
			DisplayName = "Ray Count",
			ToolTip = "Rays per probe in cascade 0. Must be a perfect square: 4, 16, 64.",
			ClampMin = "4", UIMin = "4", UIMax = "64",
			EditCondition = "bScreenSpaceEnabled"))
	int32 RayCount = 4;

	UPROPERTY(EditAnywhere, config, Category = "Quality",
		meta = (ConsoleVariable = "r.RC.TileSize",
			DisplayName = "Tile Size",
			ToolTip = "Size of cascade 0 tiles, i.e. base probe resolution in pixels.",
			ClampMin = "1", UIMin = "2", UIMax = "16",
			EditCondition = "bScreenSpaceEnabled"))
	int32 TileSize = 4;

	UPROPERTY(EditAnywhere, config, Category = "Quality",
		meta = (ConsoleVariable = "r.RC.WallThickness",
			DisplayName = "Wall Thickness",
			ToolTip = "Assumed depth behind visible walls.",
			ClampMin = "0.0", UIMin = "0.0", UIMax = "2000.0", Units = "cm",
			EditCondition = "bScreenSpaceEnabled"))
	float WallThickness = 300.0f;

	UPROPERTY(EditAnywhere, Transient, Category = "Debug",
		meta = (ConsoleVariable = "r.RC.DisplayCascade",
			DisplayName = "Display Cascade",
			ToolTip = "Display a specific cascade instead of the composited result.",
			ClampMin = "0", UIMin = "0", UIMax = "8",
			EditCondition = "bScreenSpaceEnabled"))
	int32 DisplayCascade = 0;

	UPROPERTY(EditAnywhere, Transient, Category = "Debug",
		meta = (ConsoleVariable = "r.RC.IntervalMult",
			DisplayName = "Interval Multiplier",
			ToolTip = "Multiply cascade interval lengths.",
			ClampMin = "1", UIMin = "1", UIMax = "8",
			EditCondition = "bScreenSpaceEnabled"))
	float IntervalMult = 1;

	virtual FName GetCategoryName() const { return FName("Plugins"); }
};