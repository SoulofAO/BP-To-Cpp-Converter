/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright 2026 AO All Rights Reserved.
 */

#include "GenerationGraphContext.h"
#include "BlueprintNativizationSubsystem.h"
#include "Engine/Blueprint.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Composite.h"

FGenerationGraphContext* FGenerationGraphContext::Active = nullptr;

FGenerationGraphContext::FGenerationGraphContext(UBlueprint* SourceBlueprint, UNativizationV2Subsystem* Generator)
    : Blueprint(SourceBlueprint), Subsystem(Generator), Previous(Active)
{
    Active = this;
    Subsystem->InputValues.Reset();
    Subsystem->PureFunctionCalls.Reset();
    Subsystem->ActiveInputValues.Reset();
    Subsystem->CurrentMacroStack.Reset();
    Subsystem->EvaluationSequence = 0;
    Subsystem->CurrentEvaluation = 0;
}

FGenerationGraphContext::~FGenerationGraphContext()
{
    Active = Previous;
}

UNativizationV2Subsystem* FGenerationGraphContext::GetGenerator()
{
    if (Active)
    {
        return Active->Subsystem;
    }
    return nullptr;
}

UBlueprint* FGenerationGraphContext::GetBlueprint()
{
    if (Active)
    {
        return Active->Blueprint;
    }
    return nullptr;
}

void FGenerationGraphContext::GetGraphs(UBlueprint* Source, TArray<UEdGraph*>& Result)
{
    if (!Active || Active->Blueprint != Source)
    {
        Source->GetAllGraphs(Result);
        return;
    }
    Result.Append(Source->UbergraphPages);
    Result.Append(Source->FunctionGraphs);
    for (const FBPInterfaceDescription& Interface : Source->ImplementedInterfaces)
    {
        for (UEdGraph* Graph : Interface.Graphs)
        {
            Result.AddUnique(Graph);
        }
    }
}

UEdGraph* FGenerationGraphContext::GetMacroGraph(UK2Node* Node)
{
    if (UK2Node_MacroInstance* Macro = Cast<UK2Node_MacroInstance>(Node))
    {
        return Macro->GetMacroGraph();
    }
    if (UK2Node_Composite* Composite = Cast<UK2Node_Composite>(Node))
    {
        return Composite->BoundGraph;
    }
    return nullptr;
}

TArray<UK2Node*> FGenerationGraphContext::GetMacroStack(UK2Node* Node)
{
    TArray<UK2Node*> Result;
    if (Active)
    {
        for (UK2Node* Macro : Active->Subsystem->CurrentMacroStack)
        {
            if (Macro->GetGraph() == Node->GetGraph())
            {
                return Result;
            }
            Result.Add(Macro);
            if (GetMacroGraph(Macro) == Node->GetGraph())
            {
                return Result;
            }
        }
    }
    return {};
}

bool FGenerationGraphContext::IsFunctionGraph(UK2Node* Node)
{
    const TArray<UK2Node*> Stack = GetMacroStack(Node);
    if (!Stack.IsEmpty())
    {
        Node = Stack[0];
    }
    UBlueprint* Owner = Node->GetBlueprint();
    if (Owner->FunctionGraphs.Contains(Node->GetGraph()))
    {
        return true;
    }
    for (const FBPInterfaceDescription& Interface : Owner->ImplementedInterfaces)
    {
        if (Interface.Graphs.Contains(Node->GetGraph()))
        {
            return true;
        }
    }
    return false;
}
