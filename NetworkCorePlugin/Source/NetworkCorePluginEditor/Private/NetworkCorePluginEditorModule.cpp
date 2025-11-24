#include "Modules/ModuleManager.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#include "ComponentVisualizer.h"

#include "McpTwoPointComponentVisualizer.h"
#include "McpTwoPointComponent.h"
#include "McpSitComponentVisualizer.h"
#include "McpSitComponent.h"

class FNetworkCorePluginEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		if (GUnrealEd)
		{
			// TwoPoint visualizer
			{
				TSharedPtr<FComponentVisualizer> Visualizer = MakeShared<FMcpTwoPointComponentVisualizer>();
				GUnrealEd->RegisterComponentVisualizer(UMcpTwoPointComponent::StaticClass()->GetFName(), Visualizer);
				Visualizer->OnRegister();
			}

			// Sit visualizer
			{
				TSharedPtr<FComponentVisualizer> Visualizer = MakeShared<FMcpSitComponentVisualizer>();
				GUnrealEd->RegisterComponentVisualizer(UMcpSitComponent::StaticClass()->GetFName(), Visualizer);
				Visualizer->OnRegister();
			}
		}
	}

	virtual void ShutdownModule() override
	{
		if (GUnrealEd)
		{
			GUnrealEd->UnregisterComponentVisualizer(UMcpTwoPointComponent::StaticClass()->GetFName());
			GUnrealEd->UnregisterComponentVisualizer(UMcpSitComponent::StaticClass()->GetFName());
		}
	}
};

IMPLEMENT_MODULE(FNetworkCorePluginEditorModule, NetworkCorePluginEditor)
