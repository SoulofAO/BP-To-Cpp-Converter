/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright 2026 AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
class UK2Node_CallFunction;

class BLUEPRINTNATIVIZATIONV2_API FNativeFunctionExpressions
{
public:
    static bool IsClassFilterParameter(UFunction* Function, FName ParameterName);
    static FString Arithmetic(UFunction* Function, const TArray<FString>& Arguments, const TArray<FString>& ArgumentTypes = {});
    static bool Projection(UFunction* Function, FString& Input, TArray<TPair<FString, FString>>& Outputs);
    static FString BehaviorTree(UFunction* Function, const FString& Arguments, UK2Node_CallFunction* Call);
};
