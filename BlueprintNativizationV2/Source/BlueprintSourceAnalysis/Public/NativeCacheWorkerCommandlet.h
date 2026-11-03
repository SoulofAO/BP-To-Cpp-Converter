/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#pragma once
#include "Commandlets/Commandlet.h"
#include "NativeCacheWorkerCommandlet.generated.h"

UCLASS()
class UNativeCacheWorkerCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UNativeCacheWorkerCommandlet();
    virtual int32 Main(const FString& Parameters) override;
};
