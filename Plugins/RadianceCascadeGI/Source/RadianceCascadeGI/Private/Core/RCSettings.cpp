#include "RCSettings.h"

#include "Misc/ConfigUtilities.h"

URCSettings::URCSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("Radiance Cascades");
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

	const FString CVarName = Property->GetMetaData(TEXT("ConsoleVariable"));
	if (CVarName.IsEmpty())
	{
		return;
	}

	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*CVarName);
	if (!CVar)
	{
		UE_LOG(LogTemp, Warning, TEXT("RCSettings: %s -> unknown CVar '%s'"),
			*Property->GetName(), *CVarName);
		return;
	}

	if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
	{
		CVar->Set(BoolProp->GetPropertyValue_InContainer(this) ? 1 : 0, ECVF_SetByProjectSetting);
	}
	else if (const FIntProperty* IntProp = CastField<FIntProperty>(Property))
	{
		CVar->Set(IntProp->GetPropertyValue_InContainer(this), ECVF_SetByProjectSetting);
	}
	else if (const FFloatProperty* FloatProp = CastField<FFloatProperty>(Property))
	{
		CVar->Set(FloatProp->GetPropertyValue_InContainer(this), ECVF_SetByProjectSetting);
	}
}
#endif