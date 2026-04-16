// Custom SGraphNode that shows a function-selection combo box directly on the node body
#pragma once

#include "CoreMinimal.h"
#include "KismetNodes/SGraphNodeK2Default.h"

class UK2Node_MCPAutoRegister;

class SGraphNode_MCPAutoRegister : public SGraphNodeK2Default
{
public:
    SLATE_BEGIN_ARGS(SGraphNode_MCPAutoRegister) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UK2Node_MCPAutoRegister* InNode);

protected:
    virtual void CreateBelowPinControls(TSharedPtr<SVerticalBox> MainBox) override;

private:
    void RebuildFunctionOptions();
    void OnFunctionSelected(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);

    TWeakObjectPtr<UK2Node_MCPAutoRegister> McpNode;
    TArray<TSharedPtr<FString>> FunctionOptions;
    TSharedPtr<FString> SelectedOption;
};
