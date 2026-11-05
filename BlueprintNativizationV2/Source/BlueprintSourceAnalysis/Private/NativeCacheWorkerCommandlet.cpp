/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright 2026 AO All Rights Reserved.
 */

#include "NativeCacheWorkerCommandlet.h"
#include "NativeModuleCache.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"

UNativeCacheWorkerCommandlet::UNativeCacheWorkerCommandlet()
{
    IsEditor = true;
    IsClient = false;
    IsServer = false;
    UseCommandletResultAsExitCode = true;
}

int32 UNativeCacheWorkerCommandlet::Main(const FString& Parameters)
{
    FString TaskDirectory;
    FString Scope;
    FParse::Value(*Parameters, TEXT("TaskDirectory="), TaskDirectory);
    FParse::Value(*Parameters, TEXT("CacheScope="), Scope);
    if (TaskDirectory.IsEmpty() || (Scope != TEXT("Project") && Scope != TEXT("Engine")))
    {
        return 1;
    }
    FString TargetFile;
    FString Report;
    FFileHelper::SaveStringToFile(TEXT("Exporting editor target configuration"), *(TaskDirectory / TEXT("Progress.txt")));
    bool bSucceeded = FNativeModuleCache::ExportTarget(TargetFile, Report);
    if (bSucceeded)
    {
        const bool bEngine = Scope == TEXT("Engine");
        bSucceeded = FNativeModuleCache::Build(TargetFile, bEngine, FNativeModuleCache::GetStorageDirectory(bEngine),
            FNativeModuleCache::GetIndexFile(bEngine), Report,
            [TaskDirectory](int32 Completed, int32 Total, const FString& Module)
            {
                const FString Progress = FString::Format(TEXT("{0}/{1} target modules visited: {2}"), { Completed, Total, Module });
                FFileHelper::SaveStringToFile(Progress, *(TaskDirectory / TEXT("Progress.txt")));
            });
    }
    FFileHelper::SaveStringToFile(Report, *(TaskDirectory / TEXT("Result.txt")));
    if (!bSucceeded)
    {
        return 1;
    }
    return 0;
}
