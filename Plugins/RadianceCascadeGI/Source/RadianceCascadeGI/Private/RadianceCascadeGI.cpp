// Copyright Epic Games, Inc. All Rights Reserved.

#include "RadianceCascadeGI.h"

#define LOCTEXT_NAMESPACE "FRadianceCascadeGIModule"

void FRadianceCascadeGIModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module

	// Set up the Shader Directories
	FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("RadianceCascadeGI"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugins/SceneViewExtensionTemplate"), PluginShaderDir);
}

void FRadianceCascadeGIModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRadianceCascadeGIModule, RadianceCascadeGI)