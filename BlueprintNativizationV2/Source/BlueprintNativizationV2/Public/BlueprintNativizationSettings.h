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

UCLASS(config = EditorPerProjectUserSettings)
class UBlueprintNativizationV2EditorSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UBlueprintNativizationV2EditorSettings();

    virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

    virtual FName GetSectionName() const override { return TEXT("BlueprintNativizationV2PluginEditorSettings"); }

    //The main source of translating Blueprint functions into C++ is translators. A translator serves one or more types of K2Node, and translates them into C++ code.
    UPROPERTY(EditAnywhere, config, Category = "General")
    TArray<TSubclassOf<UTranslatorBPToCppObject>> TranslatorBPToCPPObjects;

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

    //The Setup Action Object is an object that links EU_NativizationTool to various helper Widgets on the Blueprint side and is best left alone.
    UPROPERTY(EditAnywhere, config, Category = "General")
    TSoftClassPtr<USetupActionObject> SetupActionObject;

    //ModuleName to initialize and add Code
    UPROPERTY(EditAnywhere, config, Category = "General")
    FString ModuleName = "BlueprintNativizationModule";

    //Enable Generate Value Suffix controls whether all generated variables in C++ code will end with GeneratedValue. Better disable it, the code will be cleaner. 
    UPROPERTY(EditAnywhere, config, Category = "General")
    bool bEnableGenerateSuffix = false;

    //Add BP Prefix To Parent Blueprint controls whether existing Blueprint classes will get the “BP_” prefix when rebuilding on C++ inheritors.
    UPROPERTY(EditAnywhere, config, Category = "General")
    bool bAddBPPrefixToReparentBlueprint = true;

    /*
    Function Redirects - a list of functions that are Blueprint Implemented.These functions do not have enough metadata to say which function calls it,
    where it can be redefined in C++.This is what this array is for.It relates the name of the Blueprint Implemented and the original C++ function.
    */
    UPROPERTY(EditAnywhere, Category = "General")
    TArray<FFunctionBinding> FunctionRedirects;

    /*
    Construction Descriptors - to constructors of structures, where using a constructor for initialization is the most “correct” option. In other cases, direct setting of values via '.' is used, that is, instead of, for example, FLinearColor(0.0,0.66,1.0,1.0), FLinearColor LinearColor() will be generated;
    LinearColor.R = 1.0; 
    LinearColor.G = 1.0; etc. 
    Also implemented due to the lack of reflection for this. For structures generated in Blueprints, their full constructor is implemented by default.
    */
    UPROPERTY(EditAnywhere, config, Category = "General")
    TArray<FConstructorDescriptor> ConstructorDescriptors;

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

    //If the class cannot be activated, call the Blueprint functions directly.
    UPROPERTY(EditAnywhere, config, Category = "General")
    bool bReplaceCallToIgnoreAssetsToDirectBlueprintCall = false;

    /*
    Getter And Setter Description. 
    FProperty do not contain information about the privacy - publicity of variables. This leads to the fact that access to a superior object can cause a crash due to lack of access to the variable.
    For this, there is a list where variables are presented with a function to perform an operation, such as Get Set or completely ignore the variable.
    */
    UPROPERTY(EditAnywhere, config, Category = "General")
    TArray<FGetterAndSetterPropertyDescriptor> GetterAndSetterDescriptors;
};
