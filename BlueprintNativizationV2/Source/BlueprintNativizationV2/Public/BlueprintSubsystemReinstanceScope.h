/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"

class BLUEPRINTNATIVIZATIONV2_API FBlueprintSubsystemReinstanceScope
{
public:
    FBlueprintSubsystemReinstanceScope();
    ~FBlueprintSubsystemReinstanceScope();

    FBlueprintSubsystemReinstanceScope(const FBlueprintSubsystemReinstanceScope&) = delete;
    FBlueprintSubsystemReinstanceScope& operator=(const FBlueprintSubsystemReinstanceScope&) = delete;

private:
    FDelegateHandle ReinstanceRegistration;
};
