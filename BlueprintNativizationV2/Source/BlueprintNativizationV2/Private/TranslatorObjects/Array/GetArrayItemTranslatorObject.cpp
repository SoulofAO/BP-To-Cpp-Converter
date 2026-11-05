/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright 2025 AO All Rights Reserved.
 */

#include "TranslatorObjects/Array/GetArrayItemTranslatorObject.h"
#include "RetainedBlueprintGeneration.h"
#include "NativeApiCatalog.h"
#include "K2Node_VariableGet.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"

FGenerateResultStruct UGetArrayItemTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	if (UK2Node_GetArrayItem* GetArrayItemNode = Cast<UK2Node_GetArrayItem>(Node))
	{
		FGenerationPreparations LocalPreparations;

		FGenerateResultStruct ArrayPinResultStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(GetArrayItemNode, GetArrayItemNode->GetTargetArrayPin(), 0, MacroStack);
		FGenerateResultStruct IndexPinResultStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(GetArrayItemNode, GetArrayItemNode->GetIndexPin(), 0, MacroStack);

		LocalPreparations.Append(ArrayPinResultStruct.Preparations);
		LocalPreparations.Append(IndexPinResultStruct.Preparations);

        FEdGraphPinType ElementType = GetArrayItemNode->GetResultPin()->PinType;
        const FString TypeName = UBlueprintNativizationLibrary::GetPinType(ElementType, false);
        FString ReturnType = TypeName;
        FString MissingValue = TEXT("{}");
        const bool bRequiresStorage = ElementType.bIsReference && NativizationV2Subsystem->bRequiresInputStorage;
        if (bRequiresStorage)
        {
            UEdGraphPin* ArrayPin = GetArrayItemNode->GetTargetArrayPin();
            if (!ArrayPin->LinkedTo.IsEmpty())
            {
                if (UK2Node_VariableGet* Variable = Cast<UK2Node_VariableGet>(ArrayPin->LinkedTo[0]->GetOwningNode()))
                {
                    if (FRetainedBlueprintGeneration::IsRetained(Variable->GetPropertyForVariable()->GetOwnerClass()))
                    {
                        FNativeApiCatalog::AddError(TEXT("An element reference cannot outlive a reflected Blueprint array snapshot: ") + Variable->GetPropertyForVariable()->GetPathName());
                    }
                }
            }
            ReturnType += TEXT("*");
            MissingValue = NativizationV2Subsystem->GetNodeStateName(Node, TEXT("MissingArrayItem"));
            if (NativizationV2Subsystem->DataConsumer)
            {
                FString Purpose = MissingValue;
                if (NativizationV2Subsystem->InputEvaluationPurpose != TEXT("Input"))
                {
                    Purpose = NativizationV2Subsystem->InputEvaluationPurpose + Purpose;
                }
                MissingValue = NativizationV2Subsystem->GetNodeStateName(NativizationV2Subsystem->DataConsumer, Purpose);
            }
            LocalPreparations.Add(TypeName + TEXT(" ") + MissingValue + TEXT("{};\n"));
        }
        FString Purpose = TEXT("ArrayItem");
        FString ArrayName = NativizationV2Subsystem->GetNodeStateName(Node, TEXT("ArrayValues"), true);
        FString IndexName = NativizationV2Subsystem->GetNodeStateName(Node, TEXT("ElementIndex"), true);
        if (NativizationV2Subsystem->DataConsumer)
        {
            ArrayName = NativizationV2Subsystem->GetNodeStateName(NativizationV2Subsystem->DataConsumer,
                NativizationV2Subsystem->InputEvaluationPurpose + Purpose + ArrayName, true);
            IndexName = NativizationV2Subsystem->GetNodeStateName(NativizationV2Subsystem->DataConsumer,
                NativizationV2Subsystem->InputEvaluationPurpose + Purpose + IndexName, true);
        }
        FString Access = ArrayName + TEXT("[") + IndexName + TEXT("]");
        FString Fallback = TEXT("{}");
        if (bRequiresStorage)
        {
            Purpose = TEXT("ArrayItemStorage");
            Access = TEXT("&") + Access;
            Fallback = TEXT("&") + MissingValue;
        }
        FString ResultName = NativizationV2Subsystem->GetNodeStateName(Node, Purpose, true);
        if (NativizationV2Subsystem->DataConsumer)
        {
            ResultName = NativizationV2Subsystem->GetNodeStateName(NativizationV2Subsystem->DataConsumer,
                NativizationV2Subsystem->InputEvaluationPurpose + ResultName, true);
        }
        FString Binding = TEXT("auto&& ") + ArrayName + TEXT(" = ") + ArrayPinResultStruct.Code + TEXT(";\n");
        FString Statement = ReturnType + TEXT(" ") + ResultName + TEXT("{};\n");
        if (bRequiresStorage)
        {
            LocalPreparations.Add(Binding);
            Statement += TEXT("{\n");
        }
        else
        {
            Statement += TEXT("{\n") + Binding;
        }
        Statement +=
            TEXT("const int32 ") + IndexName + TEXT(" = ") + IndexPinResultStruct.Code + TEXT(";\nif (") + ArrayName + TEXT(".IsValidIndex(") + IndexName + TEXT("))\n{\n") +
            ResultName + TEXT(" = ") + Access + TEXT(";\n}\n");
        if (bRequiresStorage)
        {
            Statement += TEXT("else\n{\n") + ResultName + TEXT(" = ") + Fallback + TEXT(";\n}\n");
        }
        Statement += TEXT("}\n");
        LocalPreparations.Add(Statement);
        FString Code = ResultName;
        if (bRequiresStorage)
        {
            Code = TEXT("(*") + ResultName + TEXT(")");
        }
        return FGenerateResultStruct(Code, LocalPreparations);
	}
	return FString();
}
