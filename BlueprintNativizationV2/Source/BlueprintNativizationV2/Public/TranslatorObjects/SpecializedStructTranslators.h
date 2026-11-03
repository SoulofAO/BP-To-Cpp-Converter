/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "TranslatorObjects/StructTranslatorObject.h"
#include "SpecializedStructTranslators.generated.h"

UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UExternalBlueprintStructTranslator : public UStructTranslatorObject
{
    GENERATED_BODY()
public:
    virtual bool CanTranslate(const UScriptStruct* Structure) const override;
    virtual bool Translate(const FStructTranslationRequest& Request, FStructTranslationResult& Result) const override;
    virtual bool TranslateSerializedFallback(const FStructTranslationRequest& Request, FStructTranslationResult& Result) const override;
};

UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UCollisionStructTranslator : public UStructTranslatorObject
{
    GENERATED_BODY()
public:
    virtual bool CanTranslate(const UScriptStruct* Structure) const override;
    virtual bool Translate(const FStructTranslationRequest& Request, FStructTranslationResult& Result) const override;
    virtual bool TranslateSerializedFallback(const FStructTranslationRequest& Request, FStructTranslationResult& Result) const override;
    virtual bool CanInitializeValueBeforeEngineStartup(const FStructTranslationRequest& Request) const override;
};

UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UGameplayTagQueryStructTranslator : public UStructTranslatorObject
{
    GENERATED_BODY()
public:
    virtual bool CanTranslate(const UScriptStruct* Structure) const override;
    virtual bool Translate(const FStructTranslationRequest& Request, FStructTranslationResult& Result) const override;
};

UCLASS()
class BLUEPRINTNATIVIZATIONV2_API USlateValueStructTranslator : public UStructTranslatorObject
{
    GENERATED_BODY()
public:
    virtual bool CanTranslate(const UScriptStruct* Structure) const override;
    virtual bool Translate(const FStructTranslationRequest& Request, FStructTranslationResult& Result) const override;
    virtual bool TranslateSerializedFallback(const FStructTranslationRequest& Request, FStructTranslationResult& Result) const override;
};

UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UInstancedValueStructTranslator : public UStructTranslatorObject
{
    GENERATED_BODY()
public:
    virtual bool CanTranslate(const UScriptStruct* Structure) const override;
    virtual bool Translate(const FStructTranslationRequest& Request, FStructTranslationResult& Result) const override;
    virtual bool TranslateSerializedFallback(const FStructTranslationRequest& Request, FStructTranslationResult& Result) const override;
};
