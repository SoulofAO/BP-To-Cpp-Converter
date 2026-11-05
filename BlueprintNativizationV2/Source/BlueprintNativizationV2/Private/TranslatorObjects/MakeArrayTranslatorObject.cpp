/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright 2025 AO All Rights Reserved.
 */

#include "TranslatorObjects/MakeArrayTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"


FGenerateResultStruct UMakeArrayTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	if (UK2Node_MakeArray* MakeArrayNode = Cast<UK2Node_MakeArray>(Node))
	{
		FName ElementCppType = *UBlueprintNativizationLibrary::GetPinType(Pin->PinType, false);
		TArray<FString> Elements;
		FGenerationPreparations Preparations;

		for (UEdGraphPin* ElementPin : MakeArrayNode->Pins)
		{
			if (ElementPin->Direction == EGPD_Input && !ElementPin->ParentPin && !ElementPin->bOrphanedPin)
			{
				FGenerateResultStruct InputResultStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(MakeArrayNode, ElementPin, 0, MacroStack);

				Preparations.Append(InputResultStruct.Preparations);
				Elements.Add(InputResultStruct.Code);
			}
		}

		const FString ElementsJoined = FString::Join(Elements, TEXT(", "));
		const FString ArrayLiteral = FString::Format(TEXT("{0}{ {1} }"),
			{
				ElementCppType.ToString(),
				ElementsJoined
			});

		return FGenerateResultStruct(ArrayLiteral, Preparations);
	}
	return FGenerateResultStruct();
}
