// Fill out your copyright notice in the Description page of Project Settings.


#include "TranslatorObjects/CastByteToEnumTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSettings.h"

FGenerateResultStruct UCastByteToEnumTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
    UK2Node_CastByteToEnum* Conversion = CastChecked<UK2Node_CastByteToEnum>(Node);
    const FGenerateResultStruct Input = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node,
        Conversion->FindPinChecked(TEXT("Byte")), 0, MacroStack);
    const FString Type = UBlueprintNativizationLibrary::GetUniqueFieldName(Conversion->Enum);
    if (Conversion->bSafe)
    {
        FGenerateResultStruct Result = Input;
        const FString ValueName = NativizationV2Subsystem->GetNodeStateName(Node, TEXT("ValidatedEnumValue"), true);
        Result.Preparations.Add(FString::Format(TEXT("{0} {1} = static_cast<{0}>({2});\n{\nconst auto EnumValue = {3};\nif (StaticEnum<{0}>()->IsValidEnumValueOrBitfield(EnumValue))\n{\n{1} = static_cast<{0}>(EnumValue);\n}\n}\n"),
            {Type, ValueName, Conversion->Enum->GetMaxEnumValue(), Input.Code}));
        Result.Code = ValueName;
        return Result;
    }
    return FGenerateResultStruct(FString::Format(TEXT("static_cast<{0}>({1})"), { Type, Input.Code }), Input.Preparations);
}
