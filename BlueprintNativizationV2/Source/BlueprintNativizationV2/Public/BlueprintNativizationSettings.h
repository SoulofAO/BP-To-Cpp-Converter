/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright 2025 AO All Rights Reserved.
 */


#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "TranslatorObjects/StructTranslatorObject.h"
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

    UPROPERTY(EditAnywhere, Category = "ConstructorDescriptor")
    bool bNeedToTransform = true;

    FConstructorDescriptor()
        : OriginalStructName(TEXT(""))
        , ConstructorProperties()
    {
    }

    FConstructorDescriptor(const FString& InOriginalStructName, const TArray<FConstructorPropertyDescriptor>& InConstructorProperties, bool bNewNeedToTransform = true)
        : OriginalStructName(InOriginalStructName)
        , ConstructorProperties(InConstructorProperties)
        , bNeedToTransform(bNewNeedToTransform)
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

    UFUNCTION(BlueprintCallable, Category = "General")
    static FString GetModuleApiName();

    UFUNCTION(BlueprintCallable, Category = "General")
    static FString GetModuleName();

    UFUNCTION(BlueprintCallable, Category = "Function Mapping")
    static bool FindNativeByBlueprint(const FFunctionDescriptor BlueprintDesc, FFunctionDescriptor& OutNativeDesc);

    UFUNCTION(BlueprintCallable, Category = "Constructors")
    static bool FindConstructorDescriptorByStruct(UStruct* OwnerStruct, FConstructorDescriptor& OutNativeDesc);

    static UStructTranslatorObject* FindStructTranslator(const UScriptStruct* Structure);
    static FString TranslateStructure(const FStructTranslationRequest& Request, FGenerationPreparations* Preparations = nullptr);

    UFUNCTION(BlueprintCallable, Category = "Getter And Setter")
    static bool FindGetterAndSetterDescriptorDescriptorByPropertyNameAndObject(FName PropertyOriginalName, UObject* Object, FGetterAndSetterPropertyDescriptor& OutGetterAndSetterDescriptorDesc);

    static bool FindGetterAndSetterDescriptorDescriptorByProperty(FProperty* Property, FGetterAndSetterPropertyDescriptor & OutGetterAndSetterDescriptorDesc);

    UFUNCTION(BlueprintCallable, Category = "ColorEditor")
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

UENUM(BlueprintType)
enum class ETextGenerationMode : uint8
{
    PlainText UMETA(DisplayName = "Plain text (FromString)"),
    ReadableLocalizationKeys UMETA(DisplayName = "New readable localization keys"),
    PreserveTextData UMETA(DisplayName = "Preserve all text data and translation identity")
};

