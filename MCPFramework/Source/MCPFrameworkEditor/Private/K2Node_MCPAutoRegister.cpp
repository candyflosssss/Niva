// K2Node: Register a single UFUNCTION as MCP Tool — inline property configuration
#include "K2Node_MCPAutoRegister.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "KismetCompiler.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet/GameplayStatics.h"

#include "MCP/MCPTransportSubsystem.h"
#include "MCP/MCPToolProperty.h"
#include "MCP/MCPToolHandle.h"
#include "Components/Base/McpExposableBaseComponent.h"

#define LOCTEXT_NAMESPACE "K2Node_MCPAutoRegister"

// ---------- Constants ----------
static const FName PN_Target(TEXT("Target"));
static const FName PN_ToolDescription(TEXT("ToolDescription"));
const FString UK2Node_MCPAutoRegister::DynPinPrefix = TEXT("P_");

// Sub-pin suffixes
static const FString Suffix_Name = TEXT("_Name");
static const FString Suffix_Desc = TEXT("_Desc");
static const FString Suffix_Min  = TEXT("_Min");
static const FString Suffix_Max  = TEXT("_Max");

// ---------- UEdGraphNode ----------

FText UK2Node_MCPAutoRegister::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
    if (SelectedFunction.IsNone())
    {
        return LOCTEXT("NodeTitle", "Register MCP Tool");
    }
    return FText::Format(LOCTEXT("NodeTitleWithFunc", "Register MCP Tool: {0}"), FText::FromName(SelectedFunction));
}

FText UK2Node_MCPAutoRegister::GetTooltipText() const
{
    return LOCTEXT("Tooltip",
        "Registers a single UFUNCTION as an MCP tool.\n"
        "Select a function in the Details panel, then fill in the parameter names and descriptions on the inline pins.\n"
        "The node internally creates the required UMCPToolProperty objects and registers the tool.");
}

FLinearColor UK2Node_MCPAutoRegister::GetNodeTitleColor() const
{
    return FLinearColor(0.0f, 0.55f, 0.82f);
}

FText UK2Node_MCPAutoRegister::GetMenuCategory() const
{
    return LOCTEXT("MenuCategory", "NetworkCore|MCP");
}

void UK2Node_MCPAutoRegister::AllocateDefaultPins()
{
    // Exec
    CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);
    CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then);

    // Target (self)
    UEdGraphPin* TargetPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Object, UObject::StaticClass(), PN_Target);
    TargetPin->PinFriendlyName = LOCTEXT("Target", "Target");

    // Tool description
    UEdGraphPin* DescPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_String, PN_ToolDescription);
    DescPin->PinFriendlyName = LOCTEXT("ToolDesc", "Tool Description");
    DescPin->DefaultValue = TEXT("");

    // Dynamic pins based on selected function
    ReconstructDynamicPins();
}

void UK2Node_MCPAutoRegister::PostReconstructNode()
{
    Super::PostReconstructNode();
    ReconstructDynamicPins();
}

void UK2Node_MCPAutoRegister::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    if (PropertyChangedEvent.Property &&
        PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UK2Node_MCPAutoRegister, SelectedFunction))
    {
        ReconstructNode();
    }
}

// ---------- Dynamic Pin Management ----------

bool UK2Node_MCPAutoRegister::IsDynamicPin(const UEdGraphPin* Pin) const
{
    return Pin && Pin->PinName.ToString().StartsWith(DynPinPrefix);
}

