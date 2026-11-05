/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright 2026 AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"

struct FAssetData;

struct BLUEPRINTNATIVIZATIONV2_API FEngineAssetPolicy
{
    static bool IsEnginePackage(const FString& PackageName);
    static FString GetExclusionReason(const UObject* Object, bool bExcludeEngineAssets);
    static FString GetExclusionReason(const FAssetData& Asset, bool bExcludeEngineAssets);
};
