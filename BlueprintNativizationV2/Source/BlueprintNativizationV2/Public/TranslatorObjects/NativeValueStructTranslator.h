/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright 2026 AO All Rights Reserved.
 */

#pragma once

#include "TranslatorObjects/StructTranslatorObject.h"
#include "NativeValueStructTranslator.generated.h"

UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UNativeValueStructTranslator : public UStructTranslatorObject
{
    GENERATED_BODY()
public:
    virtual bool CanTranslate(const UScriptStruct* Structure) const override;
    virtual bool Translate(const FStructTranslationRequest& Request, FStructTranslationResult& Result) const override;
    virtual bool CanInitializeValueBeforeEngineStartup(const FStructTranslationRequest& Request) const override;
};