void UK2Node_MCPAutoRegister::ReconstructDynamicPins()
{
    // Remove old dynamic pins
    TArray<UEdGraphPin*> ToRemove;
    for (UEdGraphPin* Pin : Pins)
    {
        if (IsDynamicPin(Pin))
        {
            ToRemove.Add(Pin);
        }
    }
    for (UEdGraphPin* Pin : ToRemove)
    {
        Pins.Remove(Pin);
        Pin->MarkAsGarbage();
    }

    if (SelectedFunction.IsNone())
    {
        return;
    }

    // Create inline pins for each parameter
    const TArray<FParamPinInfo> ParamInfos = GetParamPinInfos();
    for (const FParamPinInfo& Info : ParamInfos)
    {
        const FString Base = DynPinPrefix + Info.CppParamName.ToString();

        // Name pin (always)
        {
            FName PinName(*(Base + Suffix_Name));
            UEdGraphPin* P = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_String, PinName);
            P->PinFriendlyName = FText::Format(LOCTEXT("ParamName", "{0} (Name)"), FText::FromString(Info.CppParamName.ToString()));
            P->DefaultValue = Info.CppParamName.ToString();
        }
        // Description pin (always)
        {
            FName PinName(*(Base + Suffix_Desc));
            UEdGraphPin* P = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_String, PinName);
            P->PinFriendlyName = FText::Format(LOCTEXT("ParamDesc", "{0} (Desc)"), FText::FromString(Info.CppParamName.ToString()));
            P->DefaultValue = TEXT("");
        }
        // Min/Max pins (for Int and Number)
        if (Info.TypeHint == TEXT("Int") || Info.TypeHint == TEXT("Number"))
        {
            {
                FName PinName(*(Base + Suffix_Min));
                UEdGraphPin* P = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Int, PinName);
                P->PinFriendlyName = FText::Format(LOCTEXT("ParamMin", "{0} (Min)"), FText::FromString(Info.CppParamName.ToString()));
                P->DefaultValue = TEXT("0");
            }
            {
                FName PinName(*(Base + Suffix_Max));
                UEdGraphPin* P = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Int, PinName);
                P->PinFriendlyName = FText::Format(LOCTEXT("ParamMax", "{0} (Max)"), FText::FromString(Info.CppParamName.ToString()));
                P->DefaultValue = TEXT("100");
            }
        }
    }
}

// ---------- Parameter inspection ----------

TArray<UK2Node_MCPAutoRegister::FParamPinInfo> UK2Node_MCPAutoRegister::GetParamPinInfos() const
{
    TArray<FParamPinInfo> Result;

    UBlueprint* BP = FBlueprintEditorUtils::FindBlueprintForNode(this);
    if (!BP || SelectedFunction.IsNone())
    {
        return Result;
    }

    // Try skeleton first (has custom events before compile), then generated
    UFunction* Func = nullptr;
    if (BP->SkeletonGeneratedClass)
    {
        Func = BP->SkeletonGeneratedClass->FindFunctionByName(SelectedFunction);
    }
    if (!Func && BP->GeneratedClass)
    {
        Func = BP->GeneratedClass->FindFunctionByName(SelectedFunction);
    }
    if (!Func)
    {
        return Result;
    }

    for (TFieldIterator<FProperty> It(Func); It; ++It)
    {
        FProperty* Property = *It;
        if (!Property || !Property->HasAnyPropertyFlags(CPF_Parm) || Property->HasAnyPropertyFlags(CPF_ReturnParm))
        {
            continue;
        }

        // Skip Handle param
        if (const FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Property))
        {
            if (ObjProp->PropertyClass && ObjProp->PropertyClass->IsChildOf(UMCPToolHandle::StaticClass()))
            {
                continue;
            }

            FParamPinInfo Info;
            Info.CppParamName = Property->GetFName();

            if (ObjProp->PropertyClass && ObjProp->PropertyClass->IsChildOf(AActor::StaticClass()))
            {
                Info.TypeHint = TEXT("Actor");
                Info.ObjectClass = ObjProp->PropertyClass;
            }
            else if (ObjProp->PropertyClass && ObjProp->PropertyClass->IsChildOf(UMcpExposableBaseComponent::StaticClass()))
            {
                Info.TypeHint = TEXT("Component");
                Info.ObjectClass = ObjProp->PropertyClass;
            }
            else
            {
                continue; // unsupported object type
            }
            Result.Add(Info);
            continue;
        }

        FParamPinInfo Info;
        Info.CppParamName = Property->GetFName();

        if (CastField<FStrProperty>(Property) || CastField<FNameProperty>(Property))
        {
            Info.TypeHint = TEXT("String");
        }
        else if (CastField<FIntProperty>(Property))
        {
            Info.TypeHint = TEXT("Int");
        }
        else if (CastField<FFloatProperty>(Property) || CastField<FDoubleProperty>(Property))
        {
            Info.TypeHint = TEXT("Number");
        }
        else if (CastField<FBoolProperty>(Property))
        {
            Info.TypeHint = TEXT("Bool");
        }
        else
        {
            continue; // unsupported type
        }

        Result.Add(Info);
    }

    return Result;
}

