/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright 2026 AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BlueprintNativeApiCacheLibrary.generated.h"

class FJsonObject;

UCLASS()
class BLUEPRINTSOURCEANALYSIS_API UBlueprintNativeApiCacheLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Blueprint to C++|Source Cache")
    static bool BuildSourceCache(const TArray<FString>& SourceFiles, const FString& OutputFile, FString& Report);

    UFUNCTION(BlueprintCallable, Category = "Blueprint to C++|Source Cache")
    static bool ValidateSourceCache(const FString& CacheFile, FString& Report);

    static bool ReadValidatedSourceCache(const FString& CacheFile, TSharedPtr<FJsonObject>& Cache, FString& Report);

    static TArray<FString> GetSourceArguments();

    static void BuildProjectCache();
    static void BuildEditorCache();
};
