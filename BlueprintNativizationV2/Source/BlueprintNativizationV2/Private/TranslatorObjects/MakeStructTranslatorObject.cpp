/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright 2025 AO All Rights Reserved.
 */

#include "TranslatorObjects/MakeStructTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSettings.h"
#include "K2Node_CallFunction.h"
#include "BlueprintNativizationSubsystem.h"
#include "RetainedBlueprintGeneration.h"
#include "EdGraphSchema_K2.h"

bool UMakeStructTranslatorObject::CanApply(UK2Node* Node)
{
    return Super::CanApply(Node);
}

FGenerateResultStruct UMakeStructTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
    if (const FGenerateResultStruct* Cached = NativizationV2Subsystem->PureFunctionCalls.Find(NativizationV2Subsystem->GetEvaluationKey(Node)))
    {
        return *Cached;
    }
    UK2Node_MakeStruct* MakeStruct = CastChecked<UK2Node_MakeStruct>(Node);
    const FString StructType = UBlueprintNativizationLibrary::GetUniqueFieldName(MakeStruct->StructType);
    const FString ValueName = NativizationV2Subsystem->GetNodeStateName(Node, TEXT("StructureValue"), true);
    if (MakeStruct->StructType->HasMetaData(TEXT("HasNativeMake")) &&
        UBlueprintNativizationSettingsLibrary::FindStructTranslator(MakeStruct->StructType))
    {
        FStructTranslationRequest Request;
        Request.Structure = MakeStruct->StructType;
        Request.Operation = EStructTranslationOperation::Construction;
        FGenerationPreparations Preparations;
        for (UEdGraphPin* Input : Node->Pins)
        {
            if (Input->Direction != EGPD_Input || Input->ParentPin || Input->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
            {
                continue;
            }
            const FGenerateResultStruct Value = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, Input, 0, MacroStack);
            Request.Arguments.Add({Input->PinName, Value.Code, UBlueprintNativizationLibrary::GetPinType(Input->PinType, false)});
            Preparations.Append(Value.Preparations);
        }
        const FString Expression = UBlueprintNativizationSettingsLibrary::TranslateStructure(Request, &Preparations);
        Preparations.Add(TEXT("auto ") + ValueName + TEXT(" = ") + Expression + TEXT(";\n"));
        const FGenerateResultStruct Result(ValueName, Preparations);
        NativizationV2Subsystem->PureFunctionCalls.Add(NativizationV2Subsystem->GetEvaluationKey(Node), Result);
        return Result;
    }
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
    auto WriteMember = [&](FProperty* Property, const FString& Expression, FEdGraphPinType PinType)
    {
        if (UBlueprintNativizationSettingsLibrary::FindStructTranslator(MakeStruct->StructType))
        {
            FStructTranslationRequest Request;
            Request.Structure = MakeStruct->StructType;
            Request.Operation = EStructTranslationOperation::WriteMember;
            Request.MemberName = Property->GetFName();
            Request.SourceExpression = ValueName;
            Request.Arguments.Add({Property->GetFName(), Expression, UBlueprintNativizationLibrary::GetPinType(PinType, false)});
            FGenerationPreparations MemberPreparations;
            const FString MemberAssignment = UBlueprintNativizationSettingsLibrary::TranslateStructure(Request, &MemberPreparations);
            Code += FString::Join(MemberPreparations.Array(), TEXT("")) + MemberAssignment + TEXT(";\n");
        }
        else
        {
            Code += ValueName + TEXT(".") + UBlueprintNativizationLibrary::GetUniquePropertyName(Property) + TEXT(" = ") + Expression + TEXT(";\n");
        }
    };
    TMap<FBoolProperty*, bool> Overrides;
    const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
    for (UEdGraphPin* Input : Inputs)
    {
        if (Input->bOrphanedPin || Schema->IsMetaPin(*Input))
        {
            continue;
        }
        FProperty* Property = MakeStruct->StructType->FindPropertyByName(Input->PinName);
        if (!Property)
        {
            continue;
        }
        if (!Input->bNotConnectable)
        {
            const FGenerateResultStruct Value = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, Input, 0, MacroStack);
            Preparations.Append(Value.Preparations);
            WriteMember(Property, Value.Code, Input->PinType);
        }
        FBoolProperty* Override = MakeStruct->GetOverrideConditionForProperty(Property);
        if (Override && Override->HasAllPropertyFlags(CPF_BlueprintVisible) && !Override->HasAnyPropertyFlags(CPF_BlueprintReadOnly))
        {
            const FOptionalPinFromProperty* Entry = MakeStruct->ShowPinForProperties.FindByPredicate(
                [Property](const FOptionalPinFromProperty& Candidate)
                {
                    return Candidate.PropertyName == Property->GetFName() && Candidate.bHasOverridePin && Candidate.bShowPin;
                });
            if (Entry && Entry->bIsOverridePinVisible)
            {
                Overrides.Add(Override, Entry->bIsOverrideEnabled);
            }
        }
    }
    for (const TPair<FBoolProperty*, bool>& Override : Overrides)
    {
        FEdGraphPinType PinType;
        Schema->ConvertPropertyToPinType(Override.Key, PinType);
        FString Expression = TEXT("false");
        if (Override.Value)
        {
            Expression = TEXT("true");
        }
        WriteMember(Override.Key, Expression, PinType);
    }
    Preparations.Append(InitialValuePreparations);
    Preparations.Add(Code);
    const FGenerateResultStruct Result(ValueName, Preparations);
    NativizationV2Subsystem->PureFunctionCalls.Add(NativizationV2Subsystem->GetEvaluationKey(Node), Result);
    return Result;
}
