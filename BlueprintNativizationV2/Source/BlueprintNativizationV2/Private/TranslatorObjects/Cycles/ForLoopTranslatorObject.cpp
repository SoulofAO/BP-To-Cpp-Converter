/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/Cycles/ForLoopTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"

FString UForLoopTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
    FString Content;

    if (UK2Node_MacroInstance* MacroNode = Cast<UK2Node_MacroInstance>(Node))
    {
        FString MacroName = MacroNode->GetMacroGraph() ? MacroNode->GetMacroGraph()->GetName() : "";

        if (MacroName == TEXT("ForLoop") || MacroName == TEXT("ForLoopWithBreak"))
        {
            if (EntryPinName == TEXT("Break"))
            {
                if (FindLastForLoopInStackNode(VisitedNodes) == MacroNode)
                {
                    return TEXT("break;\n");
                }
                else
                {
                    UE_LOG(LogTemp, Warning, TEXT("SOME STRANGE CIRCLE BREAK USE"));
                    return TEXT("// Invalid break usage");
                }
            }

            FString IndexVar = UBlueprintNativizationLibrary::GetUniquePinName(Node->FindPin(TEXT("Index")), NativizationV2Subsystem->EntryNodes);

            UEdGraphPin* FirstIndexPin = MacroNode->FindPin(TEXT("FirstIndex"));
            UEdGraphPin* LastIndexPin = MacroNode->FindPin(TEXT("LastIndex"));
            UEdGraphPin* LoopBodyPin = MacroNode->FindPin(TEXT("LoopBody"));

            FString FirstIndex = NativizationV2Subsystem->GenerateInputParameterCodeForNode(MacroNode, FirstIndexPin, 0, MacroStack);
            FString LastIndex = NativizationV2Subsystem->GenerateInputParameterCodeForNode(MacroNode, LastIndexPin, 0, MacroStack);

            FString BodyCode;
            if (LoopBodyPin && LoopBodyPin->LinkedTo.Num() > 0)
            {
                UK2Node* BodyNode = Cast<UK2Node>(LoopBodyPin->LinkedTo[0]->GetOwningNode());
                FString BodyPinName = LoopBodyPin->LinkedTo[0]->GetName();

                BodyCode = NativizationV2Subsystem->GenerateCodeFromNode(BodyNode, BodyPinName, VisitedNodes, MacroStack);
            }

            Content += FString::Format(TEXT("for (int32 {0} = {1}; {0} <= {2}; {0}++)\n{\n{3}\n}"),
                { *IndexVar, *FirstIndex, *LastIndex, *BodyCode });
        }
    }

    return Content;
}

bool UForLoopTranslatorObject::CanGenerateOutPinVariable(UEdGraphPin* Pin, UNativizationV2Subsystem* NativizationV2Subsystem)
{
    return false;
}

UK2Node* UForLoopTranslatorObject::FindLastForLoopInStackNode(const TArray<FVisitedNodeStack>& VisitedNodes)
{
    for (int32 i = VisitedNodes.Num() - 1; i >= 0; --i)
    {
        const FVisitedNodeStack& StackNode = VisitedNodes[i];
        if (UK2Node_MacroInstance* MacroInstance = Cast<UK2Node_MacroInstance>(StackNode.Node))
        {
            if (StackNode.Node && NodeNamesForCheck.Contains(MacroInstance->GetMacroGraph()->GetName()))
            {
                return StackNode.Node;
            }
        }
    }
    return nullptr;
}
