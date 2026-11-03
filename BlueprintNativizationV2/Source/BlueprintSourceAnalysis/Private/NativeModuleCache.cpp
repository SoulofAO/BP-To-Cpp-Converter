/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#include "NativeModuleCache.h"
#include "BlueprintNativeApiCacheLibrary.h"
#include "DesktopPlatformModule.h"
#include "Interfaces/IPluginManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/SecureHash.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

static bool ReadJson(const FString& File, TSharedPtr<FJsonObject>& Object)
{
    FString Text;
    return FFileHelper::LoadFileToString(Text, *File) &&
           FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object) && Object.IsValid();
}

static bool WriteJson(const FString& File, const TSharedPtr<FJsonObject>& Object)
{
    FString Text;
    FJsonSerializer::Serialize(Object.ToSharedRef(), TJsonWriterFactory<>::Create(&Text));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
    const FString Temporary = File + TEXT(".") + FGuid::NewGuid().ToString() + TEXT(".tmp");
    return FFileHelper::SaveStringToFile(Text, *Temporary, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) &&
           IFileManager::Get().Move(*File, *Temporary, true, true);
}

static void AddIndexedTypeOwner(const TSharedPtr<FJsonObject>& Type, const FString& ModuleName,
    const TSharedPtr<FJsonObject>& Owners)
{
    if (!Type->GetBoolField(TEXT("Definition")))
    {
        return;
    }
    const TSharedPtr<FJsonObject>* Declaration = nullptr;
    if (Type->TryGetObjectField(TEXT("Declaration"), Declaration))
    {
        if (!(*Declaration)->GetStringField(TEXT("Condition")).IsEmpty())
        {
            return;
        }
    }
    else
    {
        for (const TSharedPtr<FJsonValue>& Evidence : Type->GetArrayField(TEXT("Evidence")))
        {
            if (!Evidence->AsObject()->GetStringField(TEXT("Condition")).IsEmpty())
            {
                return;
            }
        }
    }
    const FString TypeName = Type->GetStringField(TEXT("Name"));
    FString ExistingOwner;
    if (Owners->TryGetStringField(TypeName, ExistingOwner) && ExistingOwner != ModuleName)
    {
        Owners->SetStringField(TypeName, TEXT(""));
    }
    else
    {
        Owners->SetStringField(TypeName, ModuleName);
    }
}

FString FNativeModuleCache::GetIdentity()
{
    return FPaths::ConvertRelativePathToFull(FPaths::EngineDir()) + TEXT("|") +
           FEngineVersion::Current().ToString() + TEXT("|Win64|") +
           LexToString(FApp::GetBuildConfiguration()) + TEXT("|NativeApiCache.Text.20|") +
           FString::Join(UBlueprintNativeApiCacheLibrary::GetSourceArguments(), TEXT("|"));
}

FString FNativeModuleCache::GetCacheRootDirectory()
{
    return FPaths::ConvertRelativePathToFull(IPluginManager::Get().FindPlugin(TEXT("BlueprintNativizationV2"))->GetBaseDir() / TEXT("NativeApiCache"));
}

FString FNativeModuleCache::GetConfigurationDirectory()
{
    return GetCacheRootDirectory() / TEXT("Projects") / FPaths::GetBaseFilename(FPaths::GetProjectFilePath()) /
        LexToString(FApp::GetBuildConfiguration()) / FMD5::HashAnsiString(*GetIdentity());
}

FString FNativeModuleCache::GetStorageDirectory(bool bEngine)
{
    if (bEngine)
    {
        return GetCacheRootDirectory() / TEXT("Engine") / LexToString(FApp::GetBuildConfiguration()) /
            FMD5::HashAnsiString(*GetIdentity()) / TEXT("Modules");
    }
    return GetConfigurationDirectory() / TEXT("Modules");
}

FString FNativeModuleCache::GetIndexFile(bool bEngine)
{
    FString Name = TEXT("ProjectModules.json");
    if (bEngine)
    {
        Name = TEXT("EngineModules.json");
    }
    return GetConfigurationDirectory() / Name;
}

