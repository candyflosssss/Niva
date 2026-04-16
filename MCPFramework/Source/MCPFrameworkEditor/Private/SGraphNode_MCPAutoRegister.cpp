// Custom SGraphNode — shows a function-selection combo box on the node body
#include "SGraphNode_MCPAutoRegister.h"
#include "K2Node_MCPAutoRegister.h"
#include "Widgets/Input/STextComboBox.h"
#include "ScopedTransaction.h"

void SGraphNode_MCPAutoRegister::Construct(const FArguments& InArgs, UK2Node_MCPAutoRegister* InNode)
{
    McpNode = InNode;
    GraphNode = InNode;
    SetCursor(EMouseCursor::CardinalCross);
    UpdateGraphNode();
}

void SGraphNode_MCPAutoRegister::CreateBelowPinControls(TSharedPtr<SVerticalBox> MainBox)
{
    if (!MainBox.IsValid())
    {
        return;
    }

    RebuildFunctionOptions();

    MainBox->AddSlot()
        .AutoHeight()
        .Padding(10.f, 4.f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(0.f, 0.f, 4.f, 0.f)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("Function")))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                .ColorAndOpacity(FLinearColor::White)
            ]
            + SHorizontalBox::Slot()
            .FillWidth(1.f)
            [
                SNew(STextComboBox)
                .OptionsSource(&FunctionOptions)
                .InitiallySelectedItem(SelectedOption)
                .OnSelectionChanged(this, &SGraphNode_MCPAutoRegister::OnFunctionSelected)
                .ToolTipText(FText::FromString(TEXT("Select the function to register as MCP tool")))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
            ]
        ];
}

void SGraphNode_MCPAutoRegister::RebuildFunctionOptions()
{
    FunctionOptions.Reset();
    SelectedOption.Reset();

    FunctionOptions.Add(MakeShared<FString>(TEXT("None")));

    if (McpNode.IsValid())
    {
        for (const FName& N : McpNode->GetAvailableFunctions())
        {
            FunctionOptions.Add(MakeShared<FString>(N.ToString()));
        }

        // Match current selection
        FString CurrentName = McpNode->SelectedFunction.IsNone()
            ? TEXT("None")
            : McpNode->SelectedFunction.ToString();

        for (const TSharedPtr<FString>& Opt : FunctionOptions)
        {
            if (*Opt == CurrentName)
            {
                SelectedOption = Opt;
                break;
            }
        }

        if (!SelectedOption.IsValid())
        {
            SelectedOption = FunctionOptions[0]; // fallback to "None"
        }
    }
}

void SGraphNode_MCPAutoRegister::OnFunctionSelected(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
{
    if (!NewSelection.IsValid() || !McpNode.IsValid())
    {
        return;
    }

    FName NewFunc = (*NewSelection == TEXT("None")) ? NAME_None : FName(**NewSelection);
    if (NewFunc != McpNode->SelectedFunction)
    {
        const FScopedTransaction Transaction(FText::FromString(TEXT("Change MCP Tool Function")));
        McpNode->Modify();
        McpNode->SelectedFunction = NewFunc;
        McpNode->ReconstructNode();
        // ReconstructNode → UpdateGraphNode → rebuilds combo box automatically
    }
}
