/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/PromotabaleOperatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"

FGenerateResultStruct UPromotabaleOperatorTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	if (UK2Node_PromotableOperator* PromotableOperator = Cast<UK2Node_PromotableOperator>(Node))
	{
		TArray<UEdGraphPin*> Pins = UBlueprintNativizationLibrary::GetFilteredPins(Node, EPinOutputOrInputFilter::Input, EPinExcludeFilter::None, EPinIncludeOnlyFilter::None);

		if (Pins.Num() == 0)
		{
			return FString();
		}

		UEdGraphPin* MainPin = Pins[0];
		Pins.RemoveAt(0);
		const FName OperationName = PromotableOperator->GetOperationName();

		const TMap<FName, FString> OperatorMap = {
			{ "Add", "+" },
			{ "Subtract", "-" },
			{ "Multiply", "*" },
			{ "Divide", "/" },
			{ "EqualEqual", "==" },
			{ "NotEqual", "!=" },
			{ "Greater", ">" },
			{ "GreaterEqual", ">=" },
			{ "Less", "<" },
			{ "LessEqual", "<=" },
			{ "And", "&&" },
			{ "Or", "||" },
			{ "Xor", "^" },
			{ "Modulus", "%" },
			{ "LeftShift", "<<" },
			{ "RightShift", ">>" }
		};

		if (const FString* Operator = OperatorMap.Find(OperationName))
		{
			TArray<UK2Node*> LocalMacroStack = MacroStack;
			FGenerateResultStruct Result = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, MainPin, 0, LocalMacroStack);

			for (UEdGraphPin* GraphPin : Pins)
			{
				TArray<UK2Node*> LocalGraphPinMacroStack = MacroStack;
				FGenerateResultStruct GraphPinGenerateResult = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, GraphPin, 0, LocalGraphPinMacroStack);
				Result.Code = Result.Code + *Operator + GraphPinGenerateResult.Code;
				Result.Preparations.Append(GraphPinGenerateResult.Preparations);
			}
			return Result;
		}
	}
	return FString();
}
