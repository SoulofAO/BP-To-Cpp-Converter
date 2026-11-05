/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright 2026 AO All Rights Reserved.
 */

#pragma once
#include "CoreMinimal.h"
class UBlueprint;
class UEdGraph;
class UK2Node;
class UNativizationV2Subsystem;

class FGenerationGraphContext
{
public:
    FGenerationGraphContext(UBlueprint* SourceBlueprint, UNativizationV2Subsystem* Generator);
    ~FGenerationGraphContext();
    bool IsValid() const { return true; }
    static UNativizationV2Subsystem* GetGenerator();
    static UBlueprint* GetBlueprint();
    static void GetGraphs(UBlueprint* Source, TArray<UEdGraph*>& Result);
    static UEdGraph* GetMacroGraph(UK2Node* Node);
    static TArray<UK2Node*> GetMacroStack(UK2Node* Node);
    static bool IsFunctionGraph(UK2Node* Node);
private:
    static FGenerationGraphContext* Active;
    UBlueprint* Blueprint;
    UNativizationV2Subsystem* Subsystem;
    FGenerationGraphContext* Previous;
};
