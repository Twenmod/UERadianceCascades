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
		meta = (ConsoleVariable = "r.RC.ScreenSpace.Enabled",
			DisplayName = "Enable Screen Space RC"))
	bool bScreenSpaceEnabled = true;

	UPROPERTY(EditAnywhere, config, Category = "Screen Space",
		meta = (ConsoleVariable = "r.RC.ScreenSpace.Intensity",
			DisplayName = "Intensity",
			ToolTip = "Multiplier of GI contribution.",
			ClampMin = "0.0", UIMin = "0.0", UIMax = "10.0",
			EditCondition = "bScreenSpaceEnabled"))
	float Intensity = 1.0f;

	UPROPERTY(EditAnywhere, config, Category = "Screen Space|Quality",
		meta = (ConsoleVariable = "r.RC.ScreenSpace.RayCount",
			DisplayName = "Ray Count",
			ToolTip = "Rays per probe in cascade 0. Must be a perfect square: 4, 16, 64.",
			ClampMin = "4", UIMin = "4", UIMax = "64",
			EditCondition = "bScreenSpaceEnabled"))
	int32 RayCount = 4;

	UPROPERTY(EditAnywhere, config, Category = "Screen Space|Quality",
		meta = (ConsoleVariable = "r.RC.ScreenSpace.TileSize",
			DisplayName = "Tile Size",
			ToolTip = "Size of cascade 0 tiles, i.e. base probe resolution in pixels.",
			ClampMin = "1", UIMin = "2", UIMax = "16",
			EditCondition = "bScreenSpaceEnabled"))
	int32 TileSize = 4;

	UPROPERTY(EditAnywhere, config, Category = "Screen Space|Quality",
		meta = (ConsoleVariable = "r.RC.ScreenSpace.WallThickness",
			DisplayName = "Wall Thickness",
			ToolTip = "Assumed depth behind visible walls.",
			ClampMin = "0.0", UIMin = "0.0", UIMax = "2000.0", Units = "cm",
			EditCondition = "bScreenSpaceEnabled"))
	float WallThickness = 300.0f;

	UPROPERTY(EditAnywhere, Transient, Category = "Screen Space|Debug",
		meta = (ConsoleVariable = "r.RC.ScreenSpace.DisplayCascade",
			DisplayName = "Display Cascade",
			ToolTip = "Display a specific cascade instead of the composited result.",
			ClampMin = "0", UIMin = "0", UIMax = "8",
			EditCondition = "bScreenSpaceEnabled"))
	int32 DisplayCascade = 0;

	UPROPERTY(EditAnywhere, Transient, Category = "Screen Space|Debug",
		meta = (ConsoleVariable = "r.RC.ScreenSpace.IntervalMult",
			DisplayName = "Interval Multiplier",
			ToolTip = "Multiply cascade interval lengths.",
			ClampMin = "1", UIMin = "1", UIMax = "8",
			EditCondition = "bScreenSpaceEnabled"))
	float IntervalMult = 1;

	//Worldspace
	UPROPERTY(EditAnywhere, config, Category = "World Space",
		meta = (ConsoleVariable = "r.RC.WorldSpace.Enabled",
			DisplayName = "Enabled",
			Tooltip = "Enable World Space RC"))
	bool bWorldSpaceEnabled = true;

	UPROPERTY(EditAnywhere, config, Category = "World Space",
		meta = (ConsoleVariable = "r.RC.WorldSpace.Intensity", DisplayName = "Intensity",
			Tooltip = "Multiply output result", ClampMin = "0", UIMin = "0", UIMax = "1"
		))

	float WsIntensity = 1.0;
	UPROPERTY(EditAnywhere, config, Category = "World Space",
		meta = (ConsoleVariable = "r.RC.WorldSpace.NormalOffset",
			DisplayName = "Normal offset",
			Tooltip = "Offset rays from normal, avoids shadow acne", ClampMin = "0", UIMin = "0", UIMax = "10"
		))
	float WsNormalOffset = 1.0;
	UPROPERTY(EditAnywhere, config, Category = "World Space|Quality",
		meta = (ConsoleVariable = "r.RC.WorldSpace.ProbeSize",
			DisplayName = "Probe size",
			Tooltip = "Probe size in CM at LOD 0", ClampMin = "1", UIMin = "4", UIMax = "32"))
	float WsProbeSize = 8.0;
	UPROPERTY(EditAnywhere, config, Category = "World Space|Quality",
		meta = (ConsoleVariable = "r.RC.WorldSpace.ProbeHalfDistance",
			DisplayName = "Probe half distance",
			Tooltip = "Doubles probe size every X CM", ClampMin = "1", UIMin = "50", UIMax = "400"))
	float WsProbeHalfDist = 250.0;
	UPROPERTY(EditAnywhere, config, Category = "World Space|Debug",
		meta = (ConsoleVariable = "r.RC.WorldSpace.DisplayCascade",
			DisplayName = "Display from cascade",
			Tooltip = "Display from a specific cascade", ClampMin = "0", ClampMax = "3", UIMin = "0", UIMax = "3"))
	int32 WsDisplayCascade = 0;
	UPROPERTY(EditAnywhere, config, Category = "World Space|Debug",
		meta = (ConsoleVariable = "r.RC.WorldSpace.DisplayProbes",
			DisplayName = "Display probes"))
	bool bWorldSpaceProbeDebug = false;

	virtual FName GetCategoryName() const override { return FName("Plugins"); }
};
