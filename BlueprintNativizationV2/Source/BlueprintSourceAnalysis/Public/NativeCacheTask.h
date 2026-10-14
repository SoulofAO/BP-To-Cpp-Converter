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
