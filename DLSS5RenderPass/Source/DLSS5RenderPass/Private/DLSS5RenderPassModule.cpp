//  Copyright MonsterGuoGuo. All Rights Reserved.2026

#include "DLSS5RenderPassModule.h"

#define LOCTEXT_NAMESPACE "FDLSS5RenderPassModule"

void FDLSS5RenderPassModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
}

void FDLSS5RenderPassModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDLSS5RenderPassModule, DLSS5RenderPass)
