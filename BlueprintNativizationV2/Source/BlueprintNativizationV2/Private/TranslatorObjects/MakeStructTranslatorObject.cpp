/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/MakeStructTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSettings.h"
#include "K2Node_CallFunction.h"
#include "BlueprintNativizationSubsystem.h"
#include "RetainedBlueprintGeneration.h"

bool UMakeStructTranslatorObject::CanApply(UK2Node* Node)
{
    return Super::CanApply(Node);
}

FGenerateResultStruct UMakeStructTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
    if (const FGenerateResultStruct* Cached = NativizationV2Subsystem->PureFunctionCalls.Find(Node))
    {
        return *Cached;
    }
    UK2Node_MakeStruct* MakeStruct = CastChecked<UK2Node_MakeStruct>(Node);
    const FString StructType = UBlueprintNativizationLibrary::GetUniqueFieldName(MakeStruct->StructType);
    const FString ValueName = NativizationV2Subsystem->GetNodeStateName(Node, TEXT("StructureValue"), true);
    FGenerationPreparations InitialValuePreparations;
    FString Code = StructType + TEXT(" ") + ValueName + TEXT("{};\n");
    if (FRetainedBlueprintGeneration::IsExternalStructure(MakeStruct->StructType))
    {
        FStructTranslationRequest Request;
        Request.Structure = MakeStruct->StructType;
        Request.Operation = EStructTranslationOperation::DefaultValue;
        Code = TEXT("FInstancedStruct ") + ValueName + TEXT(" = ") +
            UBlueprintNativizationSettingsLibrary::TranslateStructure(Request, &InitialValuePreparations) + TEXT(";\n");
    }
    FGenerationPreparations Preparations;
    const TArray<UEdGraphPin*> Inputs = UBlueprintNativizationLibrary::GetParentPins(
        UBlueprintNativizationLibrary::GetFilteredPins(Node, EPinOutputOrInputFilter::Input, EPinExcludeFilter::ExecPin, EPinIncludeOnlyFilter::None));
    for (UEdGraphPin* Input : Inputs)
    {
        FProperty* Property = MakeStruct->StructType->FindPropertyByName(Input->PinName);
        if (!Property)
        {
            continue;
        }
        const FGenerateResultStruct Value = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, Input, 0, MacroStack);
        Preparations.Append(Value.Preparations);
        if (UBlueprintNativizationSettingsLibrary::FindStructTranslator(MakeStruct->StructType))
        {
            FStructTranslationRequest Request;
            Request.Structure = MakeStruct->StructType;
            Request.Operation = EStructTranslationOperation::WriteMember;
            Request.MemberName = Property->GetFName();
            Request.SourceExpression = ValueName;
            Request.Arguments.Add({Property->GetFName(), Value.Code, {}});
            FGenerationPreparations MemberPreparations;
            const FString MemberAssignment = UBlueprintNativizationSettingsLibrary::TranslateStructure(Request, &MemberPreparations);
            Code += FString::Join(MemberPreparations.Array(), TEXT("")) + MemberAssignment + TEXT(";\n");
        }
        else
        {
            Code += ValueName + TEXT(".") + UBlueprintNativizationLibrary::GetUniquePropertyName(Property) + TEXT(" = ") + Value.Code + TEXT(";\n");
        }
    }
    Preparations.Append(InitialValuePreparations);
    Preparations.Add(Code);
    const FGenerateResultStruct Result(ValueName, Preparations);
    NativizationV2Subsystem->PureFunctionCalls.Add(Node, Result);
    return Result;
}
