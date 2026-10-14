#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class USCS_Node;

struct FBlueprintComponentRepair
{
    TArray<USCS_Node*> BrokenNodes;
    TArray<FName> MissingPackages;
    TArray<FString> Diagnostics;

    bool HasRepairs() const;
    static FBlueprintComponentRepair Inspect(UBlueprint* Blueprint);
    void TryRemoveBrokenNodes(UBlueprint* Blueprint, TArray<FString>& Messages) const;
};