// ---------- Menu registration ----------

void UK2Node_MCPAutoRegister::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UClass* ActionKey = GetClass();
    if (ActionRegistrar.IsOpenForRegistration(ActionKey))
    {
        UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
        check(Spawner != nullptr);
        ActionRegistrar.AddBlueprintAction(ActionKey, Spawner);
    }
}

TArray<FName> UK2Node_MCPAutoRegister::GetAvailableFunctions() const
{
    UBlueprint* OwningBP = FBlueprintEditorUtils::FindBlueprintForNode(this);
    if (!OwningBP)
    {
        return TArray<FName>();
    }

    // Try SkeletonGeneratedClass first (reflects custom events before compile)
    TArray<FName> Result;
    if (OwningBP->SkeletonGeneratedClass)
    {
        Result = UMCPTransportSubsystem::GetEligibleMCPFunctions(OwningBP->SkeletonGeneratedClass);
    }
    // Merge from GeneratedClass (may have additional compiled functions)
    if (OwningBP->GeneratedClass)
    {
        for (const FName& N : UMCPTransportSubsystem::GetEligibleMCPFunctions(OwningBP->GeneratedClass))
        {
            Result.AddUnique(N);
        }
    }
    return Result;
}

TArray<FString> UK2Node_MCPAutoRegister::GetFunctionOptions() const
{
    TArray<FString> Options;
    Options.Add(TEXT("None"));
    for (const FName& N : GetAvailableFunctions())
    {
        Options.Add(N.ToString());
    }
    return Options;
}

// ---------- ExpandNode helpers ----------

// Safe pin lookup — returns nullptr and logs error on failure
static UEdGraphPin* SafeFindPin(UEdGraphNode* Node, FName PinName, FKismetCompilerContext& Ctx, UK2Node* SourceNode)
{
    UEdGraphPin* Pin = Node->FindPin(PinName);
    if (!Pin)
    {
        Ctx.MessageLog.Error(
            *FString::Printf(TEXT("@@: Internal error — pin '%s' not found on intermediate node '%s'."),
                *PinName.ToString(), *Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString()),
            SourceNode);
    }
    return Pin;
}

// ---------- ExpandNode ----------

