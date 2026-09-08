//  Copyright MonsterGuoGuo. All Rights Reserved.2026

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FDLSS5RenderPassModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
