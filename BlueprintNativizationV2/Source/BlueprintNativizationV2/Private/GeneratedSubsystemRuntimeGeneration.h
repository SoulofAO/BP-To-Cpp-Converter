#pragma once

#include "CoreMinimal.h"
#include "BlueprintNativizationData.h"

class FGeneratedSubsystemRuntimeGeneration
{
public:
    static FNativizationCode Generate(const FString& ModuleName);
};
