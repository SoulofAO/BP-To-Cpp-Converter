#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSettings.h"
#include "BlueprintNativizationUniqueNameSubsystem.h"
#include "GeneratedCodeCleanup.h"
#include "GenerationGraphContext.h"
#include "Engine/Engine.h"
#include "Engine/Blueprint.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableSet.h"

static bool DoesExecutionAlwaysReturn(UK2Node* Node, UNativizationV2Subsystem* Subsystem, TSet<UK2Node*> ActiveNodes)
{
    if (!Node || ActiveNodes.Contains(Node))
    {
        return false;
    }
    if (Node->IsA<UK2Node_FunctionResult>())
    {
        return true;
    }
    ActiveNodes.Add(Node);
    const TArray<UEdGraphPin*> Outputs = UBlueprintNativizationLibrary::GetFilteredPins(Node,
        EPinOutputOrInputFilter::Ouput, EPinExcludeFilter::None, EPinIncludeOnlyFilter::ExecPin);
    const bool bSequence = Node->IsA<UK2Node_ExecutionSequence>();
    if (Outputs.IsEmpty() || (Outputs.Num() > 1 && !bSequence &&
        !Node->IsA<UK2Node_IfThenElse>() && !Node->IsA<UK2Node_DynamicCast>()))
    {
        return false;
    }
    for (UEdGraphPin* Output : Outputs)
    {
        TArray<UK2Node*> MacroStack;
        UEdGraphPin* Destination = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Output, 0, false, MacroStack, Subsystem);
        bool bReturns = false;
        if (Destination)
        {
            UK2Node* NextNode = Cast<UK2Node>(Destination->GetOwningNode());
            const bool bOtherEntry = Subsystem->EntryNodes.ContainsByPredicate([&](const FGenerateFunctionStruct& Entry)
            {
                return Entry.Node == NextNode;
            });
            if (!bOtherEntry)
            {
                bReturns = DoesExecutionAlwaysReturn(NextNode, Subsystem, ActiveNodes);
            }
        }
        if (bSequence && bReturns)
        {
            return true;
        }
        if (!bSequence && !bReturns)
        {
            return false;
        }
    }
    return !bSequence;
}

UEdGraphPin* UNativizationV2Subsystem::FindCommonExecutionTail(UEdGraphPin* FirstBranch, UEdGraphPin* SecondBranch)
{
    return FindCommonExecutionTail(TArray<UEdGraphPin*>{FirstBranch, SecondBranch});
}

UEdGraphPin* UNativizationV2Subsystem::FindCommonExecutionTail(const TArray<UEdGraphPin*>& Branches)
{
    if (Branches.Num() < 2)
    {
        return nullptr;
    }
    TArray<UEdGraphPin*> CommonPins;
    bool bFirstBranch = true;
    for (UEdGraphPin* Branch : Branches)
    {
        TArray<UEdGraphPin*> Path;
        TSet<UEdGraphPin*> VisitedPins;
        UEdGraphPin* Current = Branch;
        while (Current)
        {
            TArray<UK2Node*> MacroStack;
            Current = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Current, 0, false, MacroStack, this);
            if (!Current || !MacroStack.IsEmpty() || VisitedPins.Contains(Current))
            {
                break;
            }
            VisitedPins.Add(Current);
            Path.Add(Current);
            UK2Node* CurrentNode = CastChecked<UK2Node>(Current->GetOwningNode());
            bool bSynchronous = CurrentNode->IsA<UK2Node_VariableSet>();
            if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(CurrentNode))
            {
                UFunction* Function = Call->GetTargetFunction();
                bSynchronous = Function && !Function->HasMetaData(TEXT("Latent"));
            }
            if (!bSynchronous)
            {
                break;
            }
            const TArray<UEdGraphPin*> Outputs = UBlueprintNativizationLibrary::GetFilteredPins(CurrentNode,
                EPinOutputOrInputFilter::Ouput, EPinExcludeFilter::None, EPinIncludeOnlyFilter::ExecPin);
            if (Outputs.Num() != 1 || Outputs[0]->LinkedTo.Num() != 1)
            {
                break;
            }
            Current = Outputs[0];
        }
        if (bFirstBranch)
        {
            CommonPins = MoveTemp(Path);
            bFirstBranch = false;
        }
        else
        {
            CommonPins.RemoveAll([&Path](UEdGraphPin* Pin) { return !Path.Contains(Pin); });
        }
        if (CommonPins.IsEmpty())
        {
            return nullptr;
        }
    }
    return CommonPins[0];
}

