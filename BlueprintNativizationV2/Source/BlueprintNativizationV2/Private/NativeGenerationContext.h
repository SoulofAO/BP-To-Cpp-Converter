/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright 2026 AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"

struct FStructTranslationResult;

struct FNativeGenerationRequirements
{
    TSet<FString> Headers;
    TSet<FString> Implementations;
    TSet<FString> Declarations;
    TSet<FString> Modules;
};

class BLUEPRINTNATIVIZATIONV2_API FNativeGenerationContext
{
public:
    static bool bGeneratingInlineDefaults;
    static bool bGeneratingClassFilter;
    static void BeginGeneration();
    static void AddClassReplacement(UClass* OriginalClass, UClass* ParentClass);
    static UClass* ResolveClass(UClass* Class);
    static const TMap<UClass*, UClass*>& GetClassReplacements();
    static void ClearClassReplacements();
    static void BeginObject();
    static void AddRequirements(const FStructTranslationResult& Result);
    static void CompleteHeader(FString& Header);
    static void AddImplementationInclude(const FString& Include);
    static void CompleteImplementation(FString& Implementation);
    static const TSet<FString>& GetModuleDependencies();
    static FNativeGenerationRequirements CaptureRequirements();
    static void RestoreRequirements(const FNativeGenerationRequirements& Requirements);
};
