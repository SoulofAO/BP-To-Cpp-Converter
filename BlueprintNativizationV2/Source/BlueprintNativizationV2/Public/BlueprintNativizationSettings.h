/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */


#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "Engine/DeveloperSettings.h"

#include "BlueprintNativizationSettings.generated.h"

/**
 * 
 */

USTRUCT(BlueprintType)
struct FColorEditableKeywordGroup
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "ColorEditableKeywordGroup")
    FLinearColor Color;

    UPROPERTY(EditAnywhere, Category = "ColorEditableKeywordGroup")
    TArray<FString> Names;
};


USTRUCT(BlueprintType)
struct FGetterAndSetterPropertyDescriptor
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "GetterAndSetterPropertyDescriptor")
    FString OriginalStructName;

    UPROPERTY(EditAnywhere, Category = "GetterAndSetterPropertyDescriptor")
    FString OriginalPropertyName;

    //Only Name Function
    UPROPERTY(EditAnywhere, Category = "GetterAndSetterPropertyDescriptor")
    FString GetterPropertyFunctionName;

    //Only Name Function
    UPROPERTY(EditAnywhere, Category = "GetterAndSetterPropertyDescriptor")
    FString SetterPropertyFunctionName;

    FGetterAndSetterPropertyDescriptor()
        : OriginalStructName(TEXT(""))
        , OriginalPropertyName(TEXT(""))
        , GetterPropertyFunctionName(TEXT(""))
        , SetterPropertyFunctionName(TEXT(""))
    {
    }

    FGetterAndSetterPropertyDescriptor(const FString& InOriginalStructName, const FString& InOriginalPropertyName, const FString& InGetterPropertyFunctionName, const FString& InSetterPropertyFunctionName)
        : OriginalStructName(InOriginalStructName)
        , OriginalPropertyName(InOriginalPropertyName)
        , GetterPropertyFunctionName(InGetterPropertyFunctionName)
        , SetterPropertyFunctionName(InSetterPropertyFunctionName)
    {
    }
};


USTRUCT(BlueprintType)
struct FConstructorPropertyDescriptor
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "ConstructorPropertyDescriptor")
    FString OriginalPropertyName;

    UPROPERTY(EditAnywhere, Category = "ConstructorPropertyDescriptor")
    int ConstructorIndex;

    UPROPERTY(EditAnywhere, Category = "ConstructorPropertyDescriptor")
    FString DefaultValueName;

    FConstructorPropertyDescriptor()
        : OriginalPropertyName(TEXT(""))
        , ConstructorIndex(0)
        , DefaultValueName(TEXT(""))
    {
    }

    FConstructorPropertyDescriptor(const FString& InOriginalPropertyName, int InConstructorIndex, const FString& InDefaultValueName)
        : OriginalPropertyName(InOriginalPropertyName)
        , ConstructorIndex(InConstructorIndex)
        , DefaultValueName(InDefaultValueName)
    {
    }
};

USTRUCT(BlueprintType)
struct FConstructorDescriptor
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "ConstructorDescriptor")
    FString OriginalStructName;

    UPROPERTY(EditAnywhere, Category = "ConstructorDescriptor")
    TArray<FConstructorPropertyDescriptor> ConstructorProperties;

    FConstructorDescriptor()
        : OriginalStructName(TEXT(""))
        , ConstructorProperties()
    {
    }

    FConstructorDescriptor(const FString& InOriginalStructName, const TArray<FConstructorPropertyDescriptor>& InConstructorProperties)
        : OriginalStructName(InOriginalStructName)
        , ConstructorProperties(InConstructorProperties)
    {
    }
};


USTRUCT(BlueprintType)
struct FFunctionDescriptor
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Function")
    FString ClassName;

    UPROPERTY(EditAnywhere, Category = "Function")
    FString FunctionName;

    UPROPERTY(EditAnywhere, Category = "Function")
    TArray<FString> ParameterSignature;

    FFunctionDescriptor() = default;

    FFunctionDescriptor(const FString& InClass, const FString& InFunc, const TArray<FString>& InParams)
        : ClassName(InClass), FunctionName(InFunc), ParameterSignature(InParams)
    {
    }

    FORCEINLINE FString GetFullSignature() const
    {
        return ClassName + TEXT("::") + FunctionName + TEXT("(") + FString::Join(ParameterSignature, TEXT(", ")) + TEXT(")");
    }

    FORCEINLINE FString GetDeclarationSignature() const
    {
        return FunctionName + TEXT("(") + FString::Join(ParameterSignature, TEXT(", ")) + TEXT(")");
    }

    FORCEINLINE bool operator==(const FFunctionDescriptor& Other) const
    {
        return ClassName == Other.ClassName &&
            FunctionName == Other.FunctionName &&
            ParameterSignature == Other.ParameterSignature;
    }
};

