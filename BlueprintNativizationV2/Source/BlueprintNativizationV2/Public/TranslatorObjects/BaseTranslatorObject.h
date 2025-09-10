/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */


#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Engine/DeveloperSettings.h"
#include "BlueprintNativizationData.h"
#include "BlueprintNativizationV2.h"
#include "BaseTranslatorObject.generated.h"

/**
 * 
 */

class UNativizationV2Subsystem;

UCLASS(Abstract)
class BLUEPRINTNATIVIZATIONV2_API UTranslatorBPToCppObject : public UObject
{
    GENERATED_BODY()

public:
    virtual void GenerateNewFunction(UK2Node* InputNode, TArray<UK2Node*> Path, TArray<FGenerateFunctionStruct>& EntryNodes, UNativizationV2Subsystem* NativizationV2Subsystem);

    virtual FString GenerateGlobalVariables(UK2Node* InputNode, FString Prefix, UNativizationV2Subsystem* NativizationV2Subsystem);

    virtual TSet<FString> GenerateLocalVariables(UK2Node* InputNode, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem);

    //ќграничивает возможность генерации переменных как локальных, или глобальных переменных. ѕока идей, дл€ чего это может пригодитс€, нет. 
    virtual bool CanGenerateOutPinVariable(UEdGraphPin* Pin, UNativizationV2Subsystem* NativizationV2Subsystem) { return true; };

    virtual FString GenerateConstruction(UK2Node* InputNode, FString Prefix, UNativizationV2Subsystem* NativizationV2Subsystem) { return FString(); };

    virtual FString GenerateSetupPlayerInputComponentFunction(UK2Node* InputNode, FString Prefix, UNativizationV2Subsystem* NativizationV2Subsystem) { return FString(); };

    //ѕр€ма€ генераци€.
    virtual FString GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, TSet<FString>& Preparations, UNativizationV2Subsystem* NativizationV2Subsystem) { return FString(); };

    //≈сли да, то продолжит последовательную генерацию дл€ всех локальных Exec пинов в Out направлении. ¬ключена по умолчанию и должнв быть включена дл€ всех нод, которые не затрагивают контроль исполнени€ и не латенны.
    virtual bool CanContinueCodeGeneration(UK2Node* InputNode, FString EntryPinName);

    virtual TSet<FString> GenerateHeaderIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem);

    virtual TSet<FString> GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem);

    virtual TSet<FString> GenerateCSPrivateDependencyModuleNames(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem);

    //ќбратна€ генераци€.
    virtual FGenerateResultStruct GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem);

    //«апрещает дальнейшие попытки обработку системами PinSplit. ѕолезно только в случае, когда пин уже распаршен и имеет нестандартный механизм сплитинга (как, например, в Enhance).
    virtual bool CanContinueGenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem);

protected:
    virtual FString GenerateLocalFunctionVariablesToHeaderVariables(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem);

    virtual TSet<FString> GenerateLocalFunctionVariables(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem);

    virtual FString GenerateNewPreparations(TSet<FString> OldPreparations, TSet<FString> NewPreparations);

public:
    virtual bool CanApply(UK2Node* Node);

    virtual bool CanExposeMacro(UK2Node* InputNode)
    {
        return false;
    }

    //For instructions like Break in For(). These are instructions that do not loop the graph, but affect its behavior.
    virtual bool CanCreateCircleWithPin(UK2Node* Node, FString EnterPinName)
    {
        return true;
    }

    TArray<TSubclassOf<UK2Node>> ClassNodeForCheck;

    TArray<FString> NodeNamesForCheck = {};

};

UCLASS(Abstract)
class BLUEPRINTNATIVIZATIONV2_API UTranslatorMacroBPToCppObject : public UTranslatorBPToCppObject
{
    GENERATED_BODY()

public:
    virtual TSubclassOf<UK2Node> GetBaseClass()
    {
        return UK2Node::StaticClass();
    }
    virtual bool CanContinue()
    {
        return false;
    }
};

