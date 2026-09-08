#include "RCSettings.h"
URCSettings::URCSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("Radiance Cascades");
}