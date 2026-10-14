// Fill out your copyright notice in the Description page of Project Settings.


#include "TranslatorObjects/EnumNameAsStringTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSettings.h"
#include "Engine/UserDefinedEnum.h"

FGenerateResultStruct UEnumNameAsStringTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
    UK2Node_GetEnumeratorName* Enumerator = CastChecked<UK2Node_GetEnumeratorName>(Node);
    UEdGraphPin* Input = Enumerator->FindPinChecked(TEXT("Enumerator"));
    UEnum* Enumeration = CastChecked<UEnum>(Input->PinType.PinSubCategoryObject);
    const FGenerateResultStruct Value = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, Input, 0, MacroStack);
    const bool bDisplayName = Enumerator->IsA<UK2Node_GetEnumeratorNameAsString>();
    if (Enumeration->IsA<UUserDefinedEnum>())
    {
        FString ReturnType = TEXT("FName");
        FString DefaultValue = TEXT("NAME_None");
        if (bDisplayName)
        {
            ReturnType = TEXT("FString");
            DefaultValue = TEXT("FString()");
        }
        const FString ResultName = NativizationV2Subsystem->GetNodeStateName(Node, TEXT("EnumeratorName"), true);
        FString Code = ReturnType + TEXT(" ") + ResultName + TEXT(" = ") + DefaultValue + TEXT(";\nswitch (static_cast<int64>(") + Value.Code + TEXT("))\n{\n");
        for (int32 EntryIndex = 0; EntryIndex < Enumeration->NumEnums(); ++EntryIndex)
        {
            FString EntryValue = FString::Format(TEXT("FName(TEXT(\"{0}\"))"),
                { Enumeration->GetNameByIndex(EntryIndex).ToString().ReplaceCharWithEscapedChar() });
            if (bDisplayName)
            {
                EntryValue = UBlueprintNativizationLibrary::GenerateTextLiteral(Enumeration->GetDisplayNameTextByIndex(EntryIndex),
                    Enumeration->GetPathName(), Enumeration->GetNameByIndex(EntryIndex).ToString()) + TEXT(".ToString()");
            }
            Code += FString::Format(TEXT("case {0}:\n{\n{2} = {1};\nbreak;\n}\n"),
                { Enumeration->GetValueByIndex(EntryIndex), EntryValue, ResultName });
        }
        Code += TEXT("default:\n{\nbreak;\n}\n}\n");
        FGenerateResultStruct Result(ResultName, Value.Preparations);
        Result.Preparations.Add(Code);
        return Result;
    }
    if (bDisplayName)
    {
        return FGenerateResultStruct(FString::Format(TEXT("StaticEnum<{0}>()->GetValueOrBitfieldAsDisplayNameText(static_cast<int64>({1})).ToString()"),
            { UBlueprintNativizationLibrary::GetUniqueFieldName(Enumeration), Value.Code }), Value.Preparations);
    }
    return FGenerateResultStruct(FString::Format(TEXT("StaticEnum<{0}>()->GetNameByValue(static_cast<int64>({1}))"),
        { UBlueprintNativizationLibrary::GetUniqueFieldName(Enumeration), Value.Code }), Value.Preparations);
}
