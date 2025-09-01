/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/MakeSetTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"

FString UMakeSetTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	if (UK2Node_MakeSet* MakeSetNode = Cast<UK2Node_MakeSet>(Node))
	{
		FName ElementCppType = *UBlueprintNativizationLibrary::GetPinType(Pin->PinType, false);
		TArray<FString> Elements;

		for (UEdGraphPin* ElementPin : MakeSetNode->Pins)
		{
			if (ElementPin->Direction == EGPD_Input)
			{
				Elements.Add(NativizationV2Subsystem->GenerateInputParameterCodeForNode(MakeSetNode, ElementPin, 0, MacroStack));
			}
		}

		const FString ElementsJoined = FString::Join(Elements, TEXT(", "));
		const FString ArrayLiteral = FString::Format(TEXT("{0}{{1} }"),
			{
				ElementCppType.ToString(),
				ElementsJoined
			});

		return ArrayLiteral;
	}
	return "";
}
