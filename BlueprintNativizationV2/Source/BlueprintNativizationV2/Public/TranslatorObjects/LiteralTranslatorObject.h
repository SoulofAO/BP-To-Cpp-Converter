/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright 2026 AO All Rights Reserved.
 */

#pragma once

#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_Literal.h"
#include "LiteralTranslatorObject.generated.h"

UCLASS()
class BLUEPRINTNATIVIZATIONV2_API ULiteralTranslatorObject : public UTranslatorBPToCppObject
{
    GENERATED_BODY()

public:
    ULiteralTranslatorObject()
    {
        ClassNodeForCheck = { UK2Node_Literal::StaticClass() };
    }

    FObjectPropertyBase* FindStorageProperty(UK2Node_Literal* Literal) const;

    virtual FString GenerateGlobalVariables(UK2Node* Node, FString Prefix, UNativizationV2Subsystem* Subsystem) override;
    virtual FString GenerateConstruction(UK2Node* Node, FString Prefix, UNativizationV2Subsystem* Subsystem) override;
    virtual TSet<FString> GenerateHeaderIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem) override;
    virtual FGenerateResultStruct GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem) override;
    virtual TSet<FString> GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem) override;
    virtual TSet<FString> GenerateCSPrivateDependencyModuleNames(UK2Node* Node, UNativizationV2Subsystem* Subsystem) override;
};
