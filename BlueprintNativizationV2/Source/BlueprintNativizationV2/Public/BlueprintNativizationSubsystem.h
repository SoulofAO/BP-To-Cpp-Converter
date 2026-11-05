/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright 2025 AO All Rights Reserved.
 */

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_Composite.h"
#include "BlueprintNativizationData.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "Misc/CompilationResult.h"
#include "RepeatedCallGeneration.h"
#include "BlueprintNativizationSubsystem.generated.h"

class FJsonObject;

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

UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UNativizationV2Subsystem : public UEngineSubsystem
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
        bool SaveCache = false, FString SaveCachePath = "", bool bContinueOnError = false);

    UFUNCTION(BlueprintCallable, Category = "Nativization")
    FNativizationCode RunNativizationForOneFunctionBlueprint(UBlueprint* InputTarget, FString FunctionName = "");

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
    TFunction<bool(int32, int32, const FString&)> OperationProgress;

    UPROPERTY(BlueprintReadOnly, Category = "Nativization")
    TArray<UTranslatorBPToCppObject*> TranslatorObjects;

    TArray<FGenerateFunctionStruct> EntryNodes;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UK2Node>> TimelineInitializationNodes;

    void LoadTranslatorObjects();

    bool CreateFile(const FString& FilePath, const FString& Content);

    void EnsureDirectory(const FString& Directory);

    bool SaveNativizationResult(FNativizationModuleResult NativizationModuleResult, FString Directory, FString& OutputDirectory, bool MergeWithExistModule);

    FNativizationCode GenerateUnrealModule(const FString& ModuleName, TArray<UObject*> Targets);

    FString GenerateUnrealBuildCsModule(const FString& ModuleName, TArray<UObject*> Targets);

    void CreateJSONCache(FString CacheDirectory);
    bool TryPrepareNewModule(FString& Error);
    bool CaptureConfigurationTemplates(const TMap<UField*, FName>& TranslatedObjects, const TMap<FString, FString>& MemberNames, TSharedPtr<FJsonObject>& Result, FString& Error) const;
    bool WriteJSONCache(FString CacheDirectory, const TMap<UField*, FName>& TranslatedObjects, const TMap<FString, FString>& MemberNames = {}, const TMap<FString, FString>& DefaultOnlyClassReplacements = {});
    TMap<FString, FString> CaptureMigrationMemberNames() const;
    TMap<FString, FString> CaptureMigrationClassReplacements() const;
    bool ApplyMigrationTypes(const TMap<UField*, UField*>& TypeMappings, const TMap<FString, FString>& MemberNames, FString& Error, bool bPreviewOnly = false, const TMap<UClass*, UClass*>& DefaultOnlyClassReplacements = {}, bool bContinueOnError = false, const TSharedPtr<FJsonObject>& ConfigurationTemplates = nullptr);
    bool TryReapplyJSONCache(FString CacheFilePath, FString& Error, bool bPreviewOnly = false, FString* Summary = nullptr, bool bContinueOnError = false);

    UFUNCTION(BlueprintCallable, Category = "Nativization")
    bool ReapplyJSONCache(FString CacheFilePath);

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
    static bool ShouldGenerateBlueprintNativeEvent(const UFunction* Function);

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

    TArray<UK2Node*> GetAllContextNodes();

    TSet<FString> GetInputParametersForEntryNode(UK2Node* EntryNode, bool bReflectedDeclaration = false);

    FString ProcessGraphsForCodeGeneration(const FGenerateFunctionStruct& Function);

    FString GenerateSetupPlayerInputComponentCppCode(UBlueprint* Blueprint);

    FGenerateResultStruct GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack);

    TSet<FString> GetAllUsedLocalGenerateFunctionParameters(UK2Node* EntryNode, FString EntryPinName);

    FString GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, FGenerationPreparations Preparations, TArray<UK2Node*> MacroStack);

    FString GenerateLocalVariablesCodeFromEntryNode(UK2Node* EntryNode, TArray<UK2Node*> MacroStack = TArray<UK2Node*>());

    UK2Node* LocalVariableEntryNode = nullptr;
    UK2Node* DataConsumer = nullptr;
    FString InputEvaluationPurpose = TEXT("Input");
    bool bRequiresInputStorage = false;
    TSet<UK2Node*> DeferredExecutionNodes;
    UK2Node* ValidatedCallNode = nullptr;
    FString ValidatedCallReceiver;
    TArray<UK2Node*> ActiveLoopNodes;
    TMap<UEdGraphPin*, FString> PersistentEntryParameters;
    void RegisterPersistentEntryParameters();
    TMap<FString, FGenerateResultStruct> PureFunctionCalls;
    TMap<FString, FGenerateResultStruct> InputValues;
    TSet<FString> ActiveInputValues;
    FGenerationPreparations EmittedInputPreparations;
    TArray<UK2Node*> CurrentMacroStack;
    TArray<FGenerationNodeInstance> GetNodeInstances(UK2Node* EntryNode, const TArray<UK2Node*>& MacroStack, bool bIncludeInputs);
    uint64 EvaluationSequence = 0;
    uint64 CurrentEvaluation = 0;
    FString GetEvaluationKey(UK2Node* Node) const;
    FGenerateResultStruct GenerateUncachedInputParameter(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack);
    FString GeneratePurePreparations(UK2Node* Consumer, TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations);
    FString GetNodeStateName(UK2Node* Node, const FString& Suffix, bool bLocal = false);
    UEdGraphPin* FindCommonExecutionTail(UEdGraphPin* FirstBranch, UEdGraphPin* SecondBranch);
    UEdGraphPin* FindCommonExecutionTail(const TArray<UEdGraphPin*>& Branches);
    FString GenerateControlFlowFunctions(const FGenerateFunctionStruct& Function);
    FString RegisterRepeatedCall(const FString& Signature, const FString& Name, const FString& Definition,
        const FString& ExpandedCode, const FString& Arguments, const FString& ResultName);
    FString ResolveRepeatedCalls(FString Content) const;
    FString GenerateFunctionReturnStatement(const FString& Value) const;
    FString GenerateControlFunctionCall(int32 FunctionIndex) const;
    int32 GetLatentActionIdentifier(UK2Node* Node);

private:
    bool bCollectRepeatedCalls = false;
    TArray<FRepeatedCallSite> RepeatedCallSites;
    TArray<FVisitedNodeStack> ControlFunctionEntries;
    TArray<FString> ControlFunctionNames;
    bool bGeneratingControlFunctionEntry = false;
    bool bInsideControlFunction = false;
    bool bPropagateFunctionReturn = false;
    FString FunctionReturnStateName;
    FString FunctionResultName;
    TMap<UK2Node*, int32> LatentActionIdentifiers;

};
