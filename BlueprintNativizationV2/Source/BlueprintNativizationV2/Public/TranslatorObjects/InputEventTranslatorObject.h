/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "TranslatorObjects/BaseTranslatorObject.h"
#include "InputEventTranslatorObject.generated.h"

UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UInputEventTranslatorObject : public UTranslatorBPToCppObject
{
    GENERATED_BODY()
public:
    static bool HasInputBinding(UK2Node* Node);
    virtual bool CanApply(UK2Node* Node) override;
    virtual void GenerateNewFunction(UK2Node* InputNode, TArray<UK2Node*> Path,
        TArray<FGenerateFunctionStruct>& EntryNodes, UNativizationV2Subsystem* Subsystem) override;
    virtual FString GenerateSetupPlayerInputComponentFunction(UK2Node* InputNode, FString Prefix,
        UNativizationV2Subsystem* Subsystem) override;
    virtual TSet<FString> GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem) override;
};