bool FNativeModuleCache::ExportTarget(FString& TargetFile, FString& Report)
{
    const FString Project = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
    FString TargetName;
    for (const FTargetInfo& Target : FDesktopPlatformModule::Get()->GetTargetsForProject(Project))
    {
        if (Target.Type == EBuildTargetType::Editor)
        {
            if (!TargetName.IsEmpty())
            {
                Report = TEXT("Multiple editor targets found; export the desired target explicitly.");
                return false;
            }
            TargetName = Target.Name;
        }
    }
    if (TargetName.IsEmpty())
    {
        Report = TEXT("No editor target found for the current project.");
        return false;
    }
    TArray<FString> Runtimes;
    IFileManager::Get().FindFilesRecursive(Runtimes, *(FPaths::EngineDir() / TEXT("Binaries/ThirdParty/DotNet")),
                                          TEXT("dotnet.exe"), true, false);
    Runtimes.RemoveAll([](const FString& File) { return !File.Contains(TEXT("win-x64")); });
    Runtimes.Sort();
    if (Runtimes.IsEmpty())
    {
        Report = TEXT("Bundled Win64 .NET runtime is unavailable.");
        return false;
    }
    TargetFile = FPaths::ConvertRelativePathToFull(GetConfigurationDirectory() / TEXT("TargetModules.json"));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(TargetFile), true);
    const FString Arguments = FString::Format(
        TEXT("\"{0}\" -Mode=JsonExport {1} Win64 {2} -Project=\"{3}\" -OutputFile=\"{4}\""),
        {FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll")),
         TargetName, LexToString(FApp::GetBuildConfiguration()), Project, TargetFile});
    int32 ExitCode = -1;
    FString Errors;
    if (!FPlatformProcess::ExecProcess(*Runtimes.Last(), *Arguments, &ExitCode, &Report, &Errors) || ExitCode != 0)
    {
        Report += Errors;
        return false;
    }
    return true;
}

void FNativeModuleCache::MarkCacheInvalid(const FString& CacheFile, const FString& Reason)
{
    const FString AbsoluteCacheFile = FPaths::ConvertRelativePathToFull(CacheFile);
    for (const bool bEngine : {false, true})
    {
        const FString IndexFile = GetIndexFile(bEngine);
        if (FPaths::IsSamePath(AbsoluteCacheFile, IndexFile) ||
            FPaths::IsUnderDirectory(AbsoluteCacheFile, GetStorageDirectory(bEngine)))
        {
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(IndexFile), true);
            FFileHelper::SaveStringToFile(Reason, *(IndexFile + TEXT(".invalid")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
            return;
        }
    }
}

FString FNativeModuleCache::GetCacheInvalidReason(bool bEngine)
{
    FString Reason;
    const FString StatusFile = GetIndexFile(bEngine) + TEXT(".invalid");
    if (FPaths::FileExists(StatusFile))
    {
        FFileHelper::LoadFileToString(Reason, *StatusFile);
    }
    return Reason;
}

bool FNativeModuleCache::ReadIndex(const FString& IndexFile, TSharedPtr<FJsonObject>& Index, FString& Report, bool bValidateTargetInputs)
{
    bool bCacheValid = false;
    ON_SCOPE_EXIT
    {
        if (!bCacheValid)
        {
            MarkCacheInvalid(IndexFile, Report);
        }
    };
    FString Identity;
    double Version = 0;
    const TSharedPtr<FJsonObject>* Modules = nullptr;
    if (!ReadJson(IndexFile, Index) || !Index->TryGetNumberField(TEXT("Version"), Version) || Version != 1 ||
        !Index->TryGetStringField(TEXT("Identity"), Identity) || Identity != GetIdentity() ||
        !Index->TryGetObjectField(TEXT("Modules"), Modules))
    {
        Report = FString::Format(TEXT("Module cache index is invalid or belongs to another engine configuration: {0}"), {IndexFile});
        return false;
    }
    const TSharedPtr<FJsonObject>* Inputs = nullptr;
    if (!Index->TryGetObjectField(TEXT("TargetInputs"), Inputs))
    {
        Report = TEXT("Module index has no target input fingerprints.");
        return false;
    }
    if (bValidateTargetInputs)
    {
        const bool bEngineIndex = FPaths::IsSamePath(FPaths::ConvertRelativePathToFull(IndexFile), GetIndexFile(true));
        const FString EngineDirectory = FPaths::ConvertRelativePathToFull(FPaths::EngineDir());
        for (const auto& Input : (*Inputs)->Values)
        {
            const FString File(Input.Key);
            if (bEngineIndex && !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(File), EngineDirectory))
            {
                continue;
            }
            if (!FPaths::FileExists(File) || LexToString(FMD5Hash::HashFile(*File)) != Input.Value->AsString())
            {
                Report = FString::Format(TEXT("Target configuration changed; rebuild module caches: {0}"), {File});
                return false;
            }
        }
    }
    double TypeOwnerPolicy = 0;
    if (!Index->TryGetNumberField(TEXT("TypeOwnerPolicy"), TypeOwnerPolicy) || TypeOwnerPolicy != 2)
    {
        const TSharedPtr<FJsonObject> Owners = MakeShared<FJsonObject>();
        for (const auto& Entry : (*Modules)->Values)
        {
            FString CacheFile;
            if (!Entry.Value->AsObject()->TryGetStringField(TEXT("File"), CacheFile))
            {
                continue;
            }
            if (FPaths::IsRelative(CacheFile))
            {
                CacheFile = FPaths::ConvertRelativePathToFull(FPaths::GetPath(IndexFile), CacheFile);
            }
            TSharedPtr<FJsonObject> Cache;
            if (!ReadJson(CacheFile, Cache))
            {
                Report = TEXT("Cannot index existing module metadata: ") + CacheFile;
                return false;
            }
            for (const TSharedPtr<FJsonValue>& Type : Cache->GetArrayField(TEXT("Types")))
            {
                AddIndexedTypeOwner(Type->AsObject(), FString(Entry.Key), Owners);
            }
        }
        Index->SetObjectField(TEXT("TypeOwners"), Owners);
        Index->SetNumberField(TEXT("TypeOwnerPolicy"), 2);
        WriteJson(IndexFile, Index);
    }
    for (const auto& Entry : (*Modules)->Values)
    {
        FString CacheFile;
        const auto Module = Entry.Value->AsObject();
        if (Module->TryGetStringField(TEXT("File"), CacheFile) && FPaths::IsRelative(CacheFile))
        {
            Module->SetStringField(TEXT("File"), FPaths::ConvertRelativePathToFull(FPaths::GetPath(IndexFile), CacheFile));
        }
    }
    bCacheValid = true;
    return true;
}

