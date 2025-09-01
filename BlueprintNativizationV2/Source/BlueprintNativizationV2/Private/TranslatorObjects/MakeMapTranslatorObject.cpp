/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/MakeMapTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"

FString UMakeMapTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	if (UK2Node_MakeMap* MakeMapNode = Cast<UK2Node_MakeMap>(Node))
	{
		FName ElementCppType = *UBlueprintNativizationLibrary::GetPinType(Pin->PinType, false);
		TArray<TPair<FString, FString>> Elements;

		TArray<UEdGraphPin*> InputPins = UBlueprintNativizationLibrary::GetFilteredPins(MakeMapNode, EPinOutputOrInputFilter::Input, EPinExcludeFilter::None, EPinIncludeOnlyFilter::None);

		for (int Counter = 0; Counter<MakeMapNode->Pins.Num()/2; Counter++)
		{
			int Index = Counter * 2;
			UEdGraphPin* KeyGraphPin = InputPins[Index];
			UEdGraphPin* ValueGraphPin = InputPins[Index + 1];

			Elements.Add(TPair<FString, FString>(
				NativizationV2Subsystem->GenerateInputParameterCodeForNode(MakeMapNode, KeyGraphPin, 0, MacroStack),
				NativizationV2Subsystem->GenerateInputParameterCodeForNode(MakeMapNode, ValueGraphPin, 0, MacroStack)));
		}

		
		FString ElementsJoined;
		for (TPair<FString, FString> Pair : Elements)
		{
			ElementsJoined += FString::Format(TEXT("{ {0}, {1} }"), { *Pair.Key, *Pair.Value });
		}
		const FString ArrayLiteral = FString::Format(TEXT("{0}{{1}}"),
			{
				ElementCppType.ToString(),
				ElementsJoined
			});

		return ArrayLiteral;
	}
	return "";
}