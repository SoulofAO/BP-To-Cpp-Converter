#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "StructTranslatorObject.generated.h"

enum class EStructTranslationOperation : uint8
{
    DescribeType,
    DefaultValue,
    PropertyValue,
    SerializedValue,
    Construction,
    Conversion,
    ReadMember,
    WriteMember
};

struct FStructTranslationArgument
{
    FName Name;
    FString Expression;
    FString NativeType;
};

enum class EStructInitializationStage : uint8
{
    Value,
    NativeConstructor,
    Runtime
};

struct FStructTranslationRequest
{
    EStructTranslationOperation Operation = EStructTranslationOperation::DescribeType;
    const UScriptStruct* Structure = nullptr;
    const FProperty* Property = nullptr;
    const void* PropertyValue = nullptr;
    const void* BaseValue = nullptr;
    const UObject* OwnerObject = nullptr;
    UClass* OwnerClass = nullptr;
    FString OwnerExpression;
    FString DestinationExpression;
    EStructInitializationStage InitializationStage = EStructInitializationStage::Value;
    bool bKeepAssetReferencesInBlueprint = false;
    FString SerializedValue;
    FString SourceExpression;
    FString DestinationType;
    FName MemberName;
    TArray<FStructTranslationArgument> Arguments;
};

struct FStructTranslationResult
{
    FString NativeType;
    FString Expression;
    TArray<FString> Preparations;
    TArray<FString> InitializationInstructions;
    TArray<FString> HeaderIncludes;
    TArray<FString> ForwardDeclarations;
    TArray<FString> ModuleDependencies;
    FString Error;
};

UCLASS(Abstract)
class BLUEPRINTNATIVIZATIONV2_API UStructTranslatorObject : public UObject
{
    GENERATED_BODY()

public:
    virtual bool CanTranslate(const UScriptStruct* Structure) const
        PURE_VIRTUAL(UStructTranslatorObject::CanTranslate, return false;);

    virtual bool Translate(const FStructTranslationRequest& Request, FStructTranslationResult& Result) const
        PURE_VIRTUAL(UStructTranslatorObject::Translate, return false;);

    virtual bool TranslateSerializedFallback(const FStructTranslationRequest& Request, FStructTranslationResult& Result) const;
    virtual bool CanInitializeValueBeforeEngineStartup(const FStructTranslationRequest& Request) const
    {
        return false;
    }
};
