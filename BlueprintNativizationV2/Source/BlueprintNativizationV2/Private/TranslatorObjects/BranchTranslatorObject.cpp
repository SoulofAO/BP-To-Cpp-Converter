/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright 2025 AO All Rights Reserved.
 */

#include "TranslatorObjects/BranchTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "Kismet/KismetSystemLibrary.h"

FString UBranchTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	FString Content;
	UK2Node_IfThenElse* IfNode = Cast<UK2Node_IfThenElse>(Node);
	if (!IfNode) return Content;

	FGenerationPreparations LocalPreparations = Preparations;
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
    UK2Node* ValidatedCall = nullptr;
    FString ValidatedReceiver;
    TArray<UK2Node*> ConditionStack = MacroStack;
    UEdGraphPin* ConditionSource = UBlueprintNativizationLibrary::GetFirstNonKnotPin(IfNode->GetConditionPin(), 0, false, ConditionStack, NativizationV2Subsystem);
    if (ConditionSource && ThenNode && ThenNode->IsA<UK2Node_CallFunction>())
    {
        if (UK2Node_CallFunction* ValidityCall = Cast<UK2Node_CallFunction>(ConditionSource->GetOwningNode()))
        {
            UFunction* ValidityFunction = ValidityCall->GetTargetFunction();
            if (ValidityFunction && ValidityFunction->GetOwnerClass() == UKismetSystemLibrary::StaticClass() && ValidityFunction->GetFName() == TEXT("IsValid"))
            {
                UEdGraphPin* ObjectSource = UBlueprintNativizationLibrary::GetFirstNonKnotPin(ValidityCall->FindPin(TEXT("Object")), 0, false, ConditionStack, NativizationV2Subsystem);
                if (ObjectSource)
                {
                    if (UK2Node_VariableGet* Getter = Cast<UK2Node_VariableGet>(ObjectSource->GetOwningNode()))
                    {
                        UEdGraphPin* ReceiverPin = Getter->FindPin(UEdGraphSchema_K2::PN_Self);
                        FProperty* Property = UBlueprintNativizationLibrary::ResolveVariableProperty(Getter);
                        if (Property && Getter->VariableReference.IsSelfContext() && (!ReceiverPin || ReceiverPin->LinkedTo.IsEmpty()))
                        {
                            ValidatedReceiver = UBlueprintNativizationLibrary::GetUniquePropertyName(Property);
                            ValidatedCall = ThenNode;
                        }
                    }
                }
            }
        }
    }
    const auto GenerateThenCode = [&]()
    {
        TGuardValue<UK2Node*> ValidatedNodeScope(NativizationV2Subsystem->ValidatedCallNode, ValidatedCall);
        TGuardValue<FString> ValidatedReceiverScope(NativizationV2Subsystem->ValidatedCallReceiver, ValidatedReceiver);
        return NativizationV2Subsystem->GenerateCodeFromNode(ThenNode, ThenPinName, VisitedNodes, LocalPreparations, ThenMacroStack);
    };

	if (ThenNode && ElseNode)
	{
        UEdGraphPin* CommonTail = NativizationV2Subsystem->FindCommonExecutionTail(ThenPin, ElsePin);
        FString ThenCode;
        FString ElseCode;
        TSet<UK2Node*> DeferredNodes = NativizationV2Subsystem->DeferredExecutionNodes;
        if (CommonTail)
        {
            DeferredNodes.Add(CastChecked<UK2Node>(CommonTail->GetOwningNode()));
        }
        {
            TGuardValue<TSet<UK2Node*>> DeferredScope(NativizationV2Subsystem->DeferredExecutionNodes, MoveTemp(DeferredNodes));
            ThenCode = GenerateThenCode();
            ElseCode = NativizationV2Subsystem->GenerateCodeFromNode(ElseNode, ElsePinName, VisitedNodes, LocalPreparations, ElseMacroStack);
        }
        if (ThenCode.TrimStartAndEnd().IsEmpty())
        {
            Content += FString::Format(TEXT("if (!({0}))\n{\n{1}\n}\n"), {ConditionExpiration, ElseCode});
        }
        else if (ElseCode.TrimStartAndEnd().IsEmpty())
        {
            Content += FString::Format(TEXT("if ({0})\n{\n{1}\n}\n"), {ConditionExpiration, ThenCode});
        }
        else
        {
		Content += FString::Format(TEXT("if ({0})\n{\n{1}\n}\nelse\n{\n{2}\n}\n"),
			{
				*ConditionExpiration,
                ThenCode,
                ElseCode
			});
        }
        if (CommonTail)
        {
            Content += NativizationV2Subsystem->GenerateCodeFromNode(CastChecked<UK2Node>(CommonTail->GetOwningNode()),
                CommonTail->GetName(), VisitedNodes, LocalPreparations, MacroStack);
        }
	}
	else if (ThenNode)
	{
		Content += FString::Format(TEXT("if ({0})\n{\n{1}\n}\n"),
			{
				*ConditionExpiration,
                GenerateThenCode()
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
