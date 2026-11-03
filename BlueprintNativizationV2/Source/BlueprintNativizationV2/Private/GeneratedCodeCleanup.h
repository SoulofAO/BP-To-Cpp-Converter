#pragma once

#include "CoreMinimal.h"

struct BLUEPRINTNATIVIZATIONV2_API FGeneratedCodeCleanup
{
    static FString RemoveUnusedScalarDeclarations(const FString& Declarations, const FString& Body);
    static FString RemoveUnusedForwardDeclarations(const FString& Header);
    static FString RemoveTrailingVoidReturn(const FString& Body);
    static FString CollapseImmediateScalarInitialization(const FString& Body);
    static FString SimplifyImmediateInvocations(const FString& Content);
    static FString RenameGeneratedParameters(const FString& Content, const TMap<FString, FString>& Names);
    static void RemoveRedundantOverrides(FString& Header, FString& Source);
    static FString RewriteNativeEventBody(const FString& Body, const TMap<FString, FString>& Implementations, int32& ReplacedCalls, const FString& GeneratedClass = FString(), const FString& OriginalParentClass = FString());
};