void UK2Node_MCPAutoRegister::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
    Super::ExpandNode(CompilerContext, SourceGraph);

    // "None" from the dropdown or truly empty
    if (SelectedFunction.IsNone() || SelectedFunction == FName(TEXT("None")))
    {
        CompilerContext.MessageLog.Warning(*LOCTEXT("NoFunction", "@@: No function selected.").ToString(), this);
        BreakAllNodeLinks();
        return;
    }

    const TArray<FParamPinInfo> ParamInfos = GetParamPinInfos();

    // ---- Locate runtime functions ----
    UFunction* BeginFunc = UMCPTransportSubsystem::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UMCPTransportSubsystem, BeginRegisterCustomTool));
    UFunction* AddPropFunc = UMCPTransportSubsystem::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UMCPTransportSubsystem, AddCustomToolProperty));
    UFunction* CommitFunc = UMCPTransportSubsystem::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UMCPTransportSubsystem, CommitCustomToolRegistration));

    if (!BeginFunc || !AddPropFunc || !CommitFunc)
    {
        CompilerContext.MessageLog.Error(*LOCTEXT("MissingFuncs", "@@: Missing runtime registration functions.").ToString(), this);
        BreakAllNodeLinks();
        return;
    }

    // ---- Source pins ----
    UEdGraphPin* ExecIn = GetExecPin();
    UEdGraphPin* ExecOut = FindPin(UEdGraphSchema_K2::PN_Then);
    UEdGraphPin* TargetPin = FindPin(PN_Target);
    UEdGraphPin* ToolDescPin = FindPin(PN_ToolDescription);
    if (!ExecIn || !ExecOut || !TargetPin || !ToolDescPin)
    {
        CompilerContext.MessageLog.Error(*LOCTEXT("MissingPins", "@@: Missing required pins on node.").ToString(), this);
        BreakAllNodeLinks();
        return;
    }

    // ---- Get subsystem (pure chain: GetGameInstance → GetGameInstanceSubsystem) ----
    UK2Node_CallFunction* GetGINode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
    {
        UFunction* GetGIFunc = UGameplayStatics::StaticClass()->FindFunctionByName(TEXT("GetGameInstance"));
        GetGINode->SetFromFunction(GetGIFunc);
        GetGINode->AllocateDefaultPins();
    }
    UEdGraphPin* GI_WorldCtx = GetGINode->FindPin(TEXT("WorldContextObject"));
    if (GI_WorldCtx)
    {
        CompilerContext.CopyPinLinksToIntermediate(*TargetPin, *GI_WorldCtx);
    }

    UClass* SubsystemLibClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.SubsystemBlueprintLibrary"));
    UFunction* GetSubsystemFunc = SubsystemLibClass ? SubsystemLibClass->FindFunctionByName(TEXT("GetGameInstanceSubsystem")) : nullptr;
    if (!GetSubsystemFunc)
    {
        CompilerContext.MessageLog.Error(*LOCTEXT("NoSubsystemLib", "@@: Cannot find SubsystemBlueprintLibrary.").ToString(), this);
        BreakAllNodeLinks();
        return;
    }

    UK2Node_CallFunction* GetSubNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
    GetSubNode->SetFromFunction(GetSubsystemFunc);
    GetSubNode->AllocateDefaultPins();

    // Parameter name is "ContextObject" (not "GameInstance")
    UEdGraphPin* SubCtxPin = GetSubNode->FindPin(TEXT("ContextObject"));
    if (SubCtxPin)
    {
        GetGINode->GetReturnValuePin()->MakeLinkTo(SubCtxPin);
    }
    UEdGraphPin* SubClassPin = GetSubNode->FindPin(TEXT("Class"));
    if (SubClassPin)
    {
        SubClassPin->DefaultObject = UMCPTransportSubsystem::StaticClass();
    }
    UEdGraphPin* SubsystemPin = GetSubNode->GetReturnValuePin();

    // ---- BeginRegisterCustomTool ----
    UK2Node_CallFunction* BeginNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
    BeginNode->SetFromFunction(BeginFunc);
    BeginNode->AllocateDefaultPins();

    if (UEdGraphPin* SelfPin = BeginNode->FindPin(UEdGraphSchema_K2::PN_Self))
    {
        SubsystemPin->MakeLinkTo(SelfPin);
    }
    if (UEdGraphPin* P = BeginNode->FindPin(TEXT("Target")))
    {
        CompilerContext.CopyPinLinksToIntermediate(*TargetPin, *P);
    }
    if (UEdGraphPin* P = BeginNode->FindPin(TEXT("FunctionName")))
    {
        P->DefaultValue = SelectedFunction.ToString();
    }
    if (UEdGraphPin* P = BeginNode->FindPin(TEXT("ToolDescription")))
    {
        CompilerContext.MovePinLinksToIntermediate(*ToolDescPin, *P);
    }

    UEdGraphPin* ToolNamePin = BeginNode->GetReturnValuePin(); // FString ToolName

    // Wire exec: ExecIn → BeginNode
    CompilerContext.MovePinLinksToIntermediate(*ExecIn, *BeginNode->GetExecPin());
    UEdGraphPin* PrevExecOut = BeginNode->FindPin(UEdGraphSchema_K2::PN_Then);
    if (!PrevExecOut)
    {
        CompilerContext.MessageLog.Error(*LOCTEXT("NoThen", "@@: BeginRegisterCustomTool missing Then pin.").ToString(), this);
        BreakAllNodeLinks();
        return;
    }

    // ---- For each parameter: Create*Property (pure) + AddCustomToolProperty (exec) ----
    for (int32 i = 0; i < ParamInfos.Num(); ++i)
    {
        const FParamPinInfo& Info = ParamInfos[i];
        const FString Base = DynPinPrefix + Info.CppParamName.ToString();

        // Find our inline pins
        UEdGraphPin* MyNamePin = FindPin(FName(*(Base + Suffix_Name)));
        UEdGraphPin* MyDescPin = FindPin(FName(*(Base + Suffix_Desc)));
        UEdGraphPin* MyMinPin  = FindPin(FName(*(Base + Suffix_Min)));
        UEdGraphPin* MyMaxPin  = FindPin(FName(*(Base + Suffix_Max)));

        // Determine which Create*Property to call
        UFunction* CreateFunc = nullptr;
        if (Info.TypeHint == TEXT("String"))
        {
            CreateFunc = UMCPToolPropertyString::StaticClass()->FindFunctionByName(TEXT("CreateStringProperty"));
        }
        else if (Info.TypeHint == TEXT("Bool"))
        {
            CreateFunc = UMCPToolPropertyBool::StaticClass()->FindFunctionByName(TEXT("CreateBoolProperty"));
        }
        else if (Info.TypeHint == TEXT("Int"))
        {
            CreateFunc = UMCPToolPropertyInt::StaticClass()->FindFunctionByName(TEXT("CreateIntProperty"));
        }
        else if (Info.TypeHint == TEXT("Number"))
        {
            CreateFunc = UMCPToolPropertyNumber::StaticClass()->FindFunctionByName(TEXT("CreateNumberProperty"));
        }
        else if (Info.TypeHint == TEXT("Actor"))
        {
            CreateFunc = UMCPToolPropertyActorPtr::StaticClass()->FindFunctionByName(TEXT("CreateActorPtrProperty"));
        }
        else if (Info.TypeHint == TEXT("Component"))
        {
            CreateFunc = UMCPToolPropertyComponentPtr::StaticClass()->FindFunctionByName(TEXT("CreateComponentPtrProperty"));
        }

        if (!CreateFunc)
        {
            CompilerContext.MessageLog.Error(*FText::Format(
                LOCTEXT("MissingCreateFunc", "@@: Cannot find Create function for type {0}."),
                FText::FromString(Info.TypeHint)).ToString(), this);
            continue;
        }

        // Spawn Create*Property node (pure — no exec)
        UK2Node_CallFunction* CreateNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
        CreateNode->SetFromFunction(CreateFunc);
        CreateNode->AllocateDefaultPins();

        // Connect InName
        if (MyNamePin)
        {
            if (UEdGraphPin* P = CreateNode->FindPin(TEXT("InName")))
            {
                CompilerContext.MovePinLinksToIntermediate(*MyNamePin, *P);
            }
        }
        // Connect InDescription
        if (MyDescPin)
        {
            if (UEdGraphPin* P = CreateNode->FindPin(TEXT("InDescription")))
            {
                CompilerContext.MovePinLinksToIntermediate(*MyDescPin, *P);
            }
        }
        // Connect Min/Max for Int and Number
        if ((Info.TypeHint == TEXT("Int") || Info.TypeHint == TEXT("Number")) && MyMinPin && MyMaxPin)
        {
            if (UEdGraphPin* P = CreateNode->FindPin(TEXT("InMin")))
            {
                CompilerContext.MovePinLinksToIntermediate(*MyMinPin, *P);
            }
            if (UEdGraphPin* P = CreateNode->FindPin(TEXT("InMax")))
            {
                CompilerContext.MovePinLinksToIntermediate(*MyMaxPin, *P);
            }
        }
        // Connect ActorClass / ComponentClass (set default from reflected info)
        if (Info.TypeHint == TEXT("Actor") && Info.ObjectClass)
        {
            if (UEdGraphPin* ClassPin = CreateNode->FindPin(TEXT("InActorClass")))
            {
                ClassPin->DefaultValue = Info.ObjectClass->GetPathName();
            }
        }
        else if (Info.TypeHint == TEXT("Component") && Info.ObjectClass)
        {
            if (UEdGraphPin* ClassPin = CreateNode->FindPin(TEXT("InComponentClass")))
            {
                ClassPin->DefaultValue = Info.ObjectClass->GetPathName();
            }
        }

        UEdGraphPin* CreatedPropertyPin = CreateNode->GetReturnValuePin();

        // Spawn AddCustomToolProperty node (exec)
        UK2Node_CallFunction* AddNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
        AddNode->SetFromFunction(AddPropFunc);
        AddNode->AllocateDefaultPins();

        if (UEdGraphPin* P = AddNode->FindPin(UEdGraphSchema_K2::PN_Self))
        {
            SubsystemPin->MakeLinkTo(P);
        }
        if (UEdGraphPin* P = AddNode->FindPin(TEXT("ToolName")))
        {
            ToolNamePin->MakeLinkTo(P);
        }
        if (CreatedPropertyPin)
        {
            if (UEdGraphPin* P = AddNode->FindPin(TEXT("Property")))
            {
                CreatedPropertyPin->MakeLinkTo(P);
            }
        }

        // Exec chain
        PrevExecOut->MakeLinkTo(AddNode->GetExecPin());
        PrevExecOut = AddNode->FindPin(UEdGraphSchema_K2::PN_Then);
        if (!PrevExecOut)
        {
            break;
        }
    }

    // ---- CommitCustomToolRegistration ----
    if (PrevExecOut)
    {
        UK2Node_CallFunction* CommitNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
        CommitNode->SetFromFunction(CommitFunc);
        CommitNode->AllocateDefaultPins();

        if (UEdGraphPin* P = CommitNode->FindPin(UEdGraphSchema_K2::PN_Self))
        {
            SubsystemPin->MakeLinkTo(P);
        }
        if (UEdGraphPin* P = CommitNode->FindPin(TEXT("ToolName")))
        {
            ToolNamePin->MakeLinkTo(P);
        }

        PrevExecOut->MakeLinkTo(CommitNode->GetExecPin());
        if (UEdGraphPin* P = CommitNode->FindPin(UEdGraphSchema_K2::PN_Then))
        {
            CompilerContext.MovePinLinksToIntermediate(*ExecOut, *P);
        }
    }

    BreakAllNodeLinks();
}

