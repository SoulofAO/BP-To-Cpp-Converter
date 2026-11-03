/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#pragma once
#include "CoreMinimal.h"

class BLUEPRINTSOURCEANALYSIS_API FNativeCacheTask
{
public:
    static bool Start(bool bEngine, FString& Report);
    static FString GetStatus();
    static bool IsRunning();
    static void GetProgress(FString& Label, TOptional<float>& Fraction);
    static void Cancel();
};