FString UNativizationV2Subsystem::GetNodeStateName(UK2Node* Node, const FString& Suffix, bool bLocal)
{
    const TArray<UK2Node*> NodeMacroStack = FGenerationGraphContext::GetMacroStack(Node);
    FString Name = Suffix;
    for (UK2Node* Macro : NodeMacroStack)
    {
        Name = Macro->GetGraph()->GetName() + Name;
    }
    FString Identifier;
    for (TCHAR Character : Name)
    {
        if (FChar::IsAlnum(Character) || Character == TEXT('_'))
        {
            Identifier.AppendChar(Character);
        }
    }
    if (Identifier.IsEmpty() || FChar::IsDigit(Identifier[0]))
    {
        Identifier = TEXT("Macro") + Identifier;
    }
    UFunction* OwnerFunction = nullptr;
    UK2Node* EntryNode = nullptr;
    if (bLocal)
    {
        UK2Node* Context = Node;
        if (DataConsumer)
        {
            Context = DataConsumer;
        }
        if (LocalVariableEntryNode)
        {
            EntryNode = LocalVariableEntryNode;
            const FGenerateFunctionStruct* Entry = EntryNodes.FindByPredicate([&](const FGenerateFunctionStruct& Candidate)
            {
                return Candidate.Node == LocalVariableEntryNode;
            });
            if (Entry)
            {
                OwnerFunction = Entry->OriginalUFunction;
            }
        }
        else
        {
            FGenerateFunctionStruct Entry;
            UBlueprintNativizationLibrary::GetEntryByNodeAndEntryNodes(Context, EntryNodes, Entry);
            EntryNode = Entry.Node;
            OwnerFunction = UBlueprintNativizationLibrary::GetFunctionByNodeAndEntryNodes(Context, EntryNodes);
        }
    }
    FString EvaluationIdentity = Node->GetPathName();
    for (UK2Node* Macro : NodeMacroStack)
    {
        EvaluationIdentity += TEXT(":") + Macro->GetPathName();
    }
    if (bLocal)
    {
        EvaluationIdentity += TEXT(":") + InputEvaluationPurpose;
        if (bRequiresInputStorage)
        {
            EvaluationIdentity += TEXT(":Storage");
        }
    }
    UBlueprint* Blueprint = FGenerationGraphContext::GetBlueprint();
    if (!Blueprint)
    {
        Blueprint = Node->GetBlueprint();
    }
    if (bLocal && DataConsumer)
    {
        EvaluationIdentity += FString::Format(TEXT(":{0}:{1}"), {DataConsumer->GetPathName(), CurrentEvaluation});
    }
    return GEngine->GetEngineSubsystem<UBlueprintNativizationUniqueNameSubsystem>()->GenerateUniqueLambdaVariableName(
        Identifier, Blueprint->GeneratedClass, OwnerFunction, Node, EntryNode, EvaluationIdentity).ToString();
}

FString UNativizationV2Subsystem::RegisterRepeatedCall(const FString& Signature, const FString& Name,
    const FString& Definition, const FString& ExpandedCode, const FString& Arguments, const FString& ResultName)
{
    if (!bCollectRepeatedCalls)
    {
        return ExpandedCode;
    }
    if (const FRepeatedCallSite* ExistingSite = RepeatedCallSites.FindByPredicate([&](const FRepeatedCallSite& Candidate)
        { return Candidate.Signature == Signature && Candidate.ExpandedCode == ExpandedCode; }))
    {
        return ExistingSite->Marker;
    }
    FRepeatedCallSite Site;
    Site.Signature = Signature;
    Site.Name = Name;
    Site.Definition = Definition;
    if (const FRepeatedCallSite* Existing = RepeatedCallSites.FindByPredicate([&](const FRepeatedCallSite& Candidate)
        { return Candidate.Signature == Signature; }))
    {
        Site.Name = Existing->Name;
        Site.Definition = Existing->Definition;
    }
    Site.Marker = FString::Format(TEXT("__RepeatedCallSite{0}__"), {RepeatedCallSites.Num()});
    Site.ExpandedCode = ExpandedCode;
    if (!ResultName.IsEmpty())
    {
        Site.Invocation = ResultName + TEXT(" = ");
    }
    Site.Invocation += Site.Name + TEXT("(") + Arguments + TEXT(");\n");
    RepeatedCallSites.Add(Site);
    return Site.Marker;
}

