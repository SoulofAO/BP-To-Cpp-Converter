/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#include "NativeCacheTask.h"
#include "BlueprintNativeApiCacheLibrary.h"
#include "NativeModuleCache.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/ScopeExit.h"
#include "NativeApiCache.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include <iomanip>
#include <sstream>

static FNativeSourceInput DescribeSourceFile(const FString &File)
{
    FNativeSourceInput Source;
    Source.File = TCHAR_TO_UTF8(*File);
    FString Directory = FPaths::GetPath(File);
    FString ModuleRoot;
    while (!Directory.IsEmpty())
    {
        TArray<FString> Rules;
        IFileManager::Get().FindFiles(Rules, *(Directory / TEXT("*.Build.cs")), true, false);
        if (Rules.Num() == 1)
        {
            FString Module = Rules[0];
            Module.RemoveFromEnd(TEXT(".Build.cs"));
            Source.Module = TCHAR_TO_UTF8(*Module);
            ModuleRoot = Directory;
            break;
        }
        const FString Parent = FPaths::GetPath(Directory);
        if (Parent == Directory)
        {
            break;
        }
        Directory = Parent;
    }
    for (const FString &Prefix : {TEXT("Public/"), TEXT("Classes/")})
    {
        const FString PublicRoot = ModuleRoot / Prefix;
        if (!ModuleRoot.IsEmpty() && File.StartsWith(PublicRoot))
        {
            Source.Include = TCHAR_TO_UTF8(*File.RightChop(PublicRoot.Len()));
            break;
        }
    }
    if (Source.Include.empty() && !ModuleRoot.IsEmpty() &&
        !FPaths::DirectoryExists(ModuleRoot / TEXT("Public")))
    {
        const FString RelativeFile = File.RightChop(ModuleRoot.Len() + 1);
        if (!RelativeFile.StartsWith(TEXT("Private/")) && !RelativeFile.StartsWith(TEXT("Internal/")))
        {
            Source.Include = TCHAR_TO_UTF8(*RelativeFile);
        }
    }
    return Source;
}

TArray<FString> UBlueprintNativeApiCacheLibrary::GetSourceArguments()
{
    return {
        TEXT("-DCPP=1"),
        FString::Format(TEXT("-DWITH_EDITOR={0}"), {WITH_EDITOR}),
        FString::Format(TEXT("-DWITH_EDITORONLY_DATA={0}"), {WITH_EDITORONLY_DATA})
    };
}

bool UBlueprintNativeApiCacheLibrary::BuildSourceCache(const TArray<FString> &SourceFiles, const FString &OutputFile,
                                                       FString &Report)
{
    FNativeApiCacheRequest Request;
    for (const FString& Argument : UBlueprintNativeApiCacheLibrary::GetSourceArguments())
    {
        Request.Arguments.push_back(TCHAR_TO_UTF8(*Argument));
    }
    Request.Configuration = TCHAR_TO_UTF8(*FEngineVersion::Current().ToString());
    TSet<FString> SourceIdentifiers;
    for (const FString &Input : SourceFiles)
    {
        FString File = FPaths::ConvertRelativePathToFull(Input);
        FPaths::NormalizeFilename(File);
        if ((!File.EndsWith(TEXT(".h")) && !File.EndsWith(TEXT(".cpp")) && !File.EndsWith(TEXT(".inl"))) || File.EndsWith(TEXT(".generated.h")) ||
            File.EndsWith(TEXT(".gen.cpp")))
        {
            Report = FString::Format(TEXT("Only original .h, .cpp and .inl files are accepted: {0}"), {File});
            return false;
        }
        Request.Sources.push_back(DescribeSourceFile(File));
        FString Contents;
        FFileHelper::LoadFileToString(Contents, *File);
        for (int32 Position = 0; Position < Contents.Len();)
        {
            if (!FChar::IsAlpha(Contents[Position]) && Contents[Position] != TEXT('_'))
            {
                ++Position;
                continue;
            }
            const int32 Start = Position++;
            while (Position < Contents.Len() && (FChar::IsAlnum(Contents[Position]) || Contents[Position] == TEXT('_')))
            {
                ++Position;
            }
            SourceIdentifiers.Add(Contents.Mid(Start, Position - Start));
        }
    }
    for (TObjectIterator<UStruct> Structure; Structure; ++Structure)
    {
        if (!Structure->IsNative() || Structure->IsA<UFunction>() || Structure->IsA<UBlueprintGeneratedClass>())
        {
            continue;
        }
        const FString NativeName = FString(Structure->GetPrefixCPP()) + Structure->GetName();
        if (!SourceIdentifiers.Contains(NativeName))
        {
            continue;
        }
        FNativeReflectionType Reflected;
        Reflected.Path = TCHAR_TO_UTF8(*Structure->GetPathName());
        Reflected.NativeName = TCHAR_TO_UTF8(*NativeName);
        for (TFieldIterator<FProperty> Property(*Structure, EFieldIterationFlags::None); Property; ++Property)
        {
            Reflected.Properties.emplace(TCHAR_TO_UTF8(*Property->GetName()), TCHAR_TO_UTF8(*Property->GetCPPType()));
        }
        Request.ReflectedTypes.push_back(std::move(Reflected));
    }
    FNativeApiCache Cache;
    try
    {
        Cache = FNativeApiCacheBuilder::Build(Request);
    }
    catch (const std::exception &Exception)
    {
        Report = FString::Format(TEXT("Source analysis failed: {0}"), {UTF8_TO_TCHAR(Exception.what())});
        return false;
    }
    if (!Cache.bSucceeded)
    {
        Report = TEXT("Source analysis failed; previous cache retained.");
        for (const auto& Source : Cache.Sources)
        {
            for (const auto& Diagnostic : Source.Diagnostics)
            {
                if (Diagnostic.Severity == ECppDiagnosticSeverity::Error || Diagnostic.Severity == ECppDiagnosticSeverity::Fatal)
                {
                    Report += FString::Format(TEXT("\n{0}:{1}: {2}"),
                        {UTF8_TO_TCHAR(Source.Source.File.c_str()), static_cast<int32>(Diagnostic.Location.Line), UTF8_TO_TCHAR(Diagnostic.Message.c_str())});
                }
            }
        }
        return false;
    }
    const FString Destination = FPaths::ConvertRelativePathToFull(OutputFile);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Destination), true);
    const FString Temporary = Destination + TEXT(".tmp");
    const FString Json = UTF8_TO_TCHAR(Cache.ToJson().c_str());
    if (!FFileHelper::SaveStringToFile(Json, *Temporary, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) ||
        !IFileManager::Get().Move(*Destination, *Temporary, true, true))
    {
        Report = FString::Format(TEXT("Cannot save source cache: {0}"), {Destination});
        return false;
    }
    unsigned Unresolved = 0;
    for (const auto &Source : Cache.Sources)
    {
        Unresolved += Source.UnresolvedDeclarations;
    }
    Report = FString::Format(TEXT("Source cache: {0} files, {1} types, {2} unresolved declarations. Saved to {3}."),
                             {static_cast<int32>(Cache.Sources.size()), static_cast<int32>(Cache.Types.size()),
                              static_cast<int32>(Unresolved), Destination});
    return Cache.bSucceeded;
}

