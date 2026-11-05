/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright 2026 AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class BLUEPRINTSOURCEANALYSIS_API FNativeApiCatalog
{
public:
    static void BeginGeneration(bool bAllowSourceFallback = false);
    static TSharedPtr<FJsonObject> FindType(UStruct* Structure);
    static TSet<FString> GetValueTypeModules(UStruct* Structure);
    static TSharedPtr<FJsonObject> ResolveCallable(UFunction* Function);
    static bool CanCallFunctionDirectly(UFunction* Function, UClass* CallingClass = nullptr);
    static TSharedPtr<FJsonObject> FindPublicDeclaration(const FString& Name);
    static TSharedPtr<FJsonObject> FindCallable(UFunction* Function);
    static bool FindPublicForwarding(UFunction* Function, FString& Expression, TSet<FString>& Includes, TSet<FString>& Modules);
    static bool FindEventOverride(UClass* Class, UFunction* Event, UClass*& MethodOwner,
        TSharedPtr<FJsonObject>& Method, FString& Body, FString& Diagnostic, TSet<FString>& Includes, TSet<FString>& Modules);
    static void AddError(const FString& Error);
    static const TArray<FString>& GetErrors();
    static void DiscardErrorsAfter(int32 ErrorCount);
};