FString UNativizationV2Subsystem::ResolveRepeatedCalls(FString Content) const
{
    TMap<FString, int32> Occurrences;
    for (const FRepeatedCallSite& Site : RepeatedCallSites)
    {
        int32 SearchPosition = 0;
        while ((SearchPosition = Content.Find(Site.Marker, ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchPosition)) != INDEX_NONE)
        {
            ++Occurrences.FindOrAdd(Site.Signature);
            SearchPosition += Site.Marker.Len();
        }
    }
    const int32 Threshold = FMath::Max(2, GetDefault<UBlueprintNativizationV2EditorSettings>()->RepeatedConstructionThreshold);
    FString Declarations;
    TSet<FString> DeclaredSignatures;
    for (const FRepeatedCallSite& Site : RepeatedCallSites)
    {
        if (Occurrences.FindRef(Site.Signature) >= Threshold)
        {
            if (!DeclaredSignatures.Contains(Site.Signature))
            {
                DeclaredSignatures.Add(Site.Signature);
                Declarations += TEXT("const auto ") + Site.Name + TEXT(" = ") + Site.Definition + TEXT(";\n");
            }
            Content.ReplaceInline(*Site.Marker, *Site.Invocation, ESearchCase::CaseSensitive);
        }
        else
        {
            Content.ReplaceInline(*Site.Marker, *Site.ExpandedCode, ESearchCase::CaseSensitive);
        }
    }
    return Declarations + FGeneratedCodeCleanup::CollapseImmediateScalarInitialization(Content);
}

FString UNativizationV2Subsystem::GenerateControlFlowFunctions(const FGenerateFunctionStruct& Function)
{
    RepeatedCallSites.Reset();
    TGuardValue<bool> RepeatedCallScope(bCollectRepeatedCalls, true);
    ControlFunctionEntries.Reset();
    ControlFunctionNames.Reset();
    UK2Node* EntryNode = Function.Node;
    TArray<FVisitedNodeStack> ActiveEntries;
    TArray<FVisitedNodeStack> CompletedEntries;
    TFunction<void(UK2Node*, FString, TArray<UK2Node*>)> VisitNode;
    VisitNode = [&](UK2Node* Node, FString EntryPinName, TArray<UK2Node*> Path)
    {
        if (!Node)
        {
            return;
        }
        const bool bOtherEntry = EntryNodes.ContainsByPredicate([&](const FGenerateFunctionStruct& Entry)
        {
            return Entry.Node == Node && Entry.MacroStack == Path && (!Entry.bIsLatentContinuation || Entry.EntryPinName == EntryPinName);
        });
        if ((Node != EntryNode || EntryPinName != Function.EntryPinName || Path != Function.MacroStack) && bOtherEntry)
        {
            return;
        }
        UTranslatorBPToCppObject* NodeTranslator = FindTranslatorForNode(Node);
        if (NodeTranslator && !NodeTranslator->CanCreateCircleWithPin(Node, EntryPinName))
        {
            return;
        }
        TGuardValue<TArray<UK2Node*>> MacroScope(CurrentMacroStack, Path);
        FVisitedNodeStack Entry(Node, EntryPinName);
        Entry.MacroStack = Path;
        if (ActiveEntries.Contains(Entry))
        {
            UTranslatorBPToCppObject* Translator = FindTranslatorForNode(Node);
            if (!Translator || Translator->CanCreateCircleWithPin(Node, EntryPinName))
            {
                if (!ControlFunctionEntries.Contains(Entry))
                {
                    ControlFunctionEntries.Add(Entry);
                    FString PinName = EntryPinName;
                    if (!PinName.IsEmpty())
                    {
                        PinName[0] = FChar::ToUpper(PinName[0]);
                    }
                    ControlFunctionNames.Add(GetNodeStateName(Node, TEXT("Continue") + PinName));
                }
            }
            return;
        }
        if (CompletedEntries.Contains(Entry))
        {
            return;
        }
        ActiveEntries.Add(Entry);
        for (UEdGraphPin* Pin : UBlueprintNativizationLibrary::GetFilteredPins(Node,
            EPinOutputOrInputFilter::Ouput, EPinExcludeFilter::None, EPinIncludeOnlyFilter::ExecPin))
        {
            TArray<UK2Node*> MacroStack = Path;
            if (UEdGraphPin* NextPin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Pin, 0, false, MacroStack, this))
            {
                VisitNode(Cast<UK2Node>(NextPin->GetOwningNode()), NextPin->GetName(), MacroStack);
            }
        }
        ActiveEntries.Pop();
        CompletedEntries.Add(Entry);
    };
    VisitNode(EntryNode, Function.EntryPinName, Function.MacroStack);
    FString Content;
    FProperty* ReturnProperty = nullptr;
    if (Function.OriginalUFunction)
    {
        FFunctionDescriptor NativeFunction;
        if (!UBlueprintNativizationSettingsLibrary::FindNativeByClassAndName(
            EntryNode->GetBlueprint()->GeneratedClass, Function.OriginalUFunction->GetName(), NativeFunction))
        {
            ReturnProperty = Function.OriginalUFunction->GetReturnProperty();
        }
    }
    bPropagateFunctionReturn = !ControlFunctionEntries.IsEmpty() && CompletedEntries.ContainsByPredicate(
        [](const FVisitedNodeStack& Entry) { return Entry.Node->IsA<UK2Node_FunctionResult>(); });
    FunctionResultName.Reset();
    if (bPropagateFunctionReturn)
    {
        FunctionReturnStateName = GetNodeStateName(EntryNode, TEXT("bFunctionReturned"));
        Content += FString::Format(TEXT("bool {0} = false;\n"), { FunctionReturnStateName });
        if (ReturnProperty)
        {
            FunctionResultName = GetNodeStateName(EntryNode, TEXT("FunctionResult"));
            Content += FString::Format(TEXT("{0} {1} = {};\n"), {
                UBlueprintNativizationLibrary::GetPropertyType(ReturnProperty), FunctionResultName });
        }
    }
    for (const FString& Name : ControlFunctionNames)
    {
        Content += FString::Format(TEXT("TFunction<void()> {0};\n"), { Name });
    }
    for (int32 FunctionIndex = 0; FunctionIndex < ControlFunctionEntries.Num(); ++FunctionIndex)
    {
        const FVisitedNodeStack& Entry = ControlFunctionEntries[FunctionIndex];
        bGeneratingControlFunctionEntry = true;
        bInsideControlFunction = true;
        const FString Body = GenerateCodeFromNode(Entry.Node, Entry.EntryPinName, {}, {}, Entry.MacroStack);
        Content += FString::Format(TEXT("{0} = [&]()\n{\n{1}};\n"), { ControlFunctionNames[FunctionIndex], Body });
    }
    bInsideControlFunction = false;
    Content += GenerateCodeFromNode(EntryNode, Function.EntryPinName, {}, {}, Function.MacroStack);
    TArray<FString> BodyLines;
    Content.TrimEnd().ParseIntoArrayLines(BodyLines);
    if (ReturnProperty && !DoesExecutionAlwaysReturn(EntryNode, this, {}) &&
        (BodyLines.IsEmpty() || !BodyLines.Last().TrimStart().StartsWith(TEXT("return "))))
    {
        Content += TEXT("return {};\n");
    }
    bPropagateFunctionReturn = false;
    ControlFunctionEntries.Reset();
    ControlFunctionNames.Reset();
    return ResolveRepeatedCalls(Content);
}

