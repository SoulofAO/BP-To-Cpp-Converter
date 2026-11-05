/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright 2026 AO All Rights Reserved.
 */

#pragma once

#include "BlueprintNativizationSettings.h"
#include "GameFramework/Actor.h"

class FConstructionScriptTranslation
{
public:
    static bool TranslateNativeEvent(UClass* OwnerClass, const FString& EventName, FFunctionDescriptor& Result)
    {
        if (!OwnerClass->IsChildOf(AActor::StaticClass()) || EventName != TEXT("UserConstructionScript"))
        {
            return false;
        }
        // Actor construction dispatches the Blueprint script and native hook separately.
        Result = {};
        Result.ClassName = TEXT("AActor");
        Result.FunctionName = TEXT("OnConstruction");
        Result.ParameterSignature = {TEXT("const FTransform& Transform")};
        return true;
    }
};
