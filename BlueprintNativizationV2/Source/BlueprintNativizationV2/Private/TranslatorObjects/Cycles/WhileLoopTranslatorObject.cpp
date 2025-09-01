/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/Cycles/WhileLoopTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"

FString UWhileTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
    if (UK2Node_MacroInstance* MacroNode = Cast<UK2Node_MacroInstance>(Node))
    {
        UEdGraph* MacroGraph = MacroNode->GetMacroGraph();

        FString ConditionExpr = NativizationV2Subsystem->GenerateInputParameterCodeForNode(MacroNode, UBlueprintNativizationLibrary::GetPinByName(Node->Pins, "Condition"),0, MacroStack);

        UEdGraphPin* BodyPin = MacroNode->FindPin(TEXT("LoopBody"));
        UEdGraphPin* EndPin = MacroNode->FindPin(TEXT("Completed"));

        FString LoopBodyCode = "";
        FString LoopEndCode = "";

        if (BodyPin->LinkedTo.Num() > 0 && BodyPin->LinkedTo[0])
        {
            UK2Node* BodyNode = Cast<UK2Node>(BodyPin->LinkedTo[0]->GetOwningNode());
            LoopBodyCode = *NativizationV2Subsystem->GenerateCodeFromNode(BodyNode, BodyPin->LinkedTo[0]->GetName(), VisitedNodes, MacroStack);
        }
        if (EndPin->LinkedTo.Num() > 0 && EndPin->LinkedTo[0])
        {
            UK2Node* EndNode = Cast<UK2Node>(EndPin->LinkedTo[0]->GetOwningNode());
            LoopEndCode = *NativizationV2Subsystem->GenerateCodeFromNode(EndNode, BodyPin->LinkedTo[0]->GetName(), VisitedNodes, MacroStack);
        }

        return FString::Format(TEXT("while ({0})\n{\n{1}\n}\n {2}\n"),  {*ConditionExpr, *LoopBodyCode, *LoopEndCode});
    }
    return "";
}
