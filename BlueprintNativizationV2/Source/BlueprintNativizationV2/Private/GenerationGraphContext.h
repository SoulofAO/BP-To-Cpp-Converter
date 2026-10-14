#pragma once
#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UK2Node;
class UNativizationV2Subsystem;

class FGenerationGraphContext
{
public:
    FGenerationGraphContext(UBlueprint* SourceBlueprint, UNativizationV2Subsystem* Generator);
    ~FGenerationGraphContext();
    bool IsValid() const { return bValid; }
    static UNativizationV2Subsystem* GetGenerator();
    static void GetGraphs(UBlueprint* Source, TArray<UEdGraph*>& Result);
    static UEdGraph* GetMacroGraph(UK2Node* Node);
    static TArray<UK2Node*> GetMacroStack(UK2Node* Node);
    static bool IsFunctionGraph(UK2Node* Node);
private:
    bool PrepareMacros(UEdGraph* Graph, TArray<UEdGraph*> Definitions, TArray<UK2Node*> MacroStack, const FString& Prefix);
    bool ExpandNodes(UEdGraph* Graph);
    static FGenerationGraphContext* Active;
    UBlueprint* Blueprint;
    UNativizationV2Subsystem* Subsystem;
    FGenerationGraphContext* Previous;
    TStrongObjectPtr<UEdGraph> GraphOwner;
    TArray<UEdGraph*> RootGraphs;
    TArray<UEdGraph*> Graphs;
    TSet<UEdGraph*> FunctionGraphs;
    TMap<UK2Node*, UEdGraph*> MacroGraphs;
    TMap<UEdGraph*, TArray<UK2Node*>> GraphStacks;
    TMap<UEdGraphNode*, FGuid> ExpandedNodeSourceGuids;
    bool bValid = true;
};
