/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright 2026 AO All Rights Reserved.
 */

#include "NativeGenerationContext.h"
#include "NativeApiCatalog.h"
#include "TranslatorObjects/StructTranslatorObject.h"
#include "Engine/Blueprint.h"

static TSet<FString> StructureIncludes;
static TSet<FString> ImplementationIncludes;
static TSet<FString> StructureDeclarations;
static TSet<FString> StructureModules;
static TMap<UClass*, UClass*> ClassReplacements;
bool FNativeGenerationContext::bGeneratingInlineDefaults = false;
bool FNativeGenerationContext::bGeneratingClassFilter = false;

FNativeGenerationRequirements FNativeGenerationContext::CaptureRequirements()
{
    return {StructureIncludes, ImplementationIncludes, StructureDeclarations, StructureModules};
}

void FNativeGenerationContext::RestoreRequirements(const FNativeGenerationRequirements& Requirements)
{
    StructureIncludes = Requirements.Headers;
    ImplementationIncludes = Requirements.Implementations;
    StructureDeclarations = Requirements.Declarations;
    StructureModules = Requirements.Modules;
}

void FNativeGenerationContext::BeginGeneration()
{
    ClearClassReplacements();
    FNativeApiCatalog::BeginGeneration();
    StructureModules.Reset();
    BeginObject();
}

void FNativeGenerationContext::AddClassReplacement(UClass* OriginalClass, UClass* ParentClass)
{
    ClassReplacements.Add(OriginalClass, ResolveClass(ParentClass));
}

UClass* FNativeGenerationContext::ResolveClass(UClass* Class)
{
    if (Class)
    {
        if (UBlueprint* Blueprint = Cast<UBlueprint>(Class->ClassGeneratedBy))
        {
            if (Blueprint->GeneratedClass)
            {
                Class = Blueprint->GeneratedClass;
            }
        }
    }
    while (UClass* const* ParentClass = ClassReplacements.Find(Class))
    {
        Class = *ParentClass;
    }
    return Class;
}

void FNativeGenerationContext::ClearClassReplacements()
{
    ClassReplacements.Reset();
}

const TMap<UClass*, UClass*>& FNativeGenerationContext::GetClassReplacements()
{
    return ClassReplacements;
}

void FNativeGenerationContext::BeginObject()
{
    StructureIncludes.Reset();
    ImplementationIncludes.Reset();
    StructureDeclarations.Reset();
}

void FNativeGenerationContext::AddRequirements(const FStructTranslationResult& Result)
{
    StructureIncludes.Append(Result.HeaderIncludes);
    StructureDeclarations.Append(Result.ForwardDeclarations);
    StructureModules.Append(Result.ModuleDependencies);
}

const TSet<FString>& FNativeGenerationContext::GetModuleDependencies()
{
    return StructureModules;
}

void FNativeGenerationContext::CompleteHeader(FString& Header)
{
    FString Includes;
    for (const FString& Include : StructureIncludes)
    {
        const FString Directive = FString::Format(TEXT("#include \"{0}\""), {Include});
        if (!Header.Contains(Directive))
        {
            Includes += Directive + TEXT("\n");
        }
    }
    const int32 FirstInclude = Header.Find(TEXT("#include"));
    if (FirstInclude != INDEX_NONE)
    {
        Header.InsertAt(FirstInclude, Includes);
    }
    const int32 LastInclude = Header.Find(TEXT("#include"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
    const int32 DeclarationPosition = Header.Find(TEXT("\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, LastInclude);
    if (DeclarationPosition != INDEX_NONE && !StructureDeclarations.IsEmpty())
    {
        Header.InsertAt(DeclarationPosition + 1, FString::Join(StructureDeclarations.Array(), TEXT("\n")) + TEXT("\n"));
    }
}

void FNativeGenerationContext::AddImplementationInclude(const FString& Include)
{
    ImplementationIncludes.Add(Include);
}

void FNativeGenerationContext::CompleteImplementation(FString& Implementation)
{
    const int32 FirstInclude = Implementation.Find(TEXT("#include"));
    const int32 IncludeEnd = Implementation.Find(TEXT("\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, FirstInclude);
    if (FirstInclude == INDEX_NONE || IncludeEnd == INDEX_NONE)
    {
        return;
    }
    TArray<FString> Includes = ImplementationIncludes.Array();
    Includes.Sort();
    FString AdditionalIncludes;
    for (const FString& Include : Includes)
    {
        if (!Implementation.Contains(Include))
        {
            AdditionalIncludes += Include + TEXT("\n");
        }
    }
    Implementation.InsertAt(IncludeEnd + 1, AdditionalIncludes);
}
