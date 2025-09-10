/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/CallParentTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"

FString UCallParentTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, TSet<FString>& Preparations, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	if (UK2Node_CallParentFunction* CallParentFunction = Cast<UK2Node_CallParentFunction>(Node))
	{
		UFunction* Function = CallParentFunction->GetTargetFunction();
		if (Function)
		{
			FString Args;
			TSet<FString> NewPreparations;
			for (TFieldIterator<FProperty> ParamIt(Function); ParamIt && (ParamIt->PropertyFlags & CPF_Parm); ++ParamIt)
			{
				UEdGraphPin* Pin = UBlueprintNativizationLibrary::GetPinByName(CallParentFunction->Pins, ParamIt->GetNameCPP());

				if (Pin && Pin->Direction == EGPD_Input)
				{
					FGenerateResultStruct InputResultStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(CallParentFunction, Pin, 0, MacroStack);
					NewPreparations.Append(InputResultStruct.Preparations);

					if (!InputResultStruct.Code.IsEmpty())
					{
						Args += InputResultStruct.Code + ", ";
					}
				}
				else if (Pin)
				{
					Args += UBlueprintNativizationLibrary::GetUniquePinName(Pin, NativizationV2Subsystem->EntryNodes) + ", ";
				}
			}


			Args = UBlueprintNativizationLibrary::TrimAfterLastComma(Args);

			FString Content;
			Content += GenerateNewPreparations(Preparations, NewPreparations);
			Preparations.Append(NewPreparations);

			Content += FString::Format(TEXT("Super::{0}({1});"), { UBlueprintNativizationLibrary::GetUniqueFunctionName(CallParentFunction->GetTargetFunction(), ""), Args});
			return Content;
		}

	}
	return FString();
}
