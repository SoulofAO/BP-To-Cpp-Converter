/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright 2025 AO All Rights Reserved.
 */

#include "TranslatorObjects/Delegates/CreateDelegateTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"
#include "NativeApiCatalog.h"
#include "RetainedBlueprintGeneration.h"

FGenerateResultStruct UCreateDelegateTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
    UK2Node_CreateDelegate* CreateDelegate = CastChecked<UK2Node_CreateDelegate>(Node);
    UClass* TargetClass = CreateDelegate->GetScopeClass(true);
    if (!TargetClass)
    {
        FNativeApiCatalog::AddError(TEXT("Cannot resolve the target class of a delegate binding."));
        return FGenerateResultStruct();
    }
    if (UBlueprint* TargetBlueprint = Cast<UBlueprint>(TargetClass->ClassGeneratedBy))
    {
        TargetClass = TargetBlueprint->GeneratedClass;
    }
    UFunction* Function = TargetClass->FindFunctionByName(CreateDelegate->GetFunctionName());
    FGenerateResultStruct Result = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, CreateDelegate->GetObjectInPin(), 0, MacroStack);
    if (Result.Code.IsEmpty())
    {
        Result.Code = TEXT("this");
    }
    FString FunctionName;
    if (Function)
    {
        if (FRetainedBlueprintGeneration::IsRetained(Function->GetOwnerClass()))
        {
            FunctionName = Function->GetName();
        }
        else
        {
            FunctionName = UBlueprintNativizationLibrary::GetUniqueFunctionName(Function, TEXT(""));
        }
    }
    else
    {
        const FGenerateFunctionStruct* GeneratedFunction = NativizationV2Subsystem->EntryNodes.FindByPredicate(
            [&](const FGenerateFunctionStruct& Candidate)
            {
                return TargetClass == Node->GetBlueprint()->GeneratedClass && Candidate.Node &&
                    UBlueprintNativizationLibrary::GetEntryFunctionNameByNode(Candidate.Node) == CreateDelegate->GetFunctionName();
            });
        if (!GeneratedFunction)
        {
            FNativeApiCatalog::AddError(FString::Format(TEXT("Cannot resolve delegate function {0} in {1}."),
                {CreateDelegate->GetFunctionName().ToString(), TargetClass->GetPathName()}));
            return FGenerateResultStruct();
        }
        FunctionName = GeneratedFunction->Name.ToString();
    }
    FString CallbackName = NativizationV2Subsystem->GetNodeStateName(Node, TEXT("Callback"), true);
    if (NativizationV2Subsystem->DataConsumer)
    {
        CallbackName = NativizationV2Subsystem->GetNodeStateName(NativizationV2Subsystem->DataConsumer,
            NativizationV2Subsystem->InputEvaluationPurpose + CallbackName, true);
    }
    Result.Preparations.Add(TEXT("FScriptDelegate ") + CallbackName + TEXT(";\n") + CallbackName +
        TEXT(".BindUFunction(") + Result.Code + TEXT(", TEXT(\"") + FunctionName + TEXT("\"));\n"));
    Result.Code = CallbackName;
    return Result;
}
