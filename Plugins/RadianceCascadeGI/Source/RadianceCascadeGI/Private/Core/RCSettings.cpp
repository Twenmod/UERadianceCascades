#include "RCSettings.h"

#include "Misc/ConfigUtilities.h"

URCSettings::URCSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("Radiance Cascades");

#if WITH_EDITOR
	SetSPPreset(ScreenSpaceQualityPreset);
	SetWPPreset(WorldSpaceQualityPreset);
#endif
}

#if WITH_EDITOR
void URCSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FProperty* Property = PropertyChangedEvent.Property;
	if (!Property)
	{
		return;
	}

	//Quality presets
	auto PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(URCSettings, ScreenSpaceQualityPreset))
	{
		SetSPPreset(ScreenSpaceQualityPreset);
	}else if (PropertyName == GET_MEMBER_NAME_CHECKED(URCSettings, WorldSpaceQualityPreset))
	{
		SetWPPreset(WorldSpaceQualityPreset);
	}
	else if (IsPresetVarSP(PropertyName))
	{
		ScreenSpaceQualityPreset = ERCScreenSpaceQualityPreset::Custom;
	}else if (IsPresetVarWP(PropertyName))
	{
		WorldSpaceQualityPreset = ERCWorldSpaceQualityPreset::Custom;
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);

	UpdateConsole(*Property);


}
void URCSettings::SetSPPreset(ERCScreenSpaceQualityPreset Preset)
{
	switch (Preset)
	{
	case ERCScreenSpaceQualityPreset::Low:
		RayCount = 4;
		TileSize = 4;
		break;
	case ERCScreenSpaceQualityPreset::Medium:
		RayCount = 16;
		TileSize = 4;
		break;
	case ERCScreenSpaceQualityPreset::High:
		RayCount = 16;
		TileSize = 2;
		break;
	case ERCScreenSpaceQualityPreset::Custom:
		break;
	}
	//Update all Cvars in the class
	for (TFieldIterator<FProperty> It(GetClass()); It; ++It)
	{
		UpdateConsole(**It);
	}
}
void URCSettings::SetWPPreset(ERCWorldSpaceQualityPreset Preset)
{
	switch (Preset)
	{
		case ERCWorldSpaceQualityPreset::Low:
			WsProbeSize = 24;
			WsProbeHalfDist = 200;
			break;
		case ERCWorldSpaceQualityPreset::Medium:
			WsProbeSize = 12;
			WsProbeHalfDist = 250;
			break;
		case ERCWorldSpaceQualityPreset::High:
			WsProbeSize = 8;
			WsProbeHalfDist = 300;
			break;
		case ERCWorldSpaceQualityPreset::Custom:
			break;
	}
	//Update all Cvars in the class
	for (TFieldIterator<FProperty> It(GetClass()); It; ++It)
	{
		UpdateConsole(**It);
	}
}

bool URCSettings::IsPresetVarSP(const FName & PropertyName)
{
	return PropertyName == GET_MEMBER_NAME_CHECKED(URCSettings, RayCount) || PropertyName == GET_MEMBER_NAME_CHECKED(URCSettings, TileSize);
}

bool URCSettings::IsPresetVarWP(const FName& PropertyName)
{
	return PropertyName == GET_MEMBER_NAME_CHECKED(URCSettings, WsProbeSize) || PropertyName == GET_MEMBER_NAME_CHECKED(URCSettings, WsProbeHalfDist);
}
void URCSettings::UpdateConsole(FProperty& Property)
{
	//Update console explicitly
	const FString CVarName = Property.GetMetaData(TEXT("ConsoleVariable"));
	if (CVarName.IsEmpty())
	{
		return;
	}

	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*CVarName);
	if (!CVar)
	{
		UE_LOG(LogTemp, Warning, TEXT("RCSettings: %s -> unknown CVar '%s'"),
			*Property.GetName(), *CVarName);
		return;
	}

	if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(&Property))
	{
		CVar->Set(BoolProp->GetPropertyValue_InContainer(this) ? 1 : 0, ECVF_SetByProjectSetting);
	}
	else if (const FIntProperty* IntProp = CastField<FIntProperty>(&Property))
	{
		CVar->Set(IntProp->GetPropertyValue_InContainer(this), ECVF_SetByProjectSetting);
	}
	else if (const FFloatProperty* FloatProp = CastField<FFloatProperty>(&Property))
	{
		CVar->Set(FloatProp->GetPropertyValue_InContainer(this), ECVF_SetByProjectSetting);
	}
}
#endif