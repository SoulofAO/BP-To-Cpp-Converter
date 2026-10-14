#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct FNativeApiSourcePatch
{
    FString Filename;
    TArray<uint8> Original;
    TArray<uint8> Updated;
    TArray<TPair<int32, FString>> Insertions;
    FString Module;
};

struct FNativeApiReview
{
    FString Identifier;
    FString Summary;
    FString OutputModule;
    TArray<FNativeApiSourcePatch> SourcePatches;
    bool bReady = true;
};

class BLUEPRINTNATIVIZATIONV2_API FNativeApiPreflight
{
public:
    static FNativeApiReview Analyze(const TArray<UObject*>& Targets, bool bFunctionFragment, const FString& OutputModule,
        bool bIncludeProjectClients = false);
    static void AnalyzeDefinition(const TSharedPtr<FJsonObject>& Definition, const FString& Context, FNativeApiReview& Review);
    static bool Apply(const FNativeApiReview& Review, FString& Report);
    static bool RequiresRebuild(FString& Reason);
    static bool UsesObjectInitializer(UClass* Base);
};
