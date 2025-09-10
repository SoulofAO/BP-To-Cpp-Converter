/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/BranchTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"

FString UBranchTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, TSet<FString>& Preparations, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	FString Content;
	UK2Node_IfThenElse* IfNode = Cast<UK2Node_IfThenElse>(Node);
	if (!IfNode) return Content;

	TSet<FString> LocalPreparations = Preparations;
	FString ConditionExpiration = TEXT("/*Condition*/true");
	if (IfNode->GetConditionPin())
	{
		TArray<UK2Node*> CopyMacroStack = MacroStack;
		FGenerateResultStruct GenerateResultStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(IfNode, IfNode->GetConditionPin(), 0, CopyMacroStack);
		ConditionExpiration = GenerateResultStruct.Code;
		Content += GenerateNewPreparations(Preparations, GenerateResultStruct.Preparations);
		LocalPreparations.Append(GenerateResultStruct.Preparations);
	}

	UEdGraphPin* ThenPin = IfNode->GetThenPin();
	TArray<UK2Node*> ThenMacroStack = MacroStack;
	UEdGraphPin* ThenNonKnotPin = ThenPin ? UBlueprintNativizationLibrary::GetFirstNonKnotPin(ThenPin, 0, false, ThenMacroStack, NativizationV2Subsystem) : nullptr;
	UK2Node* ThenNode = ThenNonKnotPin ? Cast<UK2Node>(ThenNonKnotPin->GetOwningNode()) : nullptr;
	FString ThenPinName = ThenNonKnotPin ? ThenNonKnotPin->GetName() : TEXT("");

	UEdGraphPin* ElsePin = IfNode->GetElsePin();
	TArray<UK2Node*> ElseMacroStack = MacroStack;
	UEdGraphPin* ElseNonKnotPin = ElsePin ? UBlueprintNativizationLibrary::GetFirstNonKnotPin(ElsePin, 0, false, ElseMacroStack, NativizationV2Subsystem) : nullptr;
	UK2Node* ElseNode = ElseNonKnotPin ? Cast<UK2Node>(ElseNonKnotPin->GetOwningNode()) : nullptr;
	FString ElsePinName = ElseNonKnotPin ? ElseNonKnotPin->GetName() : TEXT("");

	if (ThenNode && ElseNode)
	{
		Content += FString::Format(TEXT("if ({0})\n{\n{1}\n}\nelse\n{\n{2}\n}\n"),
			{
				*ConditionExpiration,
				*NativizationV2Subsystem->GenerateCodeFromNode(ThenNode, ThenPinName, VisitedNodes, LocalPreparations, ThenMacroStack),
				*NativizationV2Subsystem->GenerateCodeFromNode(ElseNode, ElsePinName, VisitedNodes, LocalPreparations, ElseMacroStack)
			});
	}
	else if (ThenNode)
	{
		Content += FString::Format(TEXT("if ({0})\n{\n{1}\n}\n"),
			{
				*ConditionExpiration,
				*NativizationV2Subsystem->GenerateCodeFromNode(ThenNode, ThenPinName, VisitedNodes, LocalPreparations, ThenMacroStack)
			});
	}
	else if (ElseNode)
	{
		Content += FString::Format(TEXT("if (!({0}))\n{\n{1}\n}\n"),
			{
				*ConditionExpiration,
				*NativizationV2Subsystem->GenerateCodeFromNode(ElseNode, ElsePinName, VisitedNodes, LocalPreparations, ElseMacroStack)
			});
	}
	return Content;
}
