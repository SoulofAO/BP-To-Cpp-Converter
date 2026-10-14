/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/Latens/DelayTranslatorObject.h"
#include "K2Node_CallFunction.h"


#include "RetriggerableDelayTranslatorObject.generated.h"


UCLASS()
class BLUEPRINTNATIVIZATIONV2_API URetriggarableDelayTranslatorObject : public UDelayTranslatorObject
{
    GENERATED_BODY()

public:

    URetriggarableDelayTranslatorObject()
    {
        ClassNodeForCheck = {UK2Node_CallFunction::StaticClass()};
        NodeNamesForCheck = { "RetriggerableDelay" };
    }

};
