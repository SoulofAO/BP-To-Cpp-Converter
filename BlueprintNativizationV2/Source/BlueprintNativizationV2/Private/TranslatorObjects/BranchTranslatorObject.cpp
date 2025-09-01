/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/BranchTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"

FString UBranchTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	FString Content;
	UK2Node_IfThenElse* IfNode = Cast<UK2Node_IfThenElse>(Node);
	if (!IfNode) return Content;

	FString ConditionExpr = TEXT("/*Condition*/true");
	if (IfNode->GetConditionPin())
	{
		TArray<UK2Node*> CopyMacroStack = MacroStack;
		ConditionExpr = NativizationV2Subsystem->GenerateInputParameterCodeForNode(IfNode, IfNode->GetConditionPin(), 0, CopyMacroStack);
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
				*ConditionExpr,
				*NativizationV2Subsystem->GenerateCodeFromNode(ThenNode, ThenPinName, VisitedNodes, ThenMacroStack),
				*NativizationV2Subsystem->GenerateCodeFromNode(ElseNode, ElsePinName, VisitedNodes, ElseMacroStack)
			});
	}
	else if (ThenNode)
	{
		Content += FString::Format(TEXT("if ({0})\n{\n{1}\n}\n"),
			{
				*ConditionExpr,
				*NativizationV2Subsystem->GenerateCodeFromNode(ThenNode, ThenPinName, VisitedNodes, ThenMacroStack)
			});
	}
	else if (ElseNode)
	{
		Content += FString::Format(TEXT("if (!({0}))\n{\n{1}\n}\n"),
			{
				*ConditionExpr,
				*NativizationV2Subsystem->GenerateCodeFromNode(ElseNode, ElsePinName, VisitedNodes, ElseMacroStack)
			});
	}
	return Content;
}
