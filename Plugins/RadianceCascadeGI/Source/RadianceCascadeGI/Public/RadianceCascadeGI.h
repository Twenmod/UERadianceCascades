// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDirectoryWatcher.h"
#include "Modules/ModuleManager.h"
#include "Modules/ModuleInterface.h"
#include "Interfaces/IPluginManager.h"

class RADIANCECASCADEGI_API FRadianceCascadeGIModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;


	//hot reload
	void OnShaderDirChanged(const TArray<FFileChangeData>& Changes);

	FDelegateHandle WatcherHandle;
	FString WatchedShaderDir;
	bool bRecompilePending = false;
};
