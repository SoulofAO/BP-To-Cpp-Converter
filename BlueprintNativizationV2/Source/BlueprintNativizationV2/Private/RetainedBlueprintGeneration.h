/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "BlueprintNativizationData.h"

class UBlueprint;
class UActorComponent;
class FObjectPropertyBase;
class UK2Node_CallFunction;
class UNativizationV2Subsystem;

class BLUEPRINTNATIVIZATIONV2_API FRetainedBlueprintGeneration
{
public:
    static void Begin(const TArray<UObject*>& Objects);
    static bool IsRetained(UClass* Class);
    static bool IsExternalStructure(const UScriptStruct* Structure);
    static bool ShouldPreserveStructureAsset(const UScriptStruct* Structure);
    static UClass* GetStorageClass(UClass* Class);
    static FString GetClassName(UClass* Class);
    static FString GetClassValue(UClass* Class);
    static FString ResolveClassFilterValue(const FString& Value, UClass* ConstraintClass);
    static bool CanUseStaticClass(UClass* Class, FString& Include);
    static void RegisterFunctionLibraryDependency(UClass* Class);
    static FString GetAssetValue(UObject* Asset, UClass* ConstraintClass = nullptr);
    static FString RegisterRuntimeComponentTemplate(UActorComponent* Component, FProperty* SourceProperty = nullptr);
    static FString RegisterInstanceTemplate(UObject* Object, FProperty* SourceProperty);
    static FString RegisterComponentInstanceTemplate(UActorComponent* SourceComponent, FProperty* Property, UObject* Object);
    static void AddInstanceInitialization(const FString& Initialization);
    static FString Quote(const FString& Value);
    static void BeginObject(UBlueprint* Blueprint = nullptr);
    static void EndObject();
    static TMap<FString, FString> GetMigrationClassMembers();
    static FString RegisterComponentTemplate(UActorComponent* Component);
    static bool bGeneratingConstructor;
    static bool CanInitializeValueBeforeEngineStartup(FProperty* Property, const void* PropertyMemory, bool bInitializeExistingStructure = false);
    static bool CanInitializeDefaultSetterBeforeEngineStartup(FProperty* Property, UObject* DefaultOwner, const FString& SetterName);
    static FString ConstructInstancedObject(FObjectPropertyBase* Property, UObject* Object, const FString& Outer, bool bArchetypeTemplate = false);
    static void Complete(UBlueprint* Blueprint, FString& Header, FString& Source);
    static void PrepareNativeArguments(UK2Node_CallFunction* Node, TArray<UK2Node*> MacroStack,
        UNativizationV2Subsystem* Subsystem, TMap<FName, FString>& Overrides,
        FGenerationPreparations& Preparations, FString& Before, FString& After);
    static FString WrapNativeCall(UFunction* Function, const FString& Invocation,
        const FString& Before, const FString& After);
    static bool UsesReflection(UFunction* Function, UClass* CallingClass = nullptr);
    static bool TryGenerateNativeCall(UFunction* Function, const FString& Target, const TArray<FString>& Arguments, FString& Invocation);
    static FString ReadProperty(FProperty* Property, const FString& Target);
    static FString PreparePropertyRead(FProperty* Property, const FString& Target, UK2Node* Node,
        UNativizationV2Subsystem* Subsystem, FGenerationPreparations& Preparations);
    static FString WriteProperty(FProperty* Property, const FString& Target, const FString& Value);
    static FString CallPropertyAccessor(FProperty* Property, UFunction* Function, const FString& Target, const FString& Value, bool bSetter);
    static FString Transfer(FProperty* Property, const FString& Descriptor, const FString& Address,
        const FString& Value, bool bWrite);
    static FGenerateResultStruct Call(UK2Node_CallFunction* Node, const FString& Target,
        TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem);
};
