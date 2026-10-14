#include "GenerationGraphContext.h"
#include "NativeApiCatalog.h"
#include "Algo/Reverse.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "TranslatorObjects/Latens/DelayTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationUniqueNameSubsystem.h"
#include "Engine/Engine.h"
#include "EdGraphUtilities.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Composite.h"
#include "K2Node_TemporaryVariable.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_Knot.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "KismetCompiler.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/TimelineTemplate.h"

class FNodeExpansionContext : public FKismetCompilerContext
{
public:
    FNodeExpansionContext(UBlueprint* Blueprint, UEdGraph* Graph, FCompilerResultsLog& Results)
        : FKismetCompilerContext(Blueprint, Results, FKismetCompilerOptions())
    {
        Schema = GetMutableDefault<UEdGraphSchema_K2>();
        NewClass = CastChecked<UBlueprintGeneratedClass>(Blueprint->GeneratedClass);
        for (UTimelineTemplate* Timeline : Blueprint->Timelines)
        {
            TimelineToMemberVariableMap.Add(Timeline, FindFProperty<FProperty>(NewClass, Timeline->GetVariableName()));
        }
        ConsolidatedEventGraph = Graph;
        bIsFullCompile = -1;
    }
    using FGraphCompilerContext::CreateExecutionSchedule;
    void PruneUnusedNodes(UEdGraph* Graph)
    {
        PruneIsolatedNodes(Graph, true);
    }
};

FGenerationGraphContext* FGenerationGraphContext::Active = nullptr;