bool UBlueprintNativeApiCacheLibrary::ValidateSourceCache(const FString &CacheFile, FString &Report)
{
    TSharedPtr<FJsonObject> Cache;
    return ReadValidatedSourceCache(CacheFile, Cache, Report);
}

bool UBlueprintNativeApiCacheLibrary::ReadValidatedSourceCache(const FString& CacheFile, TSharedPtr<FJsonObject>& Cache, FString& Report)
{
    bool bCacheValid = false;
    ON_SCOPE_EXIT
    {
        if (!bCacheValid)
        {
            FNativeModuleCache::MarkCacheInvalid(CacheFile, Report);
        }
    };
    FString Contents;
    if (!FFileHelper::LoadFileToString(Contents, *CacheFile) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Contents), Cache) || !Cache.IsValid())
    {
        Report = TEXT("Cannot read source cache.");
        return false;
    }
    double Schema = 0;
    FString Configuration;
    FString AnalyzerVersion;
    const TArray<TSharedPtr<FJsonValue>> *Sources = nullptr;
    bool bSucceeded = false;
    if (!Cache->TryGetNumberField(TEXT("SchemaVersion"), Schema) || Schema != 1 ||
        !Cache->TryGetStringField(TEXT("AnalyzerVersion"), AnalyzerVersion) ||
        AnalyzerVersion != TEXT("NativeApiCache.Text.20") ||
        !Cache->TryGetStringField(TEXT("Configuration"), Configuration) ||
        Configuration != FEngineVersion::Current().ToString() ||
        !Cache->TryGetBoolField(TEXT("Succeeded"), bSucceeded) || !bSucceeded ||
        !Cache->TryGetArrayField(TEXT("Sources"), Sources) || Sources->IsEmpty())
    {
        Report = TEXT("Source cache version, configuration or parse status is invalid.");
        return false;
    }
    TArray<FString> Arguments;
    if (!Cache->TryGetStringArrayField(TEXT("Arguments"), Arguments) || Arguments != GetSourceArguments())
    {
        Report = TEXT("Source cache preprocessing configuration changed; rebuild cache.");
        return false;
    }
    for (const auto &Value : *Sources)
    {
        const TSharedPtr<FJsonObject> *Source = nullptr;
        FString File;
        FString Fingerprint;
        TArray<uint8> Bytes;
        if (!Value->TryGetObject(Source) || !(*Source)->TryGetStringField(TEXT("File"), File) ||
            !(*Source)->TryGetStringField(TEXT("Fingerprint"), Fingerprint) ||
            !FFileHelper::LoadFileToArray(Bytes, *File))
        {
            Report = TEXT("Source cache contains an unavailable source file.");
            return false;
        }
        uint64 Hash = 14695981039346656037ULL;
        for (uint8 Character : Bytes)
        {
            Hash = (Hash ^ Character) * 1099511628211ULL;
        }
        std::ostringstream Actual;
        Actual << std::hex << Hash;
        if (Fingerprint != UTF8_TO_TCHAR(Actual.str().c_str()))
        {
            Report = FString::Format(TEXT("Source cache is stale: {0}"), {File});
            return false;
        }
        const FNativeSourceInput CurrentSource = DescribeSourceFile(File);
        FString Module;
        FString Include;
        if (!(*Source)->TryGetStringField(TEXT("Module"), Module) ||
            !(*Source)->TryGetStringField(TEXT("Include"), Include) ||
            Module != UTF8_TO_TCHAR(CurrentSource.Module.c_str()) ||
            Include != UTF8_TO_TCHAR(CurrentSource.Include.c_str()))
        {
            Report = FString::Format(TEXT("Source cache include metadata is stale; rebuild cache: {0}"), {File});
            return false;
        }
    }
    Report = TEXT("Source cache is current. Unresolved declarations still require review.");
    bCacheValid = true;
    return true;
}

void UBlueprintNativeApiCacheLibrary::BuildProjectCache()
{
    FString Report;
    FNativeCacheTask::Start(false, Report);
}

void UBlueprintNativeApiCacheLibrary::BuildEditorCache()
{
    FString Report;
    FNativeCacheTask::Start(true, Report);
}