FString UNativizationV2Subsystem::GenerateFunctionReturnStatement(const FString& Value) const
{
    if (bInsideControlFunction && bPropagateFunctionReturn)
    {
        FString Content;
        if (!Value.IsEmpty())
        {
            Content += FString::Format(TEXT("{0} = {1};\n"), { FunctionResultName, Value });
        }
        return Content + FunctionReturnStateName + TEXT(" = true;\nreturn;\n");
    }
    if (Value.IsEmpty())
    {
        return TEXT("return;\n");
    }
    return FString::Format(TEXT("return {0};\n"), { Value });
}

FString UNativizationV2Subsystem::GenerateControlFunctionCall(int32 FunctionIndex) const
{
    FString Content = ControlFunctionNames[FunctionIndex] + TEXT("();\n");
    if (bPropagateFunctionReturn)
    {
        FString ReturnStatement = TEXT("return;\n");
        if (!bInsideControlFunction && !FunctionResultName.IsEmpty())
        {
            ReturnStatement = FString::Format(TEXT("return {0};\n"), { FunctionResultName });
        }
        Content += FString::Format(TEXT("if ({0})\n{\n{1}}\n"), { FunctionReturnStateName, ReturnStatement });
    }
    return Content;
}

int32 UNativizationV2Subsystem::GetLatentActionIdentifier(UK2Node* Node)
{
    if (const int32* Identifier = LatentActionIdentifiers.Find(Node))
    {
        return *Identifier;
    }
    const int32 Identifier = LatentActionIdentifiers.Num() + 1;
    LatentActionIdentifiers.Add(Node, Identifier);
    return Identifier;
}

FString UNativizationV2Subsystem::GetEvaluationKey(UK2Node* Node) const
{
    FString Identity = Node->GetPathName() + TEXT(":") + InputEvaluationPurpose;
    if (DataConsumer)
    {
        Identity += FString::Format(TEXT(":{0}:{1}"), {DataConsumer->GetPathName(), CurrentEvaluation});
    }
    for (UK2Node* Macro : CurrentMacroStack)
    {
        Identity += TEXT(":") + Macro->GetPathName();
    }
    return Identity;
}
