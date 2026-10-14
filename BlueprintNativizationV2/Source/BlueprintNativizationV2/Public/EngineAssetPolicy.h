#pragma once

#include "CoreMinimal.h"

struct FAssetData;

struct BLUEPRINTNATIVIZATIONV2_API FEngineAssetPolicy
{
    static bool IsEnginePackage(const FString& PackageName);
    static FString GetExclusionReason(const UObject* Object, bool bExcludeEngineAssets);
    static FString GetExclusionReason(const FAssetData& Asset, bool bExcludeEngineAssets);
};
