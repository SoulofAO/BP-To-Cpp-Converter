/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright 2025 AO All Rights Reserved.
 */

#pragma once
#include "TranslatorObjects/VariableGetTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"
#include "EdGraphSchema_K2.h"

/**
 * 
 */

bool UVariableGetTranslatorObject::CanApply(UK2Node* Node)
{
    if (UK2Node_VariableGet* Getter = Cast<UK2Node_VariableGet>(Node))
    {
        FProperty* Property = Getter->GetPropertyForVariable();
        return true;
    }
    return Node->GetClass() == UK2Node_StructMemberGet::StaticClass();
}

FGenerateResultStruct UVariableGetTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	if (UK2Node_VariableGet* VariableGet = Cast<UK2Node_VariableGet>(Node))
	{
        if (!VariableGet->IsNodePure())
        {
            return UBlueprintNativizationLibrary::GetUniquePinName(VariableGet->GetValuePin(), NativizationV2Subsystem->EntryNodes);
        }
        return UBlueprintNativizationLibrary::GenerateVariableAccess(VariableGet, MacroStack, NativizationV2Subsystem);
	}
    if (UK2Node_StructMemberGet* Getter = Cast<UK2Node_StructMemberGet>(Node))
    {
        return UBlueprintNativizationLibrary::GetUniquePropertyName(Getter->GetPropertyForVariable()) +
            TEXT(".") + UBlueprintNativizationLibrary::GetUniquePropertyName(Getter->StructType->FindPropertyByName(Pin->PinName));
    }
	return FGenerateResultStruct("");
}

FString UVariableGetTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName,
    TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations,
    UNativizationV2Subsystem* Subsystem)
{
    const FGenerateResultStruct Value = UBlueprintNativizationLibrary::GenerateVariableAccess(CastChecked<UK2Node_VariableGet>(Node), MacroStack, Subsystem);
    FString Content = GenerateNewPreparations(Preparations, Value.Preparations);
    Preparations.Append(Value.Preparations);
    const FString Result = UBlueprintNativizationLibrary::GetUniquePinName(CastChecked<UK2Node_VariableGet>(Node)->GetValuePin(), Subsystem->EntryNodes);
    if (!UBlueprintNativizationLibrary::CheckAnySubPinLinked(CastChecked<UK2Node_VariableGet>(Node)->GetValuePin()))
    {
        Content += TEXT("const auto ");
    }
    Content += Result + TEXT(" = ") + Value.Code + TEXT(";\n");
    FString Condition = TEXT("IsValid(") + Result + TEXT(")");
    const FName Category = CastChecked<UK2Node_VariableGet>(Node)->GetValuePin()->PinType.PinCategory;
    if (Category == UEdGraphSchema_K2::PC_SoftObject || Category == UEdGraphSchema_K2::PC_SoftClass)
    {
        Condition = Result + TEXT(".IsValid()");
    }
    else if (Category == UEdGraphSchema_K2::PC_Boolean)
    {
        Condition = Result;
    }
    Content += TEXT("if (") + Condition + TEXT(")\n{\n");
    for (UEdGraphPin* Output : UBlueprintNativizationLibrary::GetFilteredPins(Node, EPinOutputOrInputFilter::Ouput, EPinExcludeFilter::None, EPinIncludeOnlyFilter::ExecPin))
    {
        if (Output->PinName == UEdGraphSchema_K2::PN_Else)
        {
            continue;
        }
        TArray<UK2Node*> Path = MacroStack;
        if (UEdGraphPin* Next = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Output, 0, false, Path, Subsystem))
        {
            Content += Subsystem->GenerateCodeFromNode(CastChecked<UK2Node>(Next->GetOwningNode()), Next->GetName(), VisitedNodes, Preparations, Path);
        }
    }
    Content += TEXT("}\nelse\n{\n");
    TArray<UK2Node*> Path = MacroStack;
    if (UEdGraphPin* Next = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Node->FindPin(UEdGraphSchema_K2::PN_Else), 0, false, Path, Subsystem))
    {
        Content += Subsystem->GenerateCodeFromNode(CastChecked<UK2Node>(Next->GetOwningNode()), Next->GetName(), VisitedNodes, Preparations, Path);
    }
    return Content + TEXT("}\n");
}
