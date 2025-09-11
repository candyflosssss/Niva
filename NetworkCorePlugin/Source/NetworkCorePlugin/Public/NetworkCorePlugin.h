#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "civetweb.h"    // CivetWeb 的 C API

class FNetworkCorePluginModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

};