FGenerationGraphContext::FGenerationGraphContext(UBlueprint* SourceBlueprint, UNativizationV2Subsystem* Generator)
    : Blueprint(SourceBlueprint), Subsystem(Generator), Previous(Active),
      GraphOwner(NewObject<UEdGraph>(SourceBlueprint, NAME_None, RF_Transient))
{
    Active = this;
    Subsystem->ExpandedNodeNames.Reset();
    Subsystem->PureExecutionOrder.Reset();
    UEdGraph* EventScheduleGraph = NewObject<UEdGraph>(GraphOwner.Get(), NAME_None, RF_Transient);
    EventScheduleGraph->Schema = UEdGraphSchema_K2::StaticClass();
    TArray<UEdGraph*> Sources;
    Sources.Append(Blueprint->UbergraphPages);
    Sources.Append(Blueprint->FunctionGraphs);
    for (UEdGraph* Source : Sources)
    {
        UEdGraph* Graph = FEdGraphUtilities::CloneGraph(Source, GraphOwner.Get(), nullptr, true);
        RootGraphs.Add(Graph);
        Graphs.Add(Graph);
        if (Blueprint->FunctionGraphs.Contains(Source))
        {
            FunctionGraphs.Add(Graph);
        }
        auto* Names = GEngine->GetEngineSubsystem<UBlueprintNativizationUniqueNameSubsystem>();
        for (UEdGraphNode* OriginalNode : Source->Nodes)
        {
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (Node->NodeGuid == OriginalNode->NodeGuid && OriginalNode->IsA<UK2Node>())
                {
                    Names->RegisterFunctionAlias(CastChecked<UK2Node>(OriginalNode), CastChecked<UK2Node>(Node));
                    break;
                }
            }
        }
        if (!FunctionGraphs.Contains(Graph))
        {
            // UE merges event pages by popping their nodes in reverse order.
            Algo::Reverse(Graph->Nodes);
        }
        if (!FunctionGraphs.Contains(Graph))
        {
            EventScheduleGraph->Nodes.Append(Graph->Nodes);
        }
    }
    if (!EventScheduleGraph->Nodes.IsEmpty())
    {
        UK2Node_FunctionEntry* Entry = NewObject<UK2Node_FunctionEntry>(EventScheduleGraph, NAME_None, RF_Transient);
        Entry->FunctionReference.SetExternalMember(UEdGraphSchema_K2::FN_ExecuteUbergraphBase, UObject::StaticClass());
        Entry->AllocateDefaultPins();
        EventScheduleGraph->Nodes.Add(Entry);
        FCompilerResultsLog Results;
        FNodeExpansionContext Context(Blueprint, EventScheduleGraph, Results);
        Context.PruneUnusedNodes(EventScheduleGraph);
        bValid = Results.NumErrors == 0 && bValid;
    }
    for (UEdGraph* Graph : RootGraphs)
    {
        if (FunctionGraphs.Contains(Graph))
        {
            FCompilerResultsLog Results;
            FNodeExpansionContext Context(Blueprint, Graph, Results);
            Context.PruneUnusedNodes(Graph);
            bValid = Results.NumErrors == 0 && bValid;
        }
        else
        {
            Graph->Nodes.RemoveAll([EventScheduleGraph](const UEdGraphNode* Node)
            {
                return !EventScheduleGraph->Nodes.Contains(Node);
            });
        }
        bValid = PrepareMacros(Graph, {}, {}, FString()) && bValid;
    }
    for (UEdGraph* Graph : Graphs)
    {
        bValid = ExpandNodes(Graph) && bValid;
    }
    for (UEdGraph* Graph : RootGraphs)
    {
        if (!FunctionGraphs.Contains(Graph))
        {
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                EventScheduleGraph->Nodes.AddUnique(Node);
            }
        }
    }
    {
        FCompilerResultsLog Results;
        FNodeExpansionContext Context(Blueprint, EventScheduleGraph, Results);
        Context.PruneUnusedNodes(EventScheduleGraph);
        bValid = Results.NumErrors == 0 && bValid;
    }
    for (UEdGraph* Graph : RootGraphs)
    {
        if (FunctionGraphs.Contains(Graph))
        {
            FCompilerResultsLog Results;
            FNodeExpansionContext Context(Blueprint, Graph, Results);
            Context.PruneUnusedNodes(Graph);
            bValid = Results.NumErrors == 0 && bValid;
        }
        else
        {
            Graph->Nodes.RemoveAll([EventScheduleGraph](const UEdGraphNode* Node)
            {
                return !EventScheduleGraph->Nodes.Contains(Node);
            });
        }
    }
    TSet<UK2Node*> ActiveDependencies;
    TSet<UK2Node*> CheckedDependencies;
    TFunction<bool(UK2Node*)> ValidateDependencies = [&](UK2Node* Node)
    {
        if (CheckedDependencies.Contains(Node))
        {
            return true;
        }
        ActiveDependencies.Add(Node);
        for (UEdGraphPin* Input : Node->Pins)
        {
            if (Input->Direction != EGPD_Input || Input->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
            {
                continue;
            }
            TArray<UK2Node*> MacroStack = GetMacroStack(Node);
            UEdGraphPin* Source = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Input, 0, false, MacroStack, Subsystem);
            UK2Node* Dependency = nullptr;
            if (Source)
            {
                Dependency = Cast<UK2Node>(Source->GetOwningNode());
            }
            if (!Dependency || !Dependency->IsNodePure())
            {
                continue;
            }
            if (ActiveDependencies.Contains(Dependency))
            {
                const FString Diagnostic = FString::Format(TEXT("Pure data dependency cycle in asset {0}, graph {1}: node {2}, input {3}, depends on active node {4}, output {5}."),
                    {Blueprint->GetPathName(), Node->GetGraph()->GetName(), Node->GetName(), Input->PinName.ToString(), Dependency->GetName(), Source->PinName.ToString()});
                FNativeApiCatalog::AddError(Diagnostic);
                UE_LOG(LogTemp, Error, TEXT("%s"), *Diagnostic);
                return false;
            }
            if (!ValidateDependencies(Dependency))
            {
                return false;
            }
        }
        ActiveDependencies.Remove(Node);
        CheckedDependencies.Add(Node);
        return true;
    };
    for (UEdGraph* Graph : Graphs)
    {
        for (UEdGraphNode* GraphNode : Graph->Nodes)
        {
            if (UK2Node* Node = Cast<UK2Node>(GraphNode))
            {
                if (!ValidateDependencies(Node))
                {
                    bValid = false;
                    return;
                }
            }
        }
    }
    for (UEdGraph* Graph : Graphs)
    {
        FCompilerResultsLog Results;
        FNodeExpansionContext Context(Blueprint, Graph, Results);
        TArray<UEdGraphNode*> Schedule;
        Context.CreateExecutionSchedule(Graph->Nodes, Schedule);
        for (int32 NodeIndex = 0; NodeIndex < Schedule.Num(); ++NodeIndex)
        {
            Subsystem->PureExecutionOrder.Add(Schedule[NodeIndex], NodeIndex);
        }
        bValid = Results.NumErrors == 0 && bValid;
    }
    TArray<UEdGraphNode*> EventNodes(EventScheduleGraph->Nodes);
    for (UEdGraph* Graph : RootGraphs)
    {
        if (!FunctionGraphs.Contains(Graph))
        {
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                EventNodes.AddUnique(Node);
            }
        }
    }
    if (!EventNodes.IsEmpty())
    {
        FCompilerResultsLog Results;
        FNodeExpansionContext Context(Blueprint, EventScheduleGraph, Results);
        TArray<UEdGraphNode*> Schedule;
        Context.CreateExecutionSchedule(EventNodes, Schedule);
        for (int32 NodeIndex = 0; NodeIndex < Schedule.Num(); ++NodeIndex)
        {
            Subsystem->PureExecutionOrder.Add(Schedule[NodeIndex], NodeIndex);
        }
        bValid = Results.NumErrors == 0 && bValid;
    }
}

