#pragma once

#include "CoreMinimal.h"

struct FRepeatedCallSite
{
    FString Marker;
    FString Signature;
    FString Name;
    FString Definition;
    FString ExpandedCode;
    FString Invocation;
};
