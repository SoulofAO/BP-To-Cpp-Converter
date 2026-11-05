/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright 2025 AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/Latens/DelayTranslatorObject.h"
#include "K2Node_CallFunction.h"
#include "DelayNextTickTranslatorObject.generated.h"

/**
 * 
 */

UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UDelayNextTickTranslatorObject : public UDelayTranslatorObject
{
    GENERATED_BODY()

public:

    UDelayNextTickTranslatorObject()
    {
        ClassNodeForCheck = {UK2Node_CallFunction::StaticClass()};
        NodeNamesForCheck = { "DelayUntilNextTick" };
    }

};