FGenerationGraphContext::~FGenerationGraphContext()
{
    Subsystem->PureExecutionOrder.Reset();
    Subsystem->ExpandedNodeNames.Reset();
    Active = Previous;
}

bool FGenerationGraphContext::PrepareMacros(UEdGraph* Graph, TArray<UEdGraph*> Definitions,
    TArray<UK2Node*> MacroStack, const FString& Prefix)
{
    GraphStacks.Add(Graph, MacroStack);
    for (UEdGraphNode* GraphNode : Graph->Nodes)
    {
        UK2Node* Node = Cast<UK2Node>(GraphNode);
        if (!Node)
        {
            continue;
        }
        if (!Prefix.IsEmpty())
        {
            Subsystem->ExpandedNodeNames.Add(Node, Prefix);
        }
        UK2Node_MacroInstance* Macro = Cast<UK2Node_MacroInstance>(Node);
        UK2Node_Composite* Composite = Cast<UK2Node_Composite>(Node);
        UEdGraph* Definition = nullptr;
        if (Macro && !Subsystem->FindTranslatorForNode(Macro))
        {
            Definition = Macro->GetMacroGraph();
        }
        else if (Composite)
        {
            Definition = Composite->BoundGraph;
        }
        else
        {
            continue;
        }
        if (!Definition || Definitions.Contains(Definition))
        {
            FNativeApiCatalog::AddError(TEXT("A macro definition is missing or recursively references itself."));
            return false;
        }
        UEdGraph* InstanceGraph = FEdGraphUtilities::CloneGraph(Definition, GraphOwner.Get(), nullptr, true);
        MacroGraphs.Add(Node, InstanceGraph);
        Graphs.Add(InstanceGraph);
        if (FunctionGraphs.Contains(Graph))
        {
            FunctionGraphs.Add(InstanceGraph);
        }
        if (Macro)
        {
            TArray<UEdGraphNode*> InstanceNodes(InstanceGraph->Nodes);
            Macro->InferWildcards(InstanceNodes);
        }
        TArray<UEdGraph*> NestedDefinitions = Definitions;
        NestedDefinitions.Add(Definition);
        TArray<UK2Node*> NestedStack = MacroStack;
        NestedStack.Add(Node);
        const FString InstanceName = Prefix + Definition->GetName().Replace(TEXT(" "), TEXT(""));
        if (!PrepareMacros(InstanceGraph, NestedDefinitions, NestedStack, InstanceName))
        {
            return false;
        }
    }
    return true;
}

UNativizationV2Subsystem* FGenerationGraphContext::GetGenerator()
{
    if (Active)
    {
        return Active->Subsystem;
    }
    return nullptr;
}

void FGenerationGraphContext::GetGraphs(UBlueprint* Source, TArray<UEdGraph*>& Result)
{
    if (Active && Active->Blueprint == Source)
    {
        Result.Append(Active->RootGraphs);
        return;
    }
    Source->GetAllGraphs(Result);
}

UEdGraph* FGenerationGraphContext::GetMacroGraph(UK2Node* Node)
{
    if (Active)
    {
        if (UEdGraph** Graph = Active->MacroGraphs.Find(Node))
        {
            return *Graph;
        }
    }
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
    if (Active)
    {
        return Active->GraphStacks.FindRef(Node->GetGraph());
    }
    return {};
}

bool FGenerationGraphContext::IsFunctionGraph(UK2Node* Node)
{
    if (Active && Active->Blueprint == Node->GetBlueprint())
    {
        return Active->FunctionGraphs.Contains(Node->GetGraph());
    }
    return Node->GetBlueprint()->FunctionGraphs.Contains(Node->GetGraph());
}

