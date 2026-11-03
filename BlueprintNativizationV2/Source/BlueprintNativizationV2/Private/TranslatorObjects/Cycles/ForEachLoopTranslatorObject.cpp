/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/Cycles/ForEachLoopTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"

FGenerateResultStruct UForEachLoopTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex,
    TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem)
{
    Pin = UBlueprintNativizationLibrary::GetParentPin(Pin);
    if (Pin->PinName != TEXT("Array Element"))
    {
        return UBlueprintNativizationLibrary::GetUniquePinName(Pin, Subsystem->EntryNodes);
    }
    const FGenerateResultStruct Array = Subsystem->GenerateInputParameterCodeForNode(Node, Node->FindPin(TEXT("Array")), 0, MacroStack);
    const FString IndexName = UBlueprintNativizationLibrary::GetUniquePinName(Node->FindPin(TEXT("Array Index")), Subsystem->EntryNodes);
    FString ElementName = Subsystem->GetNodeStateName(Node, TEXT("ArrayElement"), true);
    if (Subsystem->DataConsumer)
    {
        ElementName = Subsystem->GetNodeStateName(Subsystem->DataConsumer, Subsystem->InputEvaluationPurpose + ElementName, true);
    }
    const FString ArrayName = ElementName + TEXT("Values");
    const FString ElementType = UBlueprintNativizationLibrary::GetPinType(Pin->PinType, false);
    FGenerateResultStruct Result(ElementName, Array.Preparations);
    Result.Preparations.Add(FString::Format(TEXT("{0} {1}{};\n{\nconst auto& {2} = {3};\nif ({2}.IsValidIndex({4}))\n{\n{1} = {2}[{4}];\n}\n}\n"),
        { ElementType, ElementName, ArrayName, Array.Code, IndexName }));
    return Result;
}

FString UForEachLoopTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes,
    TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations, UNativizationV2Subsystem* Subsystem)
{
    const FString BreakName = Subsystem->GetNodeStateName(Node, TEXT("BreakRequested"));
    if (EntryPinName == TEXT("Break"))
    {
        return GenerateBreak(Node, Subsystem);
    }
    const bool bHasBreak = Node->FindPin(TEXT("Break")) != nullptr;
    const FString IndexName = UBlueprintNativizationLibrary::GetUniquePinName(Node->FindPin(TEXT("Array Index")), Subsystem->EntryNodes);
    const FString IterationName = Subsystem->GetNodeStateName(Node, TEXT("Iteration"));
    FGenerateResultStruct Array;
    {
        TGuardValue<FString> EvaluationScope(Subsystem->InputEvaluationPurpose, TEXT("LoopCondition"));
        TGuardValue<TMap<FString, FGenerateResultStruct>> CallScope(Subsystem->PureFunctionCalls, TMap<FString, FGenerateResultStruct>());
        Array = Subsystem->GenerateInputParameterCodeForNode(Node, Node->FindPin(TEXT("Array")), 0, MacroStack);
    }
    FString Length = FString::Format(TEXT("({0}).Num()"), { Array.Code });
    FString Condition = IterationName + TEXT(" < ") + Length;
    FString Content = IndexName + TEXT(" = 0;\n");
    if (bHasBreak)
    {
        Content += BreakName + TEXT(" = false;\n");
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
    if (Array.Preparations.IsEmpty())
    {
        Content += FString::Format(TEXT("for (int32 {0} = 0; {1}; ++{0})\n{\n{2} = {0};\n{3}\n}\n"), { IterationName, Condition, IndexName, BodyCode });
    }
    else
    {
        FString CheckCode;
        if (bHasBreak)
        {
            CheckCode = FString::Format(TEXT("if ({0})\n{\nbreak;\n}\n"), {BreakName});
        }
        CheckCode += FString::Join(Array.Preparations.Array(), TEXT("\n"));
        CheckCode += FString::Format(TEXT("\nif (!({0} < {1}))\n{\nbreak;\n}\n"), {IterationName, Length});
        Content += FString::Format(TEXT("for (int32 {0} = 0; ; ++{0})\n{\n{1}\n{2} = {0};\n{3}\n}\n"), {IterationName, CheckCode, IndexName, BodyCode});
    }
    if (UEdGraphPin* Completed = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Node->FindPin(TEXT("Completed")), 0, false, MacroStack, Subsystem))
    {
        Content += Subsystem->GenerateCodeFromNode(Cast<UK2Node>(Completed->GetOwningNode()), Completed->GetName(), VisitedNodes, Preparations, MacroStack);
    }
    return Content;
}
