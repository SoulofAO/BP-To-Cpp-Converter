/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/CallParentTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"

FString UCallParentTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	if (UK2Node_CallParentFunction* CallParentFunction = Cast<UK2Node_CallParentFunction>(Node))
	{
		UFunction* Function = CallParentFunction->GetTargetFunction();
		if (Function)
		{
			FString Args;
			for (TFieldIterator<FProperty> ParamIt(Function); ParamIt && (ParamIt->PropertyFlags & CPF_Parm); ++ParamIt)
			{
				UEdGraphPin* Pin = UBlueprintNativizationLibrary::GetPinByName(CallParentFunction->Pins, ParamIt->GetNameCPP());

				if (Pin && Pin->Direction == EGPD_Input)
				{
					FString Arg = NativizationV2Subsystem->GenerateInputParameterCodeForNode(CallParentFunction, Pin, 0, MacroStack);
					if (!Arg.IsEmpty())
					{
						Args += Arg + ", ";
					}
				}
				else if (Pin)
				{
					Args += UBlueprintNativizationLibrary::GetUniquePinName(Pin, NativizationV2Subsystem->EntryNodes) + ", ";
				}
			}


			Args = UBlueprintNativizationLibrary::TrimAfterLastComma(Args);

			return FString::Format(TEXT("Super::{0}({1});"), { UBlueprintNativizationLibrary::GetUniqueFunctionName(CallParentFunction->GetTargetFunction(), ""), Args});
		}

	}
	return FString();
}
