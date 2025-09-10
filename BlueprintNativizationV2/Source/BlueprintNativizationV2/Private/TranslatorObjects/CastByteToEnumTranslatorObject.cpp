// Fill out your copyright notice in the Description page of Project Settings.


#include "TranslatorObjects/CastByteToEnumTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSettings.h"

FGenerateResultStruct UCastByteToEnumTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	if (UK2Node_CastByteToEnum* CastByteToEnum = Cast<UK2Node_CastByteToEnum>(Node))
	{
		TArray<UEdGraphPin*> Pins = UBlueprintNativizationLibrary::GetFilteredPins(Node, EPinOutputOrInputFilter::Input, EPinExcludeFilter::None, EPinIncludeOnlyFilter::None);

		if (Pins.IsValidIndex(0))
		{
			FGenerateResultStruct InputResultStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, Pins[0], 0, MacroStack);
			return FGenerateResultStruct(FString::Format(TEXT("static_cast<{0}>({1})"), 
				{ UBlueprintNativizationLibrary::GetUniqueFieldName(CastByteToEnum->Enum), InputResultStruct.Code }), InputResultStruct.Preparations);
		}
	}

	return FGenerateResultStruct();
}
