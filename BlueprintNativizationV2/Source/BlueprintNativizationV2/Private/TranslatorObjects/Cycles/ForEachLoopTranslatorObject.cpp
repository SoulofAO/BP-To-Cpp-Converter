/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/Cycles/ForEachLoopTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"

FString UForEachLoopTranslatorObject::GenerateCodeFromNode(UK2Node* Node,
    FString EntryPinName,
    TArray<FVisitedNodeStack> VisitedNodes,
    TArray<UK2Node*> MacroStack,
    UNativizationV2Subsystem* NativizationV2Subsystem)
{
	if (UK2Node_MacroInstance* MacroNode = Cast<UK2Node_MacroInstance>(Node))
	{
		UEdGraph* MacroGraph = MacroNode->GetMacroGraph();
		if (MacroGraph)
		{
			FString MacroName = MacroGraph->GetName();

			if (EntryPinName == "Break")
			{
				if (FindLastForLoopInStackNode(VisitedNodes) == MacroNode)
				{
					return "break; \n" ;
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("SOME STRANGE CIRCLE BREAK USE"));
				}
			}

			UEdGraphPin* ArrayPin = MacroNode->FindPin(TEXT("Array"));
			UEdGraphPin* ArrayElementPin = MacroNode->FindPin(TEXT("Array Element"));
			UEdGraphPin* ArrayIndexPin = MacroNode->FindPin(TEXT("Array Index"));

			UEdGraphPin* LoopBodyPin = MacroNode->FindPin(TEXT("LoopBody"));
			if (LoopBodyPin->LinkedTo.Num() > 0)
			{

				UK2Node* BodyNode = LoopBodyPin && LoopBodyPin->LinkedTo.Num() > 0
					? Cast<UK2Node>(LoopBodyPin->LinkedTo[0]->GetOwningNode()) : nullptr;

				
				return  FString::Format(TEXT("{0}\nfor ({1} {2}: {3})\n{\n{4}\n\n{5}}"),
					{
				    FString::Format(TEXT("{0} {1} = 0;\n\n"), {"int32", UBlueprintNativizationLibrary::GetUniquePinName(ArrayIndexPin, NativizationV2Subsystem->EntryNodes)}),
					ArrayElementPin->PinType.PinCategory.ToString(),
					UBlueprintNativizationLibrary::GetUniquePinName(ArrayElementPin, NativizationV2Subsystem->EntryNodes),
					*NativizationV2Subsystem->GenerateInputParameterCodeForNode(MacroNode, ArrayPin, 0, MacroStack),
					*NativizationV2Subsystem->GenerateCodeFromNode(BodyNode, LoopBodyPin->LinkedTo[0]->GetName(), VisitedNodes, MacroStack),
					FString(UBlueprintNativizationLibrary::GetUniquePinName(ArrayIndexPin, NativizationV2Subsystem->EntryNodes) + "++;")
					});
			}
		}
	}
	return "";
}

bool UForEachLoopTranslatorObject::CanGenerateOutPinVariable(UEdGraphPin* Pin, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	return false;
}

UK2Node* UForEachLoopTranslatorObject::FindLastForLoopInStackNode(const TArray<FVisitedNodeStack>& VisitedNodes)
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
