/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */


#pragma once
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Logging/LogMacros.h"
#include <regex>
#include "TestParseCppCodeLibrary.generated.h"


UCLASS(Blueprintable)
class UEFunctionRedirectMapper : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category = "Function Mapping")
    static TArray<FFunctionBinding> FindBindings(
        const FString& SourceRoot,
        int32 MaxIfs,
        const TArray<FString>& Extensions);

    UFUNCTION(BlueprintCallable, Category = "Function Mapping")
    static TArray<FFunctionBinding> FindEngineBindings(
        int32 MaxIfs = 1);
};
