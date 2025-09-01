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
#include "Engine/SCS_Node.h"
#include "EditorUtilityWidget.h"
#include "EditorUtilityWidgetBlueprint.h"
#include "BlueprintNativizationLibrary.generated.h"


class UNativizationV2Subsystem;
const FName ConversionMetaKey(TEXT("ConversionFlag"));


UCLASS()
class UBlueprintNativizationLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    //Find all Exec Non Pure Function
    static TArray<UK2Node*> GetAllExecuteNodesInFunction(UK2Node * EntryNode, TArray<FGenerateFunctionStruct> EntryNodes, FString EntryPinName = "None");

    static TArray<UK2Node*> GetAllExecuteNodesInFunction(UK2Node * EntryNode, FString EntryPinName = "None");

    static TArray<UK2Node*> GetAllExecuteNodesInFunctionWithMacroSupported(UK2Node * EntryNode, UNativizationV2Subsystem* NativizationV2Subsystem, FString EntryPinName = "None", TArray<UK2Node*> MacroStack = {});

    static bool CheckLoopInFunction(UK2Node * EntryNode, TArray<FGenerateFunctionStruct> EntryNodes, FString EntryPinName = "None");

    //Find all function for Execs Nodes. 
    static TArray<UK2Node*> GetAllInputNodes(TArray<UK2Node*> AllNodes, bool OnlyPureNode = true);

    static UEdGraphPin* FindOptimalPin(const TArray<UEdGraphPin*>& SubPins, const FString& OriginalPropertyName);

    UFUNCTION(BlueprintCallable, Category = "Nativization")
    static void CloseTabsWithSpecificAssets(TArray<UObject*>& Assets);

    static UBlueprint* GetBlueprintFromFunction(UFunction* Function);

    static FString GetFunctionNameByEntryNode(UEdGraphNode * EntryNode);

    static FString GetFunctionNameByInstanceNode(UK2Node * Node);

    static UK2Node * FindEntryNodeByFunctionName(UBlueprint* Blueprint, const FName& TargetNodeName);

    static UFunction* FindFunctionByFunctionName(UBlueprint* Blueprint, const FName& TargetNodeName);

    static FProperty * GetPropertyFromEntryNode(UK2Node * EntryNode, const FName& PropertyName);

    static UEdGraph* GetEdGraphByFunction(UFunction* Function);

    static TArray<UK2Node*> GetEntryNodesPerGraph(UEdGraph* EdGraph, bool IncludeMacro = true);

    static UK2Node* GetEntryNodesByFunction(UFunction* Function);

    static FString RemoveBPPrefix(const FString& In);

    static FBPVariableDescription* GetVariableDescriptionByPropertyAndBlueprint(FProperty* Property, UBlueprint* Blueprint);

    static UBlueprint * GetBlueprintByProperty(FProperty* Property);

    //Function
    static FString ConversionFunctionFlagToString(EConversionFunctionFlag Flag);

    static EConversionFunctionFlag ConversionFunctionFlagFromString(const FString& In);

    static EConversionFunctionFlag GetConversionFunctionFlagForFunction(UK2Node* ContextNode);

    static void SetConversionFunctionFlagForFunction(UK2Node* ContextNode, EConversionFunctionFlag Flag);

    //Property
    static FString ConversionPropertyFlagToString(EConversionPropertyFlag Flag);

    static EConversionPropertyFlag ConversionPropertyFlagFromString(const FString& In);

    static EConversionPropertyFlag GetConversionPropertyFlagForProperty(FProperty * Property, UBlueprint* Blueprint);

    static void SetConversionPropertyFlagForProperty(FProperty* Func, UBlueprint* Blueprint, EConversionPropertyFlag Flag);

    static FString GetUniqueFieldName(UField* Field);

    static FString GetUniqueUInterfaceFieldName(TSubclassOf<UInterface> Field);

    static FString GetUniqueIInterfaceFieldName(TSubclassOf<UInterface> Field);

    static FString GetUniqueEntryNodeName(UK2Node* EntryNode, TArray<FGenerateFunctionStruct> EntryNodes, FString PredictName);

    static FString GetUniqueFunctionName(UFunction* Function, FString PredictName);

    static FString GetUniquePropertyName(FProperty* Property, TArray<FGenerateFunctionStruct> EntryNodes);

    static FString GetUniquePinName(UEdGraphPin* Pin, TArray<FGenerateFunctionStruct> EntryNodes);

    static FString GenerateLambdaConstructorUniqueVariableName(UClass* Class, FString MainName);

    static FString GetLambdaUniqueVariableNameByNode(FString MainName, UK2Node* LambdaNode, TArray<FGenerateFunctionStruct> EntryNodes);

    static FString GetLambdaUniqueVariableNameByFunction(FString MainName, UFunction* Function, TArray<FGenerateFunctionStruct> EntryNodes);

    static FString GetLambdaUniqueVariableNameByClass(FString MainName, UClass* OwnerClass);

    static bool IsNodeInCurrentFunctionNode(UK2Node* TargetNode, UK2Node* StartNode, TArray<FGenerateFunctionStruct> EntryNodes);

    static bool IsPinAllLinkInCurrentFunctionNode(UEdGraphPin* EdGraphPin, UK2Node* EntryNode, TArray<FGenerateFunctionStruct> EntryNodes);

    static void ResetNameSubsistem();

	UFUNCTION(BlueprintCallable, Category = "Nativization")
    static bool IsModuleLoaded(const FName& ModuleName);

    static UFunction* GetRootFunctionDeclaration(const UFunction* Function);

    static TArray<FName> GetAllPropertyNamesByStruct(UScriptStruct* Struct);

    static FName GetOriginalFunctionNameByNode(UK2Node* Node);

    UFUNCTION(BlueprintCallable, Category = "Nativization")
    static UEditorUtilityWidget* OpenEditorUtilityWidget(UEditorUtilityWidgetBlueprint* WidgetBlueprint);

    UFUNCTION(BlueprintCallable, Category = "Nativization")
    static void OpenWidgetToFullscreen(UEditorUtilityWidget* Widget);

    static FString TrimAfterLastComma(const FString& Input);

    static bool IsCppDefinedField(UField* Field);

    static bool IsCppDefinedProperty(FProperty* Property);

    static TArray<UStruct*> FindStructsContainingProperty(FProperty* TargetProperty);

    static bool DoesFieldContainProperty(const FProperty* TargetProperty, const UField* ContainerField);

    static bool IsAvailibleOneLineDefaultConstructorForProperty(FProperty* Property);

    static FString GenerateOneLineDefaultConstructorForProperty(FProperty* Property, bool LeftAllAssetRefInBlueprint, void* PropertyAddress);

    static UEdGraph* GetConstructionGraph(UBlueprint* Blueprint);

    //GENERATE CONSTRUCTOR ONLY FOR UCLASS CONSTRUCTOR OR PIN CONSTRUCTOR
    static FString GenerateManyLinesDefaultConstructorForProperty(FProperty* Property, void* PropertyAddress, void* SuperPropertyAddress, TArray<FGenerateFunctionStruct> EntryNodes, UClass* Class, bool FullInitialize, bool LeftAllAssetRefInBlueprint, FString PrefixComponentName, FString& NewPropertyName);

    static FString ProcessFloatStrings(const FString& Input);

    static FString TrimFloatToMeaningfulString(FString Value);

    static bool IsNotOnlyHyphenAndBrackets(FString Input);

    static bool IsAvailibleOneLineDefaultConstructor(FEdGraphPinType Type);

    static FString GenerateManyLinesDefaultConstructor(FString Name, FEdGraphPinType Type, FString DefaultValue, UClass* Class, bool IsConstructorFunction, UK2Node* EntryNode, TArray<FGenerateFunctionStruct> EntryNodes, bool FullInitialize, bool LeftAllAssetRefInBlueprint, FString& OutVariableName);

    static FString GenerateOneLineDefaultConstructor(FString Name, FEdGraphPinType Type, FString DefaultValue, UObject* DefaultObject, bool LeftAllAssetRefInBlueprint);

    static FString GetStableFieldIdentifier(const UField* Field);

    static  UField* FindFieldByStableIdentifier(const FString& Identifier);

    //MAIN FUNCTIONS TO REPLACE
    UFUNCTION(BlueprintCallable, Category = "Nativization")
    static void ReplaceReferencesToTypeObjectTest(UObject* OldType, UObject* NewType);

    UFUNCTION(BlueprintCallable, Category = "Nativization")
    static void ReplaceReferencesToType(UField* OldType, UField* NewType, TArray<UBlueprint*>& Blueprints);

    UFUNCTION(BlueprintCallable, Category = "Nativization")
    static UBlueprint* GetBlueprintFromAsset(UObject* Asset);

    static bool DoesPropertyContainAssetReference(FProperty* Property, const void* ContainerPtr);

    UFUNCTION(BlueprintCallable, Category = "Nativization")
    static void ClearBlueprint(UBlueprint* Blueprint);

    UFUNCTION(BlueprintCallable, Category = "Nativization")
    static FString ExtractNestedParam(const FString& Input, const FString& ParamName);

    UFUNCTION(BlueprintCallable, Category = "Nativization")
    static FString GetStructPart(const FString& In, int32 Index);

    static TMap<UField*, FName> GetAllRegisterUniqueFieldsInNameSubsistem();

    static UFunction* GetFunctionByNodeAndEntryNodes(UK2Node* Node, TArray<FGenerateFunctionStruct> EntryNodes);

    static UFunction* GetFunctionByNode(UK2Node* Node);

    static bool GetEntryByNodeAndEntryNodes(UK2Node* Node, TArray<FGenerateFunctionStruct> EntryNodes, FGenerateFunctionStruct& Result);

    static UK2Node* GetEntryNodeByNode(UK2Node* Node);

    static FString GetPinType(FEdGraphPinType& PinType, bool HideUselessTypeDeclaration);

    static FString GetPropertyType(FProperty* Property);

    static TArray<FProperty*> GetExposeOnSpawnProperties(UClass* Class);

    static bool CheckAnySubPinLinked(UEdGraphPin* Pin);

    static void ConvertPropertyToPinType(const FProperty* Property, FEdGraphPinType& OutPinType);

    static bool ContainsAssetInPinType(const FEdGraphPinType& PinType, FString DefaultValue, UObject* DefaultObject);

    static void SplitIgnoringParentheses(const FString& Input, TArray<FString>& OutParts);

    static UEdGraphPin* GetParentPin(UEdGraphPin* Pin);

    static TArray<UEdGraphPin*> GetParentPathPins(UEdGraphPin* ChildPin);

    static UEdGraph* GetGraphByCompositeNode(UBlueprint* Blueprint, UK2Node_Composite* GraphNode_Composite);

    static TArray <UK2Node*> GetReturnNodes(UK2Node* InputNode, TArray<FGenerateFunctionStruct> EntryNodes);

    static TArray <UK2Node*> GetReturnNodesPerGraph(UEdGraph* EdGraph);

    static bool FindFieldModuleName(UField* InField, FString& ModuleName);

    static TArray<UEdGraphPin*> GetFilteredPins(
        UK2Node* Node,
        EPinOutputOrInputFilter DirectionFilter = EPinOutputOrInputFilter::All,
        EPinExcludeFilter ExcludeFlags = EPinExcludeFilter::None,
        EPinIncludeOnlyFilter IncludeOnly = EPinIncludeOnlyFilter::None
    );

    static int32 LevenshteinDistance_FString(FString A, FString B);

    static FProperty* FindClosestPropertyByName(TArray<FProperty*>& Properties, const FName& TargetName);

    static TArray<FProperty*> GetAllPropertiesByFunction(UFunction* Function);

    static UEdGraphPin* GetPinByName(TArray<UEdGraphPin*> Pins, FString Name);

    static TArray<UEdGraphPin*> GetParentPins(TArray<UEdGraphPin*> Pins);

    static UEdGraphPin * GetFirstNonKnotPin(UEdGraphPin* Pin, int Index, bool IgnoreMacro, TArray<UK2Node*>& MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem);

    //Depedence
    UFUNCTION(BlueprintCallable, Category = "Nativization")
    static TSet<UObject*> GetAllDependencies(UObject* InputTarget, TArray<TSubclassOf<UObject>> IgnoreClasses, TArray<UObject*> IgnoreObjects);

    UFUNCTION(BlueprintCallable, Category = "Nativization")
    static void CollectDependencies(UObject* InputTarget, TSet<UObject*>& OutObjects);

    static TArray<UActorComponent*> GetClassComponentTemplates(TArray<FName>& FoundComponentNames, const TSubclassOf<AActor> ActorClass, const TSubclassOf<UActorComponent> ComponentClass, bool bIgnoreSuperClasses = false);

    static USceneComponent* GetParentComponentTemplate(UActorComponent* ActorComponent);

    static bool IsRootComponentTemplate(UActorComponent* ActorComponent);

    static FName GetRealBPPropertyName(UActorComponent* ActorComponent);

    static FString GetUniquePropertyComponentGetterName(UActorComponent* ActorComponent, const TArray<FGenerateFunctionStruct>& EntryNodes);

    static USCS_Node* FindSCSNodeForComponent(UActorComponent* ActorComponent);

    UFUNCTION(BlueprintCallable, Category = "Nativization")
    static TArray<FString> OpenFileDialog(FString FileTypes = TEXT("All Files (*.*)|*.*"));

};