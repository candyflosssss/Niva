// K2Node: Register a single UFUNCTION as MCP Tool with inline property configuration
#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "K2Node_MCPAutoRegister.generated.h"

/**
 * Blueprint node that registers a single UFUNCTION as an MCP tool.
 *
 * Usage:
 * 1. Select a function from the Details panel (must have exactly one UMCPToolHandle* param).
 * 2. The node auto-creates inline input pins for each non-Handle parameter:
 *    - {Param}_Name (FString)   — the MCP argument name exposed to clients
 *    - {Param}_Desc (FString)   — the MCP argument description
 *    - For Int/Number: {Param}_Min / {Param}_Max
 * 3. Fill in the ToolDescription pin.
 * 4. On execution the tool is registered and will auto-dispatch to the selected function.
 */
UCLASS()
class UK2Node_MCPAutoRegister : public UK2Node
{
    GENERATED_BODY()

public:
    // The single function to register (dropdown populated by GetFunctionOptions)
    UPROPERTY(EditAnywhere, Category = "MCP", meta = (GetOptions = "GetFunctionOptions"))
    FName SelectedFunction;

    // Provides the dropdown options for SelectedFunction
    UFUNCTION()
    TArray<FString> GetFunctionOptions() const;

    //~ Begin UEdGraphNode Interface
    virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
    virtual FText GetTooltipText() const override;
    virtual FLinearColor GetNodeTitleColor() const override;
    virtual bool ShouldShowNodeProperties() const override { return true; }
    virtual void AllocateDefaultPins() override;
    virtual void PostReconstructNode() override;
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
    //~ End UEdGraphNode Interface

    //~ Begin UK2Node Interface
    virtual bool IsNodePure() const override { return false; }
    virtual FText GetMenuCategory() const override;
    virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
    virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
    virtual void ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const override;
    //~ End UK2Node Interface

    // Get all eligible functions from the owning blueprint class
    TArray<FName> GetAvailableFunctions() const;

    // Parameter info (for pin generation and ExpandNode)
    struct FParamPinInfo
    {
        FName CppParamName;
        FString TypeHint; // "String", "Number", "Int", "Bool", "Actor", "Component"
        UClass* ObjectClass = nullptr; // For Actor/Component: the specific class constraint
    };

    TArray<FParamPinInfo> GetParamPinInfos() const;

private:
    void ReconstructDynamicPins();
    bool IsDynamicPin(const UEdGraphPin* Pin) const;

    static FString ToSnakeCase(const FString& InValue);
    static FString StripToolPrefix(const FString& FunctionName, const FString& Prefix);

    // Prefix for dynamic pins (distinguishes them from fixed pins)
    static const FString DynPinPrefix;
};