// ---------- Validation ----------

void UK2Node_MCPAutoRegister::ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const
{
    Super::ValidateNodeDuringCompilation(MessageLog);

    if (SelectedFunction.IsNone())
    {
        MessageLog.Warning(*LOCTEXT("NoFuncWarn", "@@: No function selected for MCP tool registration.").ToString(), this);
        return;
    }

    TArray<FName> Available = GetAvailableFunctions();
    if (!Available.Contains(SelectedFunction))
    {
        MessageLog.Error(*FText::Format(
            LOCTEXT("FuncNotFound", "@@: Function '{0}' is no longer eligible (removed or signature changed)."),
            FText::FromName(SelectedFunction)
        ).ToString(), this);
    }
}

// ---------- Helpers ----------

FString UK2Node_MCPAutoRegister::ToSnakeCase(const FString& InValue)
{
    FString Out;
    Out.Reserve(InValue.Len() + 8);
    auto AppendSep = [&Out]() { if (!Out.IsEmpty() && !Out.EndsWith(TEXT("_"))) Out.AppendChar(TEXT('_')); };

    for (int32 i = 0; i < InValue.Len(); ++i)
    {
        const TCHAR C = InValue[i];
        if (FChar::IsUpper(C))
        {
            const bool bPrev = i > 0;
            const bool bPrevNeed = bPrev && (FChar::IsLower(InValue[i - 1]) || FChar::IsDigit(InValue[i - 1]));
            const bool bNextLow = (i + 1 < InValue.Len()) && FChar::IsLower(InValue[i + 1]);
            if (bPrevNeed || (bPrev && bNextLow)) AppendSep();
            Out.AppendChar(FChar::ToLower(C));
        }
        else if (FChar::IsAlnum(C))
        {
            Out.AppendChar(FChar::ToLower(C));
        }
        else
        {
            AppendSep();
        }
    }
    while (Out.StartsWith(TEXT("_"))) Out.RightChopInline(1, EAllowShrinking::No);
    while (Out.EndsWith(TEXT("_"))) Out.LeftChopInline(1, EAllowShrinking::No);
    return Out.IsEmpty() ? InValue.ToLower() : Out;
}

FString UK2Node_MCPAutoRegister::StripToolPrefix(const FString& FunctionName, const FString& Prefix)
{
    if (!Prefix.IsEmpty() && FunctionName.StartsWith(Prefix))
    {
        return FunctionName.RightChop(Prefix.Len());
    }
    return FunctionName;
}

#undef LOCTEXT_NAMESPACE
