/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright 2025 AO All Rights Reserved.
 */

#include "TranslatorObjects/TemporaryVariableTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "GenerationGraphContext.h"
#include "BlueprintNativizationSubsystem.h"
#include "Engine/Blueprint.h"
#include "NativeApiCatalog.h"
#include "K2Node_CallFunction.h"

FGenerateResultStruct UTemporaryVariableTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node,
    UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem)
{
    if (Node->IsA<UK2Node_PureAssignmentStatement>())
    {
        FNativeApiCatalog::AddError(TEXT("PureAssignmentStatement requires compiler-dependent ordering of side effects and is not supported by direct graph traversal: ") + Node->GetPathName());
        return {};
    }
    if (UK2Node_Copy* Copy = Cast<UK2Node_Copy>(Node))
    {
        if (const FGenerateResultStruct* Cached = Subsystem->PureFunctionCalls.Find(Subsystem->GetEvaluationKey(Node)))
        {
            return *Cached;
        }
        FGenerateResultStruct Result;
        {
            const FGenerateResultStruct Input = Subsystem->GenerateInputParameterCodeForNode(Node,
                Copy->GetInputReferencePin(), 0, MacroStack);
            FString Name = Subsystem->GetNodeStateName(Node, TEXT("CopiedValue"), true);
            if (Subsystem->DataConsumer)
            {
                Name = Subsystem->GetNodeStateName(Subsystem->DataConsumer, Subsystem->InputEvaluationPurpose + Name, true);
            }
            Result.Code = Name;
            Result.Preparations = Input.Preparations;
            Result.Preparations.Add(FString::Format(TEXT("auto {0} = {1};\n"), { Name, Input.Code }));
        }
        Subsystem->PureFunctionCalls.Add(Subsystem->GetEvaluationKey(Node), Result);
        return Result;
    }
    return Subsystem->GetNodeStateName(Node, TEXT("State"));
}

TSet<FString> UTemporaryVariableTranslatorObject::GenerateLocalVariables(UK2Node* InputNode,
    TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem)
{
    UK2Node_TemporaryVariable* Variable = Cast<UK2Node_TemporaryVariable>(InputNode);
    if (Variable && FGenerationGraphContext::IsFunctionGraph(Variable))
    {
        return { FString::Format(TEXT("{0} {1} = {};\n"), {
            UBlueprintNativizationLibrary::GetPinType(Variable->GetVariablePin()->PinType, false),
            Subsystem->GetNodeStateName(Variable, TEXT("State")) }) };
    }
    return {};
}

FString UTemporaryVariableTranslatorObject::GenerateGlobalVariables(UK2Node* InputNode, FString Prefix,
    UNativizationV2Subsystem* Subsystem)
{
    UK2Node_TemporaryVariable* Variable = Cast<UK2Node_TemporaryVariable>(InputNode);
    if (Variable && !FGenerationGraphContext::IsFunctionGraph(Variable))
    {
        FString Declaration = TEXT("UPROPERTY(Transient)\n");
        if (Variable->bIsPersistent)
        {
            Declaration = TEXT("UPROPERTY(SaveGame)\n");
        }
        return Declaration + FString::Format(TEXT("{0} {1} = {};\n"), {
            UBlueprintNativizationLibrary::GetPinType(Variable->GetVariablePin()->PinType, false),
            Subsystem->GetNodeStateName(Variable, TEXT("State")) });
    }
    return FString();
}

FString UTemporaryVariableTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName,
    TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations,
    UNativizationV2Subsystem* Subsystem)
{
    UEdGraphPin* Target = nullptr;
    UEdGraphPin* Input = nullptr;
    if (UK2Node_AssignmentStatement* Assignment = Cast<UK2Node_AssignmentStatement>(Node))
    {
        Target = Assignment->GetVariablePin();
        Input = Assignment->GetValuePin();
    }
    else if (UK2Node_VariableSetRef* VariableAssignment = Cast<UK2Node_VariableSetRef>(Node))
    {
        Target = VariableAssignment->GetTargetPin();
        Input = VariableAssignment->GetValuePin();
    }
    else
    {
        return FString();
    }
    const FGenerateResultStruct Variable = Subsystem->GenerateInputParameterCodeForNode(Node, Target, 0, MacroStack);
    const FGenerateResultStruct Value = Subsystem->GenerateInputParameterCodeForNode(Node, Input, 0, MacroStack);
    FGenerationPreparations RequiredPreparations = Variable.Preparations;
    RequiredPreparations.Append(Value.Preparations);
    FString Content = GenerateNewPreparations(Preparations, RequiredPreparations);
    Preparations.Append(RequiredPreparations);
    FString Destination = Variable.Code;
    TArray<UK2Node*> TargetStack = MacroStack;
    UEdGraphPin* Source = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Target, 0, false, TargetStack, Subsystem);
    if (Source)
    {
        UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Source->GetOwningNode());
        if (Call && Call->IsNodePure() && Source == Call->GetReturnValuePin() && !Source->PinType.bIsReference)
        {
            Destination = Subsystem->GetNodeStateName(Node, TEXT("AssignmentTarget"), true);
            Content += FString::Format(TEXT("auto {0} = {1};\n"), {Destination, Variable.Code});
        }
    }
    Content += FString::Format(TEXT("{0} = {1};\n"), { Destination, Value.Code });
    return Content;
}