bool FGenerationGraphContext::ExpandNodes(UEdGraph* Graph)
{
    FCompilerResultsLog Results;
    FNodeExpansionContext Context(Blueprint, Graph, Results);

    TSet<UK2Node*> Processed;
    for (UEdGraphNode* GraphNode : Graph->Nodes)
    {
        if (UK2Node_Knot* Knot = Cast<UK2Node_Knot>(GraphNode))
        {
            Knot->ExpandNode(Context, Graph);
        }
    }
    TArray<TObjectPtr<UEdGraphNode>> Pending = Graph->Nodes;
    for (int32 NodeIndex = 0; NodeIndex < Pending.Num(); ++NodeIndex)
    {
        UK2Node* Node = Cast<UK2Node>(Pending[NodeIndex]);
        if (!Node || Processed.Contains(Node))
        {
            continue;
        }
        Processed.Add(Node);
        if (Node->IsA<UK2Node_MacroInstance>() || Node->IsA<UK2Node_Composite>() || Node->IsA<UK2Node_Tunnel>())
        {
            continue;
        }
        const bool bConnected = Node->Pins.ContainsByPredicate([](const UEdGraphPin* Pin)
        {
            return !Pin->LinkedTo.IsEmpty();
        });
        if (!bConnected)
        {
            continue;
        }
        if (Node->IsNodePure() && !Node->Pins.ContainsByPredicate([](const UEdGraphPin* Pin)
        {
            return Pin->Direction == EGPD_Output && !Pin->LinkedTo.IsEmpty();
        }))
        {
            Node->BreakAllNodeLinks();
            continue;
        }
        if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
        {
            UFunction* Function = Call->GetTargetFunction();
            if (Function && Function->HasMetaData(TEXT("Latent")) &&
                !UDelayTranslatorObject::SupportsNativeOperation(Function))
            {
                Results.Error(TEXT("No native asynchronous implementation is available for latent call @@."), Node);
                continue;
            }
        }
        if (UK2Node_Variable* Variable = Cast<UK2Node_Variable>(Node))
        {
            if ((Variable->IsA<UK2Node_VariableGet>() || Variable->IsA<UK2Node_VariableSet>()) && !Variable->GetPropertyForVariable())
            {
                FNativeApiCatalog::AddError(FString::Format(TEXT("Cannot resolve variable {0} in {1}, graph {2}, scope {3}, node {4}."),
                    {Variable->GetVarName().ToString(), Blueprint->GetPathName(), Graph->GetName(),
                    Variable->VariableReference.GetMemberScopeName(), Variable->GetName()}));
                Results.Error(TEXT("Cannot resolve the property used by @@."), Node);
                continue;
            }
        }
        UTranslatorBPToCppObject* Translator = Subsystem->FindTranslatorForNode(Node);
        if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
        {
            if (!Call->GetTargetFunction() && Translator && !Translator->ShouldExpandNode(Node))
            {
                FNativeApiCatalog::AddError(FString::Format(TEXT("Cannot resolve function {0} in {1}, node {2}."),
                    {Call->FunctionReference.GetMemberName().ToString(), Blueprint->GetPathName(), Call->GetName()}));
                Results.Error(TEXT("Cannot resolve the function called by @@."), Node);
                continue;
            }
        }
        const bool bExpandNode = (!Translator || Translator->ShouldExpandNode(Node)) && !Node->IsA<UK2Node_Event>() &&
            !Node->IsA<UK2Node_FunctionEntry>() && !Node->IsA<UK2Node_Knot>();
        const TArray<TObjectPtr<UEdGraphNode>> PreviousNodes = Graph->Nodes;
        const TArray<UEdGraphPin*> SplitPins = Node->Pins.FilterByPredicate([](const UEdGraphPin* Pin)
        {
            return !Pin->SubPins.IsEmpty();
        });
        Node->ExpandSplitPins(Context, Graph);
        for (UEdGraphPin* SplitPin : SplitPins)
        {
            if (Node->Pins.Contains(SplitPin))
            {
                SplitPin->bHidden = false;
            }
        }
        if (bExpandNode)
        {
            Node->ExpandNode(Context, Graph);
        }
        for (UEdGraphNode* ExpandedNode : Graph->Nodes)
        {
            if (!PreviousNodes.Contains(ExpandedNode))
            {
                Pending.Add(ExpandedNode);
                FGuid SourceGuid = Node->NodeGuid;
                if (const FGuid* ExpandedSourceGuid = ExpandedNodeSourceGuids.Find(Node))
                {
                    SourceGuid = *ExpandedSourceGuid;
                }
                ExpandedNodeSourceGuids.Add(ExpandedNode, SourceGuid);
                if (UK2Node* Expanded = Cast<UK2Node>(ExpandedNode))
                {
                    FString Prefix = Subsystem->ExpandedNodeNames.FindRef(Node);
                    Prefix += Node->GetClass()->GetName().RightChop(7);
                    Subsystem->ExpandedNodeNames.Add(Expanded, Prefix);
                }
            }
        }
        if (!bExpandNode)
        {
            continue;
        }
        if (Node->IsNodePure() && !Node->Pins.ContainsByPredicate([](const UEdGraphPin* Pin)
        {
            return Pin->Direction == EGPD_Output && !Pin->LinkedTo.IsEmpty();
        }))
        {
            Node->BreakAllNodeLinks();
        }
        const bool bStillConnected = Node->Pins.ContainsByPredicate([](const UEdGraphPin* Pin)
        {
            return !Pin->LinkedTo.IsEmpty();
        });
        if (bStillConnected)
        {
            Results.Error(TEXT("No translation or compiler expansion is available for @@."), Node);
        }
    }
    return Results.NumErrors == 0;
}

