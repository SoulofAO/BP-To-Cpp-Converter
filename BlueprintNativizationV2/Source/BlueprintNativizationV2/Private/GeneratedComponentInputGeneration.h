#pragma once

#include "CoreMinimal.h"
#include "BlueprintNativizationData.h"

class FGeneratedComponentInputGeneration
{
public:
    static FNativizationCode Generate(const FString& ModuleName);
};
