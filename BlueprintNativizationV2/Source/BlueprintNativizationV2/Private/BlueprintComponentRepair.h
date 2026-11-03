#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class USCS_Node;
class AActor;
class UActorComponent;

struct FBlueprintComponentRepair
{
    TArray<USCS_Node*> BrokenNodes;
    TArray<FName> MissingPackages;
    TArray<FString> Diagnostics;

    bool HasRepairs() const;
    static FBlueprintComponentRepair Inspect(UBlueprint* Blueprint);
    static TArray<UActorComponent*> FindUnmatchedConstructionComponents(AActor* Actor);
    void TryRemoveBrokenNodes(UBlueprint* Blueprint, TArray<FString>& Messages) const;
};
