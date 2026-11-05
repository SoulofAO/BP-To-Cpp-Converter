/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright 2025 AO All Rights Reserved.
 */

#include "TranslatorObjects/Cycles/ForLoopTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "UObject/Package.h"
#include "BlueprintNativizationLibrary.h"
#include "GenerationGraphContext.h"
#include "Engine/Blueprint.h"
#include "NativeApiCatalog.h"

FString UForLoopTranslatorObject::GenerateBreak(UK2Node* Node, UNativizationV2Subsystem* Subsystem)
{
    if (!Subsystem->ActiveLoopNodes.IsEmpty() && Subsystem->ActiveLoopNodes.Last() != Node)
    {
        FNativeApiCatalog::AddError(FString::Format(
            TEXT("Cross-loop Break is unsupported in {0}.{1}: the body of {2} targets {3}. Connect Break to the currently active loop."),
            { Node->GetBlueprint()->GetName(), Node->GetGraph()->GetName(),
              Subsystem->ActiveLoopNodes.Last()->GetNodeTitle(ENodeTitleType::ListView).ToString(),
              Node->GetNodeTitle(ENodeTitleType::ListView).ToString() }));
        return FString();
    }
    return Subsystem->GetNodeStateName(Node, TEXT("BreakRequested")) + TEXT(" = true;\n");
}

FString UForLoopTranslatorObject::GenerateLoopState(UK2Node* Node, UNativizationV2Subsystem* Subsystem, bool bMember)
{
    FString Content;
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin->Direction == EGPD_Output && (Pin->PinName == TEXT("Index") || Pin->PinName == TEXT("Array Index")))
        {
            if (bMember)
            {
                Content += TEXT("UPROPERTY(Transient)\n");
            }
            Content += FString::Format(TEXT("int32 {0} = 0;\n"), { UBlueprintNativizationLibrary::GetUniquePinName(Pin, Subsystem->EntryNodes) });
        }
    }
    if (Node->FindPin(TEXT("Break")))
    {
        if (bMember)
        {
            Content += TEXT("UPROPERTY(Transient)\n");
        }
        Content += FString::Format(TEXT("bool {0} = false;\n"), { Subsystem->GetNodeStateName(Node, TEXT("BreakRequested")) });
    }
    return Content;
}

TSet<FString> UForLoopTranslatorObject::GenerateLocalVariables(UK2Node* InputNode, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem)
{
    if (FGenerationGraphContext::IsFunctionGraph(InputNode))
    {
        return { GenerateLoopState(InputNode, Subsystem, false) };
    }
    return {};
}

FString UForLoopTranslatorObject::GenerateGlobalVariables(UK2Node* InputNode, FString Prefix, UNativizationV2Subsystem* Subsystem)
{
    if (!FGenerationGraphContext::IsFunctionGraph(InputNode))
    {
        return GenerateLoopState(InputNode, Subsystem, true);
    }
    return FString();
}

FString UForLoopTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes,
    TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations, UNativizationV2Subsystem* Subsystem)
{
    const FString BreakName = Subsystem->GetNodeStateName(Node, TEXT("BreakRequested"));
    if (EntryPinName == TEXT("Break"))
    {
        return GenerateBreak(Node, Subsystem);
    }
    const bool bHasBreak = Node->FindPin(TEXT("Break")) != nullptr;
    const FString IndexName = UBlueprintNativizationLibrary::GetUniquePinName(Node->FindPin(TEXT("Index")), Subsystem->EntryNodes);
    const FGenerateResultStruct FirstIndex = Subsystem->GenerateInputParameterCodeForNode(Node, Node->FindPin(TEXT("FirstIndex")), 0, MacroStack);
    FGenerateResultStruct LastIndex;
    {
        TGuardValue<FString> EvaluationScope(Subsystem->InputEvaluationPurpose, TEXT("LoopCondition"));
        TGuardValue<TMap<FString, FGenerateResultStruct>> CallScope(Subsystem->PureFunctionCalls, TMap<FString, FGenerateResultStruct>());
        LastIndex = Subsystem->GenerateInputParameterCodeForNode(Node, Node->FindPin(TEXT("LastIndex")), 0, MacroStack);
    }
    FString Content = GenerateNewPreparations(Preparations, FirstIndex.Preparations);
    if (bHasBreak)
    {
        Content += BreakName + TEXT(" = false;\n");
    }
    FString Condition = LastIndex.Code;
    Condition = IndexName + TEXT(" <= ") + Condition;
    if (bHasBreak)
    {
        Condition = TEXT("!") + BreakName + TEXT(" && (") + Condition + TEXT(")");
    }
    FString BodyCode;
    TArray<UK2Node*> BodyStack = MacroStack;
    if (UEdGraphPin* Body = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Node->FindPin(TEXT("LoopBody")), 0, false, BodyStack, Subsystem))
    {
        TArray<UK2Node*> ActiveLoops = Subsystem->ActiveLoopNodes;
        ActiveLoops.Add(Node);
        TGuardValue<TArray<UK2Node*>> LoopScope(Subsystem->ActiveLoopNodes, MoveTemp(ActiveLoops));
        FGenerationPreparations BodyPreparations = Preparations;
        BodyCode = Subsystem->GenerateCodeFromNode(Cast<UK2Node>(Body->GetOwningNode()), Body->GetName(), VisitedNodes, BodyPreparations, BodyStack);
    }
    FString Increment = IndexName + TEXT("++;");
    if (bHasBreak)
    {
        Increment = FString::Format(TEXT("if (!{0})\n{\n{1}\n}\n"), { BreakName, Increment });
    }
    if (LastIndex.Preparations.IsEmpty())
    {
        Content += FString::Format(TEXT("for ({0} = {1}; {2};)\n{\n{3}\n{4}\n}\n"), { IndexName, FirstIndex.Code, Condition, BodyCode, Increment });
    }
    else
    {
        FString CheckCode;
        if (bHasBreak)
        {
            CheckCode = FString::Format(TEXT("if ({0})\n{\nbreak;\n}\n"), {BreakName});
        }
        CheckCode += FString::Join(LastIndex.Preparations.Array(), TEXT("\n"));
        CheckCode += FString::Format(TEXT("\nif (!({0} <= {1}))\n{\nbreak;\n}\n"), {IndexName, LastIndex.Code});
        Content += FString::Format(TEXT("for ({0} = {1}; ;)\n{\n{2}\n{3}\n{4}\n}\n"), {IndexName, FirstIndex.Code, CheckCode, BodyCode, Increment});
    }
    if (UEdGraphPin* Completed = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Node->FindPin(TEXT("Completed")), 0, false, MacroStack, Subsystem))
    {
        Content += Subsystem->GenerateCodeFromNode(Cast<UK2Node>(Completed->GetOwningNode()), Completed->GetName(), VisitedNodes, Preparations, MacroStack);
    }
    return Content;
}

bool UForLoopTranslatorObject::CanGenerateOutPinVariable(UEdGraphPin* Pin, UNativizationV2Subsystem* Subsystem)
{
    return false;
}

bool UForLoopTranslatorObject::CanApply(UK2Node* Node)
{
    UK2Node_MacroInstance* Macro = Cast<UK2Node_MacroInstance>(Node);
    return Macro && Macro->GetMacroGraph() &&
        Macro->GetMacroGraph()->GetOutermost()->GetName() == TEXT("/Engine/EditorBlueprintResources/StandardMacros") && Super::CanApply(Node);
}
