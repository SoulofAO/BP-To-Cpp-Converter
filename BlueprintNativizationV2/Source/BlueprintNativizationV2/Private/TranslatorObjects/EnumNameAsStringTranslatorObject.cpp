// Fill out your copyright notice in the Description page of Project Settings.


#include "TranslatorObjects/EnumNameAsStringTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSettings.h"

FGenerateResultStruct UEnumNameAsStringTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	if (UK2Node_GetEnumeratorNameAsString* EnumeratorNameAsString = Cast<UK2Node_GetEnumeratorNameAsString>(Node))
	{
		TArray<UEdGraphPin*> Pins = UBlueprintNativizationLibrary::GetFilteredPins(Node, EPinOutputOrInputFilter::Input, EPinExcludeFilter::None, EPinIncludeOnlyFilter::None);

		if (Pins.IsValidIndex(0))
		{
			UEnum* Enum = Cast<UEnum>(Pins[0]->PinType.PinSubCategoryObject);
			FGenerateResultStruct InputResultStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, Pins[0], 0, MacroStack);

			return FGenerateResultStruct(FString::Format(TEXT("StaticEnum<{0}>()->GetNameStringByValue(static_cast<int64>({1}))"),
				{ UBlueprintNativizationLibrary::GetUniqueFieldName(Enum), InputResultStruct.Code }), InputResultStruct.Preparations);
		}
	}

	return FGenerateResultStruct();
}
