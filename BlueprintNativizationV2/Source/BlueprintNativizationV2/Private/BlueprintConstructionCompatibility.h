/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class USceneComponent;
class UEdGraphPin;

struct FConstructionAttachmentConflict
{
    UBlueprint* Blueprint = nullptr;
    USceneComponent* Component = nullptr;
    USceneComponent* Parent = nullptr;
    UEdGraphPin* MobilityInput = nullptr;
    FString Description;
};

class BLUEPRINTNATIVIZATIONV2_API FBlueprintConstructionCompatibility
{
public:
    static TArray<FConstructionAttachmentConflict> FindConflicts(UBlueprint* Blueprint);
};
