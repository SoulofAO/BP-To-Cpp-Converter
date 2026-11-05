/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright 2026 AO All Rights Reserved.
 */

#include "TranslatorObjects/SpecializedStructTranslators.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSettings.h"
#include "BlueprintNativizationSubsystem.h"
#include "Engine/Engine.h"
#include "RetainedBlueprintGeneration.h"
#include "StructInitialization.h"
#include "UObject/UnrealType.h"

bool UExternalBlueprintStructTranslator::CanTranslate(const UScriptStruct* Structure) const
{
    return FRetainedBlueprintGeneration::IsExternalStructure(Structure);
}

bool UExternalBlueprintStructTranslator::TranslateSerializedFallback(const FStructTranslationRequest& Request, FStructTranslationResult& Result) const
{
    Result.Error = TEXT("External Blueprint structure initialization must use its original reflected type.");
    return false;
}

bool UExternalBlueprintStructTranslator::Translate(const FStructTranslationRequest& Request, FStructTranslationResult& Result) const
{
    if (FStructInitialization::Describe(Request, Result, TEXT("FInstancedStruct"), TEXT("StructUtils/InstancedStruct.h"), TEXT("CoreUObject")))
    {
        return true;
    }
    Result.HeaderIncludes.Add(TEXT("UObject/UnrealType.h"));
    const FString StructureExpression = FRetainedBlueprintGeneration::GetAssetValue(const_cast<UScriptStruct*>(Request.Structure), UScriptStruct::StaticClass());
    if (Request.Operation == EStructTranslationOperation::ReadMember || Request.Operation == EStructTranslationOperation::WriteMember)
    {
        FProperty* Property = Request.Structure->FindPropertyByName(Request.MemberName);
        if (!Property)
        {
            Result.Error = TEXT("Cannot resolve an external Blueprint structure member: ") + Request.MemberName.ToString();
            return false;
        }
        const FString PropertyExpression = TEXT("FindFProperty<FProperty>(StructureType, ") + FStructInitialization::Quote(Property->GetName()) + TEXT(")");
        const FString MemberType = UBlueprintNativizationLibrary::GetPropertyType(Property);
        if (Request.Operation == EStructTranslationOperation::WriteMember)
        {
            if (Request.Arguments.Num() != 1)
            {
                Result.Error = TEXT("External Blueprint structure member assignment requires one value.");
                return false;
            }
            const FString TargetName = FStructInitialization::VariableName(Request, TEXT("ExternalStructureTarget"));
            Result.Expression = TEXT("{\nauto&& ") + TargetName + TEXT(" = ") + Request.SourceExpression + TEXT(";\nUScriptStruct* StructureType = ") + StructureExpression +
                TEXT(";\ncheck(StructureType);\nif (!") + TargetName + TEXT(".IsValid())\n{\n") + TargetName + TEXT(".InitializeAs(StructureType);\n}\ncheck(") + TargetName + TEXT(".GetScriptStruct() == StructureType);\nFProperty* MemberProperty = ") +
                PropertyExpression + TEXT(";\ncheck(MemberProperty);\nvoid* MemberAddress = MemberProperty->ContainerPtrToValuePtr<void>(") + TargetName + TEXT(".GetMutableMemory());\n") + MemberType + TEXT(" const MemberValue = ") +
                Request.Arguments[0].Expression + TEXT(";\n") + FRetainedBlueprintGeneration::Transfer(Property, TEXT("MemberProperty"), TEXT("MemberAddress"), TEXT("MemberValue"), true) + TEXT("}");
            return true;
        }
        Result.Expression = TEXT("([&](const FInstancedStruct& StructureValue)\n{\nUScriptStruct* StructureType = ") + StructureExpression +
            TEXT(";\ncheck(StructureType);\nFInstancedStruct DefaultValue;\nconst FInstancedStruct* StructureStorage = &StructureValue;\nif (!StructureValue.IsValid())\n{\nDefaultValue.InitializeAs(StructureType);\nStructureStorage = &DefaultValue;\n}\ncheck(StructureStorage->GetScriptStruct() == StructureType);\nFProperty* MemberProperty = ") +
            PropertyExpression + TEXT(";\ncheck(MemberProperty);\nconst void* MemberAddress = MemberProperty->ContainerPtrToValuePtr<void>(StructureStorage->GetMemory());\n") + MemberType + TEXT(" MemberValue{};\n") +
            FRetainedBlueprintGeneration::Transfer(Property, TEXT("MemberProperty"), TEXT("MemberAddress"), TEXT("MemberValue"), false) + TEXT("return MemberValue;\n})(") + Request.SourceExpression + TEXT(")");
        return true;
    }
    if (!FStructInitialization::IsValueOperation(Request.Operation) && Request.Operation != EStructTranslationOperation::Construction)
    {
        Result.Error = TEXT("Unsupported external Blueprint structure operation.");
        return false;
    }
    const FString ValueName = FStructInitialization::VariableName(Request, TEXT("ExternalStructureValue"));
    Result.Preparations.Add(TEXT("FInstancedStruct ") + ValueName + TEXT(";"));
    Result.Preparations.Add(ValueName + TEXT(".InitializeAs(") + StructureExpression + TEXT(");"));
    TArray<FStructTranslationArgument> Arguments = Request.Arguments;
    if (FStructInitialization::IsValueOperation(Request.Operation))
    {
        FStructOnScope Storage(Request.Structure);
        const void* Source = FStructInitialization::ReadValue(Request, Storage, Result.Error);
        if (!Source)
        {
            return false;
        }
        for (TFieldIterator<FProperty> Member(Request.Structure); Member; ++Member)
        {
            const void* MemberAddress = Member->ContainerPtrToValuePtr<void>(Source);
            Arguments.Add({Member->GetFName(), FStructInitialization::PropertyExpression(*Member, MemberAddress, Request), {}});
        }
    }
    for (const FStructTranslationArgument& Argument : Arguments)
    {
        FStructTranslationRequest Assignment = Request;
        Assignment.Operation = EStructTranslationOperation::WriteMember;
        Assignment.SourceExpression = ValueName;
        Assignment.MemberName = Argument.Name;
        Assignment.Arguments = {Argument};
        FStructTranslationResult AssignmentResult;
        if (!Translate(Assignment, AssignmentResult))
        {
            Result.Error = AssignmentResult.Error;
            return false;
        }
        Result.Preparations.Add(AssignmentResult.Expression);
    }
    Result.Expression = ValueName;
    return true;
}
