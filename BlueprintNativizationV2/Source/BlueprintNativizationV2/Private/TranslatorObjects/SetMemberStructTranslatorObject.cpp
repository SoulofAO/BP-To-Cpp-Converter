/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/SetMemberStructTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"


FString USetMemberStructTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, TSet<FString>& Preparations, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	if (UK2Node_SetFieldsInStruct* SetFieldsInStructNode = Cast<UK2Node_SetFieldsInStruct>(Node))
	{
		UEdGraphPin* StructRef = SetFieldsInStructNode->FindPin(TEXT("StructRef"));
		TArray<UEdGraphPin*> Pins = UBlueprintNativizationLibrary::GetFilteredPins(Node, EPinOutputOrInputFilter::Input, EPinExcludeFilter::ExecPin, EPinIncludeOnlyFilter::None);
		Pins.Remove(StructRef);
		FString Content = "";

		SetFieldsInStructNode->StructType;

		FGenerateResultStruct GenerateStructRefResult = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, StructRef, 0, MacroStack);

		TSet<FString> NewPreparations;

		TArray<FGenerateResultStruct> GenerateEdGraphResults;

		for (UEdGraphPin* EdGraphPin : Pins)
		{
			GenerateEdGraphResults.Add(NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, EdGraphPin, 0, MacroStack));
		}
		for (FGenerateResultStruct Result : GenerateEdGraphResults)
		{
			NewPreparations.Append(Result.Preparations);
		}
		Content += GenerateNewPreparations(Preparations, NewPreparations);
		Preparations.Append(NewPreparations);

		for (UEdGraphPin* EdGraphPin : Pins)
		{
			Content += FString::Format(TEXT("{0}.{1} = {2}; \n"), { GenerateStructRefResult.Code,
			EdGraphPin->GetName(), NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, EdGraphPin, 0, MacroStack).Code});
		}

		return Content;
	}
	return FString();
}
