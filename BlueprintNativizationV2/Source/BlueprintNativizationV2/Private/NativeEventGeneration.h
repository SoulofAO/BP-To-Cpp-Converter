/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "Dom/JsonObject.h"
#include "CoreMinimal.h"
#include "BlueprintNativizationData.h"

class UBlueprint;

struct FNativeEventGeneration
{
    static TSharedPtr<FJsonObject> FindImplementation(const UFunction* Function);
    static bool IsImplementationConst(const UFunction* Function, const UClass* GeneratedClass);
    static FString GenerateConstructor(UBlueprint* Blueprint, const TArray<FGenerateFunctionStruct>& Entries);
    static void Complete(UBlueprint* Blueprint, const TArray<FGenerateFunctionStruct>& Entries, FString& Header, FString& Source);
};
