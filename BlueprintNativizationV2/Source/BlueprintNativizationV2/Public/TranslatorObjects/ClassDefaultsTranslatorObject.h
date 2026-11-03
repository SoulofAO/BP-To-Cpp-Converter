/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "TranslatorObjects/BaseTranslatorObject.h"
#include "ClassDefaultsTranslatorObject.generated.h"

UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UClassDefaultsTranslatorObject : public UTranslatorBPToCppObject
{
    GENERATED_BODY()

public:
    virtual bool CanApply(UK2Node* Node) override;
    virtual FGenerateResultStruct GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex,
        TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem) override;
    virtual TSet<FString> GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem) override;
};
