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
#include "Engine/SCS_Node.h"
#include "BlueprintNativizationLibrary.generated.h"


class UNativizationV2Subsystem;
class UK2Node_Variable;
class UK2Node_Event;
class UEditorUtilityWidget;
class UEditorUtilityWidgetBlueprint;
const FName ConversionMetaKey(TEXT("ConversionFlag"));


UCLASS()
class UBlueprintNativizationLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    static bool IsExternalEnum(const UEnum* Enumeration);
    static bool RequiresPostCDOInitialization(FProperty* Property, const void* PropertyAddress, bool bIncludeNativeDependencies = true);
    static FProperty* FindFunctionProperty(UFunction* Function, FName PropertyName, bool bParameter);
    static FProperty* ResolveVariableProperty(UK2Node_Variable* Variable);

    static FGenerateResultStruct GenerateVariableAccess(UK2Node_Variable* Variable, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem);

    //Find all Exec Non Pure Function
    static TArray<UK2Node*> GetAllExecuteNodesInFunction(UK2Node * EntryNode, TArray<FGenerateFunctionStruct> EntryNodes, FString EntryPinName = "None");

    static TArray<UK2Node*> GetAllExecuteNodesInFunction(UK2Node * EntryNode, FString EntryPinName = "None");

    static TArray<UK2Node*> GetAllExecuteNodesInFunctionWithMacroSupported(UK2Node * EntryNode, UNativizationV2Subsystem* NativizationV2Subsystem, FString EntryPinName = "None", TArray<UK2Node*> MacroStack = {});

    static bool CheckLoopInFunction(UK2Node * EntryNode, TArray<FGenerateFunctionStruct> EntryNodes, FString EntryPinName = "None");

    //Find all function for Execs Nodes. 
    static TArray<UK2Node*> GetAllInputNodes(TArray<UK2Node*> AllNodes, bool OnlyPureNode = true);

    static UEdGraphPin * FindClosestPinByName(const TArray<UEdGraphPin*>& Pins, const FString & OriginalName, int32 MinScore =-1);

    UFUNCTION(BlueprintCallable, Category = "Nativization")
    static void CloseTabsWithSpecificAssets(TArray<UObject*>& Assets);

    UFUNCTION(BlueprintCallable, Category = "Nativization")
    static TArray<FName> GetAllBlueprintCallableNames(UBlueprint* Blueprint, bool RemoveAllMacro = true, bool bUseDisplayName = true);

    static UBlueprint* GetBlueprintFromFunction(UFunction* Function);

    static FString GetFunctionNameByEntryNode(UEdGraphNode * EntryNode);

    static FString GetFunctionNameByInstanceNode(UK2Node * Node);

    static UK2Node * FindEntryNodeByFunctionName(UBlueprint* Blueprint, const FName& TargetNodeName);

    static UFunction* FindFunctionByFunctionName(UBlueprint* Blueprint, const FName& TargetNodeName);
    static UFunction* GetFunctionDeclaration(UFunction* Function);

    static FProperty * GetPropertyFromEntryNode(UK2Node * EntryNode, const FName& PropertyName);

    static UEdGraph* GetEdGraphByFunction(UFunction* Function);

    static BLUEPRINTNATIVIZATIONV2_API bool ShouldGenerateEvent(const UK2Node_Event* Event);

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

    static FString GetUniquePropertyName(FProperty* Property);

    static FString GetUniquePinName(UEdGraphPin* Pin, TArray<FGenerateFunctionStruct> EntryNodes);

    static FString GenerateLambdaConstructorUniqueVariableName(UClass* Class, FString MainName);

    static FString GetLambdaUniqueVariableNameByNode(FString MainName, UK2Node* LambdaNode, TArray<FGenerateFunctionStruct> EntryNodes);

    static FString GetLambdaUniqueVariableNameByFunction(FString MainName, UFunction* Function, TArray<FGenerateFunctionStruct> EntryNodes);

    static FString GetLambdaUniqueVariableNameByClass(FString MainName, UClass* OwnerClass, const FString& SourceIdentity = FString());

    static bool IsNodeInCurrentFunctionNode(UK2Node* TargetNode, UK2Node* StartNode, TArray<FGenerateFunctionStruct> EntryNodes);

    static bool IsPinAllLinkInCurrentFunctionNode(UEdGraphPin* EdGraphPin, UK2Node* EntryNode, TArray<FGenerateFunctionStruct> EntryNodes);

    static void ResetNameSubsistem();

	UFUNCTION(BlueprintCallable, Category = "Nativization")
    static bool IsModuleLoaded(const FName& ModuleName);

    static UFunction* GetRootFunctionDeclaration(const UFunction* Function);

    static FString GetNetworkEventBodyName(UFunction* Function);

    static TArray<FName> GetAllPropertyNamesByStruct(UScriptStruct* Struct);

    static FName GetEntryFunctionNameByNode(UK2Node* Node);

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

    static FString GetEnumValueName(const UEnum* Enumeration, int64 Value);
    static FString GenerateEnumLiteral(const UEnum* Enumeration, int64 Value);

    static FString GenerateTextLiteral(const FText& Value, const FString& TextNamespace = FString(), const FString& TextName = FString());

    static FString GenerateOneLineDefaultConstructorForProperty(FProperty* Property, void* PropertyAddress, const FString& ObjectOuter = TEXT("this"));

    static UEdGraph* GetConstructionGraph(UBlueprint* Blueprint);

    //GENERATE CONSTRUCTOR ONLY FOR UCLASS CONSTRUCTOR OR PIN CONSTRUCTOR
    static FString GenerateManyLinesDefaultConstructorForProperty(FProperty* Property, void* PropertyAddress, void* SuperPropertyAddress, TArray<FGenerateFunctionStruct> EntryNodes, UClass* Class, bool FullInitialize, FString PrefixComponentName, FString& NewPropertyName, const FString& ObjectOuter = TEXT("this"));

    static FString ProcessFloatStrings(const FString& Input);

    static FString TrimFloatToMeaningfulString(FString Value);

    static bool IsNotOnlyHyphenAndBrackets(FString Input);

    static bool IsAvailibleOneLineDefaultConstructor(FEdGraphPinType Type);

    static FString GenerateManyLinesDefaultConstructor(FString Name, FEdGraphPinType Type, FString DefaultValue, UClass* Class, bool IsConstructorFunction, UK2Node* EntryNode, TArray<FGenerateFunctionStruct> EntryNodes, bool FullInitialize, FString& OutVariableName);

    static FString GenerateOneLineDefaultConstructor(FString Name, FEdGraphPinType Type, FString DefaultValue, UObject* DefaultObject);

    static FString GetStableFieldIdentifier(const UField* Field);

    static  UField* FindFieldByStableIdentifier(const FString& Identifier);

    //MAIN FUNCTIONS TO REPLACE
    UFUNCTION(BlueprintCallable, Category = "Nativization")
    static void ReplaceReferencesToTypeObjectTest(UObject* OldType, UObject* NewType);

    UFUNCTION(BlueprintCallable, Category = "Nativization")
    static void ReplaceReferencesToType(UField* OldType, UField* NewType, TArray<UBlueprint*>& Blueprints, bool bDeferReconstruction = false, bool bReplaceParentClass = true);

    UFUNCTION(BlueprintCallable, Category = "Nativization")
    static UBlueprint* GetBlueprintFromAsset(UObject* Asset);

    BLUEPRINTNATIVIZATIONV2_API static bool DoesPropertyContainAssetReference(FProperty* Property, const void* ContainerPtr);

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

    static BLUEPRINTNATIVIZATIONV2_API FString GetPropertyType(FProperty* Property);
    static FString ConvertArrayValue(UEdGraphPin* SourcePin, const FEdGraphPinType& TargetType, const FString& Expression);

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

    static BLUEPRINTNATIVIZATIONV2_API FString ValidateBlueprintSources(const TArray<UObject*>& InputTargets);

    //Depedence
    UFUNCTION(BlueprintCallable, Category = "Nativization")
    static TSet<UObject*> GetAllDependencies(UObject* InputTarget, TArray<TSubclassOf<UObject>> IgnoreClasses, TArray<UObject*> IgnoreObjects);

    static bool	HasAnyChildInClasses(UClass* Class, TArray<UClass*> Classes);

    static bool HasAnyChildInClasses(UClass* Class, TArray<TSubclassOf<UObject>> Classes);

    static bool HasAnyChildInClasses(UClass* Class, TArray<TSoftClassPtr<UObject>> Classes);

    UFUNCTION(BlueprintCallable, Category = "Nativization")
    static void CollectDependencies(UObject* InputTarget, TSet<UObject*>& OutObjects);

    static TArray<UActorComponent*> GetClassComponentTemplates(TArray<FName>& FoundComponentNames, const TSubclassOf<AActor> ActorClass, const TSubclassOf<UActorComponent> ComponentClass, bool bIgnoreSuperClasses = false);

    static BLUEPRINTNATIVIZATIONV2_API USceneComponent* GetParentComponentTemplate(UActorComponent* ActorComponent);
    static BLUEPRINTNATIVIZATIONV2_API FName GetComponentAttachmentSocketName(UActorComponent* ActorComponent);

    BLUEPRINTNATIVIZATIONV2_API static bool IsRootComponentTemplate(UActorComponent* ActorComponent);

    static FName GetRealBPPropertyName(UActorComponent* ActorComponent);

    static FString GetUniquePropertyComponentGetterName(UActorComponent* ActorComponent, const TArray<FGenerateFunctionStruct>& EntryNodes, FString* ComponentPropertyName = nullptr);

    static UActorComponent * GetComponentByPropertyName(FString PropertyName, UBlueprint * Blueprint);

    static USCS_Node* FindSCSNodeForComponent(UActorComponent* ActorComponent);

    static USCS_Node* FindSCSNodeForPropertyName(FString PropertyName, UBlueprint* Blueprint);

    UFUNCTION(BlueprintCallable, Category = "Nativization")
    static TArray<FString> OpenFileDialog(FString FileTypes = TEXT("All Files (*.*)|*.*"));

};
