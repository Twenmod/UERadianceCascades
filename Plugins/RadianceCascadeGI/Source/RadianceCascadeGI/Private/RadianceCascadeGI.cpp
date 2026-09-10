// Copyright Epic Games, Inc. All Rights Reserved.

#include "RadianceCascadeGI.h"

#include "DirectoryWatcherModule.h"

#define LOCTEXT_NAMESPACE "FRadianceCascadeGIModule"

void FRadianceCascadeGIModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module

	// Set up the Shader Directories
	FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("RadianceCascadeGI"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugins/SceneViewExtensionTemplate"), PluginShaderDir);






	//Hot reload debugging todo: remove
#if WITH_EDITOR
	WatchedShaderDir = PluginShaderDir;

	FDirectoryWatcherModule& DWModule =
		FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>("DirectoryWatcher");

	DWModule.Get()->RegisterDirectoryChangedCallback_Handle(
		WatchedShaderDir,
		IDirectoryWatcher::FDirectoryChanged::CreateRaw(this, &FRadianceCascadeGIModule::OnShaderDirChanged),
		WatcherHandle,
		IDirectoryWatcher::WatchOptions::IncludeDirectoryChanges);
#endif
}

void FRadianceCascadeGIModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

#if WITH_EDITOR
	if (WatcherHandle.IsValid())
	{
		if (FDirectoryWatcherModule* DWModule =
			FModuleManager::GetModulePtr<FDirectoryWatcherModule>("DirectoryWatcher"))
		{
			if (IDirectoryWatcher* DW = DWModule->Get())
			{
				DW->UnregisterDirectoryChangedCallback_Handle(WatchedShaderDir, WatcherHandle);
			}
		}
		WatcherHandle.Reset();
	}
#endif
}

//Temp todo:remove
void FRadianceCascadeGIModule::OnShaderDirChanged(const TArray<FFileChangeData>& Changes)
{
	const bool bShaderTouched = Changes.ContainsByPredicate([](const FFileChangeData& C)
		{
			return C.Filename.EndsWith(TEXT(".usf")) || C.Filename.EndsWith(TEXT(".ush"));
		});

	if (!bShaderTouched || bRecompilePending)
	{
		return;
	}

	bRecompilePending = true;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float)
		{
			bRecompilePending = false;
			GEngine->Exec(nullptr, TEXT("recompileshaders changed"));
			return false;
		}), 0.5f);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRadianceCascadeGIModule, RadianceCascadeGI)