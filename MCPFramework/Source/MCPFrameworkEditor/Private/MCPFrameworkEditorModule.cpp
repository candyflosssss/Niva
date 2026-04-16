#include "Modules/ModuleManager.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#include "ComponentVisualizer.h"
#include "EdGraphUtilities.h"

#include "McpTwoPointComponentVisualizer.h"
#include "Components/Spatial/McpTwoPointComponent.h"
#include "McpSitComponentVisualizer.h"
#include "Components/Interaction/McpSitComponent.h"
#include "K2Node_MCPAutoRegister.h"
#include "SGraphNode_MCPAutoRegister.h"

// Maps UK2Node_MCPAutoRegister → SGraphNode_MCPAutoRegister (shows combo box on node)
class FMCPAutoRegisterNodeFactory : public FGraphPanelNodeFactory
{
	virtual TSharedPtr<SGraphNode> CreateNode(UEdGraphNode* InNode) const override
	{
		if (UK2Node_MCPAutoRegister* McpNode = Cast<UK2Node_MCPAutoRegister>(InNode))
		{
			return SNew(SGraphNode_MCPAutoRegister, McpNode);
		}
		return nullptr;
	}
};

class FMCPFrameworkEditorModule : public IModuleInterface
{
	TSharedPtr<FMCPAutoRegisterNodeFactory> NodeFactory;

public:
	virtual void StartupModule() override
	{
		if (GUnrealEd)
		{
			{
				TSharedPtr<FComponentVisualizer> Visualizer = MakeShared<FMcpTwoPointComponentVisualizer>();
				GUnrealEd->RegisterComponentVisualizer(UMcpTwoPointComponent::StaticClass()->GetFName(), Visualizer);
				Visualizer->OnRegister();
			}

			{
				TSharedPtr<FComponentVisualizer> Visualizer = MakeShared<FMcpSitComponentVisualizer>();
				GUnrealEd->RegisterComponentVisualizer(UMcpSitComponent::StaticClass()->GetFName(), Visualizer);
				Visualizer->OnRegister();
			}
		}

		NodeFactory = MakeShared<FMCPAutoRegisterNodeFactory>();
		FEdGraphUtilities::RegisterVisualNodeFactory(NodeFactory);
	}

	virtual void ShutdownModule() override
	{
		if (NodeFactory.IsValid())
		{
			FEdGraphUtilities::UnregisterVisualNodeFactory(NodeFactory);
			NodeFactory.Reset();
		}

		if (GUnrealEd)
		{
			GUnrealEd->UnregisterComponentVisualizer(UMcpTwoPointComponent::StaticClass()->GetFName());
			GUnrealEd->UnregisterComponentVisualizer(UMcpSitComponent::StaticClass()->GetFName());
		}
	}
};

IMPLEMENT_MODULE(FMCPFrameworkEditorModule, MCPFrameworkEditor)

