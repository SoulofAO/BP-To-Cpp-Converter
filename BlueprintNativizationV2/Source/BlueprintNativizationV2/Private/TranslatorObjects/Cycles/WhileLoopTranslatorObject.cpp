/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright 2025 AO All Rights Reserved.
 */

#include "TranslatorObjects/Cycles/WhileLoopTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"
#include "UObject/Package.h"

FString UWhileTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes,
    TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations, UNativizationV2Subsystem* Subsystem)
{
    FGenerateResultStruct Condition;
    {
        TGuardValue<FString> EvaluationScope(Subsystem->InputEvaluationPurpose, TEXT("LoopCondition"));
        TGuardValue<TMap<FString, FGenerateResultStruct>> CallScope(Subsystem->PureFunctionCalls, TMap<FString, FGenerateResultStruct>());
        Condition = Subsystem->GenerateInputParameterCodeForNode(Node, Node->FindPin(TEXT("Condition")), 0, MacroStack);
    }
    FString BodyCode;
    TArray<UK2Node*> BodyStack = MacroStack;
    if (UEdGraphPin* Body = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Node->FindPin(TEXT("LoopBody")), 0, false, BodyStack, Subsystem))
    {
        FGenerationPreparations BodyPreparations = Preparations;
        BodyCode = Subsystem->GenerateCodeFromNode(Cast<UK2Node>(Body->GetOwningNode()), Body->GetName(), VisitedNodes, BodyPreparations, BodyStack);
    }
    FString Content;
    if (Condition.Preparations.IsEmpty())
    {
        Content = FString::Format(TEXT("while ({0})\n{\n{1}\n}\n"), { Condition.Code, BodyCode });
    }
    else
    {
        Content = FString::Format(TEXT("while (true)\n{\n{0}\nif (!({1}))\n{\nbreak;\n}\n{2}\n}\n"),
            {FString::Join(Condition.Preparations.Array(), TEXT("\n")), Condition.Code, BodyCode});
    }
    if (UEdGraphPin* Completed = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Node->FindPin(TEXT("Completed")), 0, false, MacroStack, Subsystem))
    {
        Content += Subsystem->GenerateCodeFromNode(Cast<UK2Node>(Completed->GetOwningNode()), Completed->GetName(), VisitedNodes, Preparations, MacroStack);
    }
    return Content;
}

bool UWhileTranslatorObject::CanApply(UK2Node* Node)
{
    UK2Node_MacroInstance* Macro = Cast<UK2Node_MacroInstance>(Node);
    return Macro && Macro->GetMacroGraph() &&
        Macro->GetMacroGraph()->GetOutermost()->GetName() == TEXT("/Engine/EditorBlueprintResources/StandardMacros") && Super::CanApply(Node);
}