bool FNativeModuleCache::Build(const FString& TargetFile, bool bEngine, const FString& StorageDirectory,
                              const FString& IndexFile, FString& Report, TFunction<void(int32, int32, const FString&)> ProgressCallback)
{
    TSharedPtr<FJsonObject> Target;
    const TSharedPtr<FJsonObject>* Modules = nullptr;
    if (!ReadJson(TargetFile, Target) || !Target->TryGetObjectField(TEXT("Modules"), Modules))
    {
        Report = TEXT("Cannot read target module export.");
        return false;
    }
    const FString EngineRoot = FPaths::ConvertRelativePathToFull(FPaths::EngineDir());
    auto Index = MakeShared<FJsonObject>();
    auto Entries = MakeShared<FJsonObject>();
    auto Owners = MakeShared<FJsonObject>();
    auto Aliases = MakeShared<FJsonObject>();
    auto ConstantOwners = MakeShared<FJsonObject>();
    auto Inputs = MakeShared<FJsonObject>();
    Index->SetNumberField(TEXT("Version"), 1);
    Index->SetStringField(TEXT("Identity"), GetIdentity());
    Index->SetStringField(TEXT("Target"), Target->GetStringField(TEXT("Name")));
    Index->SetObjectField(TEXT("Modules"), Entries);
    Index->SetObjectField(TEXT("TypeOwners"), Owners);
    Index->SetNumberField(TEXT("TypeOwnerPolicy"), 2);
    Index->SetObjectField(TEXT("TypeAliases"), Aliases);
    Index->SetObjectField(TEXT("ConstantOwners"), ConstantOwners);
    Index->SetObjectField(TEXT("TargetInputs"), Inputs);
    FString ProjectFile;
    if (!bEngine && Target->TryGetStringField(TEXT("ProjectFile"), ProjectFile) && FPaths::FileExists(ProjectFile))
    {
        Inputs->SetStringField(ProjectFile, LexToString(FMD5Hash::HashFile(*ProjectFile)));
        TArray<FString> TargetRules;
        IFileManager::Get().FindFilesRecursive(TargetRules, *(FPaths::GetPath(ProjectFile) / TEXT("Source")), TEXT("*.Target.cs"), true, false);
        for (const FString& File : TargetRules)
        {
            Inputs->SetStringField(File, LexToString(FMD5Hash::HashFile(*File)));
        }
    }
    TArray<FString> Names;
    for (const auto& Pair : (*Modules)->Values)
    {
        Names.Add(FString(Pair.Key));
    }
    Names.Sort();
    FScopedSlowTask Progress(Names.Num(), NSLOCTEXT("NativeApiCache", "Modules", "Building module API caches"));
    if (!IsRunningCommandlet())
    {
        Progress.MakeDialog(true);
    }
    int32 CompletedModules = 0;
    int32 Built = 0;
    int32 Reused = 0;
    for (const FString& Name : Names)
    {
        if (ProgressCallback)
        {
            ProgressCallback(CompletedModules++, Names.Num(), Name);
        }
        Progress.EnterProgressFrame(1, FText::FromString(Name));
        if (Progress.ShouldCancel())
        {
            Report = TEXT("Cache generation cancelled; previous index retained.");
            return false;
        }
        const auto Module = (*Modules)->GetObjectField(Name);
        const FString Directory = FPaths::ConvertRelativePathToFull(Module->GetStringField(TEXT("Directory")));
        if (bEngine && !FPaths::IsUnderDirectory(Directory, EngineRoot))
        {
            continue;
        }
        FString Rules;
        if (Module->TryGetStringField(TEXT("Rules"), Rules) && FPaths::FileExists(Rules))
        {
            Inputs->SetStringField(Rules, LexToString(FMD5Hash::HashFile(*Rules)));
        }
        FString ParentDirectory = Directory;
        while (!ParentDirectory.IsEmpty())
        {
            TArray<FString> Descriptors;
            IFileManager::Get().FindFiles(Descriptors, *(ParentDirectory / TEXT("*.uplugin")), true, false);
            for (const FString& Descriptor : Descriptors)
            {
                const FString File = ParentDirectory / Descriptor;
                Inputs->SetStringField(File, LexToString(FMD5Hash::HashFile(*File)));
            }
            if (!Descriptors.IsEmpty())
            {
                break;
            }
            const FString NextDirectory = FPaths::GetPath(ParentDirectory);
            if (NextDirectory == ParentDirectory)
            {
                break;
            }
            ParentDirectory = NextDirectory;
        }
        if (FPaths::IsUnderDirectory(Directory, EngineRoot) != bEngine)
        {
            continue;
        }
        auto Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("Directory"), Directory);
        TArray<TSharedPtr<FJsonValue>> Dependencies;
        for (const FString& Key : {TEXT("PublicDependencyModules"), TEXT("PrivateDependencyModules"),
                                  TEXT("DynamicallyLoadedModules"), TEXT("PublicIncludePathModules"), TEXT("PrivateIncludePathModules")})
        {
            const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
            if (Module->TryGetArrayField(Key, Values))
            {
                Dependencies.Append(*Values);
            }
        }
        Entry->SetArrayField(TEXT("Dependencies"), Dependencies);
        TArray<FString> Files;
        if (Module->GetStringField(TEXT("Type")) == TEXT("CPlusPlus"))
        {
            IFileManager::Get().FindFilesRecursive(Files, *Directory, TEXT("*.h"), true, false);
            IFileManager::Get().FindFilesRecursive(Files, *Directory, TEXT("*.cpp"), true, false, false);
            IFileManager::Get().FindFilesRecursive(Files, *Directory, TEXT("*.inl"), true, false, false);
        }
        Files.RemoveAll([](FString& File)
        {
            FPaths::NormalizeFilename(File);
            return File.Contains(TEXT("/Intermediate/")) || File.EndsWith(TEXT(".generated.h")) || File.EndsWith(TEXT(".gen.cpp"));
        });
        Files.Sort();
        Entry->SetNumberField(TEXT("SourceCount"), Files.Num());
        Entries->SetObjectField(Name, Entry);
        if (Files.IsEmpty())
        {
            Entry->SetStringField(TEXT("Status"), TEXT("NoOriginalSources"));
            continue;
        }
        FString Signature = GetIdentity();
        FString ModuleJson;
        FJsonSerializer::Serialize(Module.ToSharedRef(), TJsonWriterFactory<>::Create(&ModuleJson));
        Signature += ModuleJson;
        for (const FString& File : Files)
        {
            TArray<uint8> Bytes;
            if (!FFileHelper::LoadFileToArray(Bytes, *File))
            {
                Report = FString::Format(TEXT("Cannot read module source: {0}"), {File});
                return false;
            }
            FMD5 Hash;
            Hash.Update(Bytes.GetData(), Bytes.Num());
            uint8 Digest[16];
            Hash.Final(Digest);
            Signature += File + BytesToHex(Digest, 16);
        }
        const FString CacheFile = FPaths::ConvertRelativePathToFull(StorageDirectory / (Name + TEXT("-") + FMD5::HashAnsiString(*Signature) + TEXT(".json")));
        TSharedPtr<FJsonObject> Cache;
        FString Validation;
        if (UBlueprintNativeApiCacheLibrary::ReadValidatedSourceCache(CacheFile, Cache, Validation))
        {
            ++Reused;
        }
        else
        {
            UKismetSystemLibrary::PrintString(nullptr,
                FString::Format(TEXT("Analyzing native API module {0}: {1} source files."), {Name, Files.Num()}), false, true);
            const FString Staging = CacheFile + TEXT(".") + FGuid::NewGuid().ToString() + TEXT(".staging");
            if (!UBlueprintNativeApiCacheLibrary::BuildSourceCache(Files, Staging, Report) || !ReadJson(Staging, Cache))
            {
                Report = Name + TEXT(": ") + Report;
                return false;
            }
            auto Types = Cache->GetArrayField(TEXT("Types"));
            Types.RemoveAll([&](const TSharedPtr<FJsonValue>& Value)
            {
                const auto Type = Value->AsObject();
                return !Type->GetBoolField(TEXT("Definition")) || Type->GetStringField(TEXT("Module")) != Name;
            });
            Cache->SetArrayField(TEXT("Types"), Types);
            if (!WriteJson(CacheFile, Cache))
            {
                Report = TEXT("Cannot publish module cache.");
                return false;
            }
            IFileManager::Get().Delete(*Staging);
            ++Built;
        }
        FString RelativeCacheFile = CacheFile;
        const FString IndexDirectory = FPaths::ConvertRelativePathToFull(FPaths::GetPath(IndexFile)) + TEXT("/");
        FPaths::MakePathRelativeTo(RelativeCacheFile, *IndexDirectory);
        Entry->SetStringField(TEXT("File"), RelativeCacheFile);
        Entry->SetStringField(TEXT("Status"), TEXT("Ready"));
        for (const auto& Value : Cache->GetArrayField(TEXT("Types")))
        {
            AddIndexedTypeOwner(Value->AsObject(), Name, Owners);
        }
        for (const TSharedPtr<FJsonValue>& Value : Cache->GetArrayField(TEXT("Constants")))
        {
            const FString ConstantName = Value->AsObject()->GetStringField(TEXT("Name"));
            FString ExistingOwner;
            if (ConstantOwners->TryGetStringField(ConstantName, ExistingOwner) && ExistingOwner != Name)
            {
                ConstantOwners->SetStringField(ConstantName, TEXT(""));
            }
            else
            {
                ConstantOwners->SetStringField(ConstantName, Name);
            }
        }
        for (const auto& Value : Cache->GetArrayField(TEXT("ReflectionMatches")))
        {
            const auto Match = Value->AsObject();
            const FString NativeName = Match->GetStringField(TEXT("NativeName"));
            const FString Declaration = Match->GetStringField(TEXT("Declaration"));
            if (Match->GetBoolField(TEXT("Resolved")) && NativeName != Declaration)
            {
                FString Existing;
                if (Aliases->TryGetStringField(NativeName, Existing) && Existing != Declaration)
                {
                    Aliases->SetStringField(NativeName, TEXT(""));
                }
                else
                {
                    Aliases->SetStringField(NativeName, Declaration);
                }
            }
        }
    }
    if (!WriteJson(IndexFile, Index))
    {
        Report = TEXT("Cannot publish module cache index.");
        return false;
    }
    Report = FString::Format(TEXT("Module API cache: {0} built, {1} reused, {2} modules indexed."), {Built, Reused, Entries->Values.Num()});
    IFileManager::Get().Delete(*(IndexFile + TEXT(".invalid")));
    return true;
}

void FNativeModuleCache::BuildCurrentTarget(bool bEngine)
{
    FString TargetFile;
    FString Report;
    if (ExportTarget(TargetFile, Report))
    {
        Build(TargetFile, bEngine, GetStorageDirectory(bEngine), GetIndexFile(bEngine), Report);
    }
    UKismetSystemLibrary::PrintString(nullptr, Report, true, true);
}
