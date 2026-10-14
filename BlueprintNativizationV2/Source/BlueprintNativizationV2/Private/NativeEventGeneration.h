#pragma once

#include "CoreMinimal.h"
#include "BlueprintNativizationData.h"

class UBlueprint;

struct FNativeEventGeneration
{
    static FString GenerateConstructor(UBlueprint* Blueprint, const TArray<FGenerateFunctionStruct>& Entries);
    static void Complete(UBlueprint* Blueprint, const TArray<FGenerateFunctionStruct>& Entries, FString& Header, FString& Source);
};
