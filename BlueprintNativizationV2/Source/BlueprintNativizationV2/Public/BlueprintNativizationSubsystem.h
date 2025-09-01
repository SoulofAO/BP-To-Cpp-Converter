/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_Composite.h"
#include "BlueprintNativizationData.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "IHotReload.h"
#include "BlueprintNativizationSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE_ThreeParams(FModuleCompilerFinishedEvent, const FString&, ECompilationResult::Type, bool);

/* 
*	Function library class.
*	Each function in it is expected to be static and represents blueprint node that can be called in any blueprint.
*
*	When declaring function you can define metadata for the node. Key function specifiers will be BlueprintPure and BlueprintCallable.
*	BlueprintPure - means the function does not affect the owning object in any way and thus creates a node without Exec pins.
*	BlueprintCallable - makes a function which can be executed in Blueprints - Thus it has Exec pins.
*	DisplayName - full name of the node, shown when you mouse over the node and in the blueprint drop down menu.
*				Its lets you name the node using characters not allowed in C++ function names.
*	CompactNodeTitle - the word(s) that appear on the node.
*	Keywords -	the list of keywords that helps you to find node when you search for it using Blueprint drop-down menu. 
*				Good example is "Print String" node which you can find also by using keyword "log".
*	Category -	the category your node will be under in the Blueprint drop-down menu.
*
*	For more info on custom blueprint nodes visit documentation:
*	https://wiki.unrealengine.com/Custom_Blueprint_Node_Creation
*/

inline FString ModuleAPIName = TEXT("BLUEPRINTNATIVIZATIONMODULE_API");


UCLASS()
class UNativizationV2Subsystem : public UEngineSubsystem
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Nativization")
    FNativizationModuleResult RunNativizationForAllObjects(TArray<UObject*> InputTargets, 
        bool SaveToFile,
        FString InputOutputFolder = "",
        bool InputTransformOnlyOneFileCode = true, 
        bool HotReloadAndReplace = false,
        bool MergeWithExistModule = false, 
        bool SaveCache = false, bool NewLeftAllAssetRefInBlueprint = false, FString SaveCachePath = "");

    UFUNCTION(BlueprintCallable, Category = "Nativization")
    FNativizationCode RunNativizationForOneFunctionBlueprint(UBlueprint* InputTarget, bool NewLeftAllAssetRefInBlueprint, FString FunctionName = "");

    UFUNCTION(BlueprintCallable, Category = "Nativization")
    void PrepareNewModuleToWork();

    void AddModuleToUProject(const FString& ModuleName);

    int32 FindConstructorClosingBraceIndex(const FString& Code);

    void AddModuleToTargets(const FString& ModuleName);

    UFUNCTION(BlueprintCallable, Category = "Nativization")
    void ResetNameSubsystem();

    UFUNCTION(BlueprintCallable, Category = "Nativization")
    FNativizationCode GenerateCodeForObject(UObject* InputTarget, FString FunctionName = "");

    UFUNCTION(BlueprintCallable, Category = "Nativization")
    void PrintAllK2Nodes(UBlueprint* InputTarget, FString FunctionName = "");

    UPROPERTY(BlueprintReadOnly, Category = "Nativization")
    bool bIsNativizing = false;

    UPROPERTY(BlueprintReadOnly, Category = "Nativization")
    bool LeftAllAssetRefInBlueprint = false;

    UPROPERTY(BlueprintReadOnly, Category = "Nativization")
    TArray<UTranslatorBPToCppObject*> TranslatorObjects;

    TArray<FGenerateFunctionStruct> EntryNodes;

    void LoadTranslatorObjects();

    bool CreateFile(const FString& FilePath, const FString& Content);

    void EnsureDirectory(const FString& Directory);

    void SaveNativizationResult(FNativizationModuleResult NativizationModuleResult, FString Directory, FString& OutputDirectory, bool MergeWithExistModule);

    FNativizationCode GenerateUnrealModule(const FString& ModuleName, TArray<UObject*> Targets);

    FString GenerateUnrealBuildCsModule(const FString& ModuleName, TArray<UObject*> Targets);

    void CreateJSONCache(FString CacheDirectory);

    UFUNCTION(BlueprintCallable, Category = "Nativization")
    void ReapplyJSONCache(FString CacheFilePath);

    bool HasNewClassesAfterHotReload();

    UField* FindFieldByNameViaCDO(const FName& TargetName);

    void ReplaceBlueprintTypesAfterHotReload(const TMap<UField*, FName>& TranslatedObjects);

    FString ConvertUEnumToCppEnumDeclaration(UEnum* Enum);

    FString ConvertUStructToCppStructDeclaration(UStruct* Struct);

    void PreloadAllNamesForObjects(TArray<UObject*> Objects);

    void GenerateInterfaceCode(UBlueprint* BlueprintInterfaceClass, FNativizationCode& OutContent);

    void GenerateBlueprintCode(UBlueprint* Blueprint,FString FunctionName, FNativizationCode& OutContent);

    //GenerateHeaderTool
    FString GenerateHFileBlueprintCode(UBlueprint* Blueprint, FString DirectFunctionName = "None");

    TSet<FString> GenerateImportsDeclarationsHeaderCode(TSubclassOf<UObject> Class);

    TSet<FString> GenerateClassPreDeclarationsHeaderCode(TSubclassOf<UObject> Class);

    FString GenerateFunctionDeclarationsCode(TSubclassOf<UObject> GeneratedClass, bool VirtualDeclarationToZeroImplementation = false);

    FString GetFunctionFlagsString(UFunction* Function);

    FString GetPropertyFlagsString(FProperty* Property);

    FString GenerateDelegateMacroDeclarationBlueprintCode(UBlueprint* Blueprint);

    FString GenerateVariableDeclarationBlueprintCode(UBlueprint* Blueprint, FString DirectFunctionName = "None");

    FString GenerateGlobalFunctionVariables(UBlueprint* Blueprint);

    //HelperFunctions
    TArray<FGenerateFunctionStruct> GenerateFunctionsEntryNamesFromEntryNodes(TArray<UK2Node*> EntryNodes);

    TArray<FGenerateFunctionStruct> GenerateAllGenerateFunctionStruct(UBlueprint* Blueprint, FString DirectFunctionName = "None", TArray<UEdGraph*> FilterGraphs = TArray<UEdGraph*>());

    UTranslatorBPToCppObject* FindTranslatorForNode(UK2Node* Node);
    //EndHelperFunctions

    //GenerateCpp
    FString GenerateCppFileBlueprintCode(UBlueprint* Blueprint, FString DirectFunctionName = "None");

    TSet<FString> GenerateImportsDeclarationsCppCode(UBlueprint* Blueprint);

    FString GenerateConstructorCppCode(UBlueprint* Blueprint);

    bool GetIncludeFromField(UField* OwnerField, FString& IncludePath);

    TSet<FString> GetInputParametersForEntryNode(UK2Node* EntryNode);

    FString ProcessGraphsForCodeGeneration(UK2Node* Node);

    FString GenerateSetupPlayerInputComponentCppCode(UBlueprint* Blueprint);

    FString GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack);

    TSet<FString> GetAllUsedLocalGenerateFunctionParameters(UK2Node* EntryNode, FString EntryPinName);

    FString GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack);

    FString GenerateLocalVariablesCodeFromEntryNode(UK2Node* EntryNode, TArray<UK2Node*> MacroStack = TArray<UK2Node*>());

};
