/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright 2026 AO All Rights Reserved.
 */

#pragma once

#include "TranslatorObjects/StructTranslatorObject.h"
#include "UObject/StructOnScope.h"

class UEdGraphPin;
class UNativizationV2Subsystem;
struct FGenerateResultStruct;

class FStructInitialization
{
public:
    static bool IsValueOperation(EStructTranslationOperation Operation);
    static const void* ReadValue(const FStructTranslationRequest& Request, FStructOnScope& Storage, FString& Error);
    static FString VariableName(const FStructTranslationRequest& Request, const FString& Purpose);
    static FString Number(double Value);
    static FString Number(float Value);
    static FString Quote(const FString& Value);
    static FString PropertyExpression(FProperty* Property, const void* Value, const FStructTranslationRequest& Request);
    static bool TryReadNativeMember(UScriptStruct* Structure, UEdGraphPin* StructurePin, FName MemberName,
        const FString& SourceExpression, UNativizationV2Subsystem* Subsystem, FGenerateResultStruct& Result);
    static bool Describe(const FStructTranslationRequest& Request, FStructTranslationResult& Result,
        const FString& NativeType, const FString& Include, const FString& Module);
    static bool Fallback(const FStructTranslationRequest& Request, FStructTranslationResult& Result,
        TFunction<void(void*)> FinalizeValue = {}, const FString& FinalizeInstruction = FString());
};
