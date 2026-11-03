/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "ConvertAssetTranslatorObject.generated.h"

UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UConvertAssetTranslatorObject : public UTranslatorBPToCppObject
{
    GENERATED_BODY()

public:
    UConvertAssetTranslatorObject();

    virtual FGenerateResultStruct GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin,
        int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem) override;
};