UCLASS(config = EditorPerProjectUserSettings)
class UBlueprintNativizationV2EditorSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UBlueprintNativizationV2EditorSettings();

    UPROPERTY(EditAnywhere, config, Category = "Blueprint Repair", meta = (ToolTip = "Explicit missing Blueprint library class path to native library class path. TryFix preserves calls and requires matching pins."))
    TMap<FString, FString> BlueprintRepairLibraryReplacements;

    UPROPERTY(EditAnywhere, config, Category = "Blueprint Repair", meta = (ToolTip = "Explicit retired component class path to owning actor class path. TryFix requires the current owner to have no corresponding property or component template."))
    TMap<FString, FString> BlueprintRepairRetiredComponentOwners;

    UPROPERTY(EditAnywhere, config, Category = "Blueprint Repair", meta = (ToolTip = "Clear missing textures used only by Blueprint widget image brushes and disable drawing those empty images."))
    bool bClearMissingWidgetImages = false;

    virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

    virtual FName GetSectionName() const override { return TEXT("BlueprintNativizationV2PluginEditorSettings"); }

    //The main source of translating Blueprint functions into C++ is translators. A translator serves one or more types of K2Node, and translates them into C++ code.
    UPROPERTY(EditAnywhere, config, Category = "General")
    TArray<TSubclassOf<UTranslatorBPToCppObject>> TranslatorBPToCPPObjects;

    UPROPERTY(EditAnywhere, config, Category = "General")
    TArray<TSubclassOf<UStructTranslatorObject>> StructTranslators;

    //Some Visual Generale
    UPROPERTY(EditAnywhere, config, Category = "General|CodeEditor")
    TArray<FColorEditableKeywordGroup> ColorEditableTextGroups;

    //Some Visual Generale
    UPROPERTY(EditAnywhere, config, Category = "General|CodeEditor")
    TArray<FString> ColorEditableTextOperators;

    //Some Visual Generale
    UPROPERTY(EditAnywhere, config, Category = "General|CodeEditor")
    FLinearColor OperatorColor = FLinearColor(1.0, 0.5, 0.0);

    //Some Visual Generale
    UPROPERTY(EditAnywhere, config, Category = "General|CodeEditor")
    FLinearColor IncludeColor = FLinearColor(1.0, 0.5, 0.0);

    //Some Visual Generale
    UPROPERTY(EditAnywhere, config, Category = "General|CodeEditor")
    FLinearColor CommentsColor = FLinearColor(0.0, 1.0, 0.0);

    //Global Variable Names are used to avoid name conflicts.
    UPROPERTY(EditAnywhere, config, Category = "General")
    TArray<FString> GlobalVariableNames;

    UPROPERTY(config, meta = (DeprecatedProperty, DeprecationMessage = "The converter uses the native Slate interface."))
    TSoftClassPtr<USetupActionObject> SetupActionObject;

    //ModuleName to initialize and add Code
    UPROPERTY(EditAnywhere, config, Category = "General")
    FString ModuleName = "BlueprintNativizationModule";

    UPROPERTY(EditAnywhere, config, Category = "General", meta = (ToolTip = "Plain text discards localization data. New readable keys require new or migrated translations and flatten text histories. Preserve retains text histories, string-table references and existing translation identity."))
    ETextGenerationMode TextGenerationMode = ETextGenerationMode::PreserveTextData;

    UPROPERTY(EditAnywhere, config, Category = "General", meta = (DisplayName = "Remove Unreal Prefixes From File Names", ToolTip = "Remove U, A, F and E type prefixes from generated filenames without changing C++ type names."))
    bool bRemoveUnrealPrefixesFromFileNames = true;

    UPROPERTY(EditAnywhere, config, Category = "General", meta = (DisplayName = "Replace Default-Only Children With Parents", ToolTip = "Skip Blueprint children with no added structure or logic. Generated references use their parents and discard child class default overrides. Source assets are unchanged."))
    bool bReplaceDefaultOnlyChildrenWithParents = true;

    UPROPERTY(EditAnywhere, config, Category = "General", meta = (DisplayName = "Exclude Engine Assets From Conversion", ToolTip = "Exclude Engine assets, Engine plugin assets and Blueprint descendants of Engine Blueprints from selection, dependencies and generation. Native C++ parents remain supported."))
    bool bExcludeEngineAssetsFromConversion = true;

    UPROPERTY(EditAnywhere, config, Category = "General", meta = (DisplayName = "Continue On Error", ToolTip = "Continue generation and migration with the successful subset. Skip failed assets and assets that depend on them. Allow Export After Failed Generation separately permits diagnostic export of incomplete code."))
    bool bContinueOnError = true;

    UPROPERTY(Config, EditAnywhere, Category = "General", meta = (DisplayName = "Allow Export After Failed Generation", ToolTip = "Keep partial source after generation errors for diagnostic export. Failed results cannot create migration caches."))
    bool bAllowExportAfterFailedGeneration = false;


    UPROPERTY(EditAnywhere, config, Category = "General", meta = (ClampMin = "2", ToolTip = "Minimum occurrences of an independent guarded call before extracting a named local lambda."))
    int32 RepeatedConstructionThreshold = 3;

    //Enable Generate Value Suffix controls whether all generated variables in C++ code will end with GeneratedValue. Better disable it, the code will be cleaner. 
    UPROPERTY(EditAnywhere, config, Category = "General")
    bool bEnableGenerateSuffix = false;

    //Add BP Prefix To Parent Blueprint controls whether existing Blueprint classes will get the “BP_” prefix when rebuilding on C++ inheritors.
    UPROPERTY(EditAnywhere, config, Category = "General")
    bool bAddBPPrefixToReparentBlueprint = true;





    /*
    Ignore Class to Ref Generate. 
    This includes all classes that should not be nativized. 
    Typically, this is UI, Widget, etc. Do not change this widget and try to generate C++ for UI, this will lead to a crash.
    */
    UPROPERTY(EditAnywhere, config, Category = "General")
    TArray<TSoftClassPtr<UObject>> IgnoreClassToRefGenerate;

    //This includes all assets that should not be nativized. 
    UPROPERTY(EditAnywhere, Category = "General")
    TArray<TSoftObjectPtr<UObject>> IgnoreAssetsToRefGenerate;



};
