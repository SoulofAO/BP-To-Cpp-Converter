/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/ContainerLoopTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"
#include "GenerationGraphContext.h"

static UEdGraphPin* GetContainerInput(UK2Node* Node)
{
    if (UK2Node_MapForEach* Loop = Cast<UK2Node_MapForEach>(Node))
    {
        return Loop->GetMapPin();
    }
    return CastChecked<UK2Node_SetForEach>(Node)->GetSetPin();
}

static UEdGraphPin* GetContainerBreakInput(UK2Node* Node)
{
    if (UK2Node_MapForEach* Loop = Cast<UK2Node_MapForEach>(Node))
    {
        return Loop->GetBreakPin();
    }
    return CastChecked<UK2Node_SetForEach>(Node)->GetBreakPin();
}

bool UContainerLoopTranslatorObject::CanCreateCircleWithPin(UK2Node* Node, FString EntryPinName)
{
    return EntryPinName != GetContainerBreakInput(Node)->PinName.ToString();
}

FString UContainerLoopTranslatorObject::GenerateGlobalVariables(UK2Node* Node, FString Prefix, UNativizationV2Subsystem* Subsystem)
{
    if (!FGenerationGraphContext::IsFunctionGraph(Node))
    {
        return TEXT("UPROPERTY(Transient)\nint32 ") + Subsystem->GetNodeStateName(Node, TEXT("ContainerIndex")) + TEXT(" = 0;\n");
    }
    return {};
}

TSet<FString> UContainerLoopTranslatorObject::GenerateLocalVariables(UK2Node* Node, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem)
{
    if (FGenerationGraphContext::IsFunctionGraph(Node))
    {
        return {TEXT("int32 ") + Subsystem->GetNodeStateName(Node, TEXT("ContainerIndex")) + TEXT(" = 0;\n")};
    }
    return {};
}

FGenerateResultStruct UContainerLoopTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin,
    int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem)
{
    FGenerateResultStruct Result = Subsystem->GenerateInputParameterCodeForNode(Node, GetContainerInput(Node), 0, MacroStack);
    const FString Type = UBlueprintNativizationLibrary::GetPinType(Pin->PinType, false);
    FString Expression = TEXT("*Iterator");
    if (UK2Node_MapForEach* Loop = Cast<UK2Node_MapForEach>(Node))
    {
        Expression = TEXT("Iterator.Value()");
        if (Pin == Loop->GetKeyPin())
        {
            Expression = TEXT("Iterator.Key()");
        }
    }
    Result.Code = TEXT("([&]() -> ") + Type + TEXT("\n{\nconst auto& Values = ") + Result.Code +
        TEXT(";\nauto Iterator = Values.CreateConstIterator();\nfor (int32 Position = 0; Iterator && Position < ") + Subsystem->GetNodeStateName(Node, TEXT("ContainerIndex")) +
        TEXT("; ++Position)\n{\n++Iterator;\n}\nif (Iterator)\n{\nreturn ") + Expression + TEXT(";\n}\nreturn {};\n}())");
    return Result;
}

FString UContainerLoopTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName,
    TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations,
    UNativizationV2Subsystem* Subsystem)
{
    const FString Index = Subsystem->GetNodeStateName(Node, TEXT("ContainerIndex"));
    FGenerateResultStruct Container;
    {
        TGuardValue<FString> Purpose(Subsystem->InputEvaluationPurpose, TEXT("LoopCondition"));
        Container = Subsystem->GenerateInputParameterCodeForNode(Node, GetContainerInput(Node), 0, MacroStack);
    }
    if (EntryPinName == GetContainerBreakInput(Node)->PinName.ToString())
    {
        return FString::Join(Container.Preparations.Array(), TEXT("\n")) + Index + TEXT(" = (") + Container.Code + TEXT(").Num() - 1;\n");
    }
    FString Content = TEXT("for (") + Index + TEXT(" = 0; ; ++") + Index + TEXT(")\n{\n") +
        FString::Join(Container.Preparations.Array(), TEXT("\n")) + TEXT("if (") + Index + TEXT(" >= (") + Container.Code + TEXT(").Num())\n{\nbreak;\n}\n");
    UEdGraphPin* Body;
    UEdGraphPin* Completed;
    if (UK2Node_MapForEach* Loop = Cast<UK2Node_MapForEach>(Node))
    {
        Body = Loop->GetForEachPin();
        Completed = Loop->GetCompletedPin();
    }
    else
    {
        UK2Node_SetForEach* SetLoop = CastChecked<UK2Node_SetForEach>(Node);
        Body = SetLoop->GetForEachPin();
        Completed = SetLoop->GetCompletedPin();
    }
    TArray<UK2Node*> BodyPath = MacroStack;
    if (UEdGraphPin* Target = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Body, 0, false, BodyPath, Subsystem))
    {
        Content += Subsystem->GenerateCodeFromNode(CastChecked<UK2Node>(Target->GetOwningNode()), Target->GetName(), VisitedNodes, Preparations, BodyPath);
    }
    Content += TEXT("}\n");
    if (UEdGraphPin* Target = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Completed, 0, false, MacroStack, Subsystem))
    {
        Content += Subsystem->GenerateCodeFromNode(CastChecked<UK2Node>(Target->GetOwningNode()), Target->GetName(), VisitedNodes, Preparations, MacroStack);
    }
    return Content;
}
