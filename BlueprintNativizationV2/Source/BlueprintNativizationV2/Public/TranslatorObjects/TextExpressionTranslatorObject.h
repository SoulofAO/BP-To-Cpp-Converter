/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright 2026 AO All Rights Reserved.
 */

#pragma once
#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_FormatText.h"
#include "K2Node_EnumEquality.h"
#include "TextExpressionTranslatorObject.generated.h"

UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UTextExpressionTranslatorObject : public UTranslatorBPToCppObject
{
    GENERATED_BODY()
public:
    UTextExpressionTranslatorObject()
    {
        ClassNodeForCheck = {UK2Node_FormatText::StaticClass(), UK2Node_EnumEquality::StaticClass()};
    }
    virtual FGenerateResultStruct GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem) override;
    virtual TSet<FString> GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem) override;
};
