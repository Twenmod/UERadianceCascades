// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#if WITH_EDITOR
#include "IDirectoryWatcher.h"
#endif
#include "Modules/ModuleManager.h"
#include "Modules/ModuleInterface.h"
#include "Interfaces/IPluginManager.h"
#include "WorldSpace/WorldSpaceRCGi.h"

class RADIANCECASCADEGI_API FRadianceCascadeGIModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;


	//hot reload
#if WITH_EDITOR
	void OnShaderDirChanged(const TArray<FFileChangeData>& Changes);

	FWorldSpaceRCGi WorldSpaceRC;
	FDelegateHandle WatcherHandle;
	FDelegateHandle GIPassHandle;
	FString WatchedShaderDir;
	bool bRecompilePending = false;
#endif
};