FORCEINLINE uint32 GetTypeHash(const FFunctionDescriptor& Desc)
{
    uint32 Hash = HashCombine(GetTypeHash(Desc.ClassName), GetTypeHash(Desc.FunctionName));
    for (const FString& Param : Desc.ParameterSignature)
    {
        Hash = HashCombine(Hash, GetTypeHash(Param));
    }
    return Hash;
}


USTRUCT(BlueprintType)
struct FFunctionBinding
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Function Mapping")
    FFunctionDescriptor BlueprintFunction;

    UPROPERTY(EditAnywhere, Category = "Function Mapping")
    FFunctionDescriptor NativeFunction;
};

UCLASS(BlueprintType)
class BLUEPRINTNATIVIZATIONV2_API UBlueprintNativizationSettingsLibrary : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Function Mapping")
    static bool FindNativeByClassAndName(UClass* OwnerClass, FString OwnerName, FFunctionDescriptor& OutNativeDesc);

    UFUNCTION(BlueprintCallable, Category = "Function Mapping")
    static bool FindNativeByBlueprint(const FFunctionDescriptor BlueprintDesc, FFunctionDescriptor& OutNativeDesc);

    UFUNCTION(BlueprintCallable, Category = "General")
    static bool FindConstructorDescriptorByStruct(UStruct* OwnerStruct, FConstructorDescriptor& OutNativeDesc);

    static bool FindGetterAndSetterDescriptorDescriptorByPropertyName(FProperty* Property, FGetterAndSetterPropertyDescriptor & OutGetterAndSetterDescriptorDesc);

    static bool FindColorByNameInColorTextGroup(FString Name, FLinearColor& LinearColor);
};

UCLASS(Blueprintable)
class USetupActionObject : public UObject
{
    GENERATED_BODY()

public:

    UFUNCTION(BlueprintNativeEvent, meta = (ForceAsFunction))
    void ActionToNode(FName ActionName, FName NodeName, UBlueprint* Blueprint);
    virtual void ActionToNode_Implementation(FName ActionName, FName NodeName, UBlueprint* Blueprint) {};

    UFUNCTION(BlueprintNativeEvent, meta = (ForceAsFunction))
    void ActionToProeprty(FName ActionName, FName PropertyName, UBlueprint* Blueprint);
    virtual void ActionToProeprty_Implementation(FName ActionName, FName PropertyName, UBlueprint* Blueprint) {};

};

UCLASS(config = EditorPerProjectUserSettings)
class UBlueprintNativizationV2EditorSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UBlueprintNativizationV2EditorSettings();

    virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

    virtual FName GetSectionName() const override { return TEXT("BlueprintNativizationV2PluginEditorSettings"); }


    UPROPERTY(EditAnywhere, config, Category = "General")
    TArray<TSubclassOf<UTranslatorBPToCppObject>> TranslatorBPToCPPObjects;

    UPROPERTY(EditAnywhere, config, Category = "General|CodeEditor")
    TArray<FColorEditableKeywordGroup> ColorEditableTextGroups;

    UPROPERTY(EditAnywhere, config, Category = "General|CodeEditor")
    TArray<FString> ColorEditableTextOperators;

    UPROPERTY(EditAnywhere, config, Category = "General")
    TArray<FString> GlobalVariableNames;

    UPROPERTY(EditAnywhere, config, Category = "General")
    TSoftClassPtr<USetupActionObject> SetupActionObject;

    UPROPERTY(EditAnywhere, config, Category = "General")
    bool bEnableGenerateValueSuffix = true;

    UPROPERTY(EditAnywhere, config, Category = "General")
    bool bAddBPPrefixToReparentBlueprint = true;

    UPROPERTY(EditAnywhere, Category = "General")
    TArray<FFunctionBinding> FunctionRedirects;

    UPROPERTY(EditAnywhere, config, Category = "General")
    TArray<FConstructorDescriptor> ConstructorDescriptors;

    UPROPERTY(EditAnywhere, config, Category = "General")
    TArray<TSoftClassPtr<UObject>> IgnoreClassToRefGenerate;

    UPROPERTY(EditAnywhere, Category = "General")
    TArray<TSoftObjectPtr<UObject>> IgnoreAssetsToRefGenerate;

    UPROPERTY(EditAnywhere, config, Category = "General")
    TArray<FGetterAndSetterPropertyDescriptor> GetterAndSetterDescriptors;
};
