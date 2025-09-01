/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/PromotabaleOperatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"

FString UPromotabaleOperatorTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
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
		const TArray<UEdGraphPin*> OtherPins = Pins;

		const FName OperationName = PromotableOperator->GetOperationName();

		auto GenerateCode = [&](const FString& Operator) -> FString
			{
				TArray<UK2Node*> LocalMacroStack = MacroStack;
				FString Result = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, MainPin, 0, LocalMacroStack);
				for (UEdGraphPin* GraphPin : OtherPins)
				{
					TArray<UK2Node*> LocalGraphPinMacroStack = MacroStack;
					Result += Operator + NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, GraphPin, 0, LocalGraphPinMacroStack);
				}
				return Result;
			};

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
			return GenerateCode(*Operator);
		}
	}
	return FString();
}
