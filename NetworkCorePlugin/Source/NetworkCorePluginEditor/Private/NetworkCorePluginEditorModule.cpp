#include "Modules/ModuleManager.h"

class FNetworkCorePluginEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override {}
	virtual void ShutdownModule() override {}
};

IMPLEMENT_MODULE(FNetworkCorePluginEditorModule, NetworkCorePluginEditor)
