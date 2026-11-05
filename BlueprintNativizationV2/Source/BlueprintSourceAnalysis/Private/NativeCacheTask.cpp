/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright 2026 AO All Rights Reserved.
 */

#include "NativeCacheTask.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

static FProcHandle CacheProcess;
static FString TaskDirectory;
static FString TaskState = TEXT("Idle");
static FString TaskScope;

bool FNativeCacheTask::IsRunning()
{
    if (CacheProcess.IsValid() && !FPlatformProcess::IsProcRunning(CacheProcess))
    {
        int32 ExitCode = -1;
        FPlatformProcess::GetProcReturnCode(CacheProcess, &ExitCode);
        FPlatformProcess::CloseProc(CacheProcess);
        CacheProcess.Reset();
        TaskState = TEXT("Failed");
        if (ExitCode == 0)
        {
            TaskState = TEXT("Succeeded");
        }
    }
    return CacheProcess.IsValid();
}

bool FNativeCacheTask::Start(bool bEngine, FString& Report)
{
    if (IsRunning())
    {
        Report = TEXT("A cache task is already running.");
        return false;
    }
    TaskScope = TEXT("Project");
    if (bEngine)
    {
        TaskScope = TEXT("Engine");
    }
    TaskDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("NativeApiCache/Tasks") / FGuid::NewGuid().ToString(EGuidFormats::Digits));
    IFileManager::Get().MakeDirectory(*TaskDirectory, true);
    const FString Arguments = FString::Format(TEXT("\"{0}\" -run=NativeCacheWorker -CacheScope={1} -TaskDirectory=\"{2}\" -nullrhi -unattended -nosplash -NoSound -ddc=NoZenLocalFallback -abslog=\"{2}/Worker.log\""),
        { FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()), TaskScope, TaskDirectory });
    const FString Executable = FPlatformProcess::ExecutablePath();
    CacheProcess = FPlatformProcess::CreateProc(*Executable, *Arguments, false, true, true, nullptr, 0, nullptr, nullptr);
    TaskState = TEXT("Running");
    if (!CacheProcess.IsValid())
    {
        TaskState = TEXT("Failed to start");
    }
    Report = GetStatus();
    return CacheProcess.IsValid();
}

FString FNativeCacheTask::GetStatus()
{
    IsRunning();
    if (TaskDirectory.IsEmpty())
    {
        return TEXT("No cache task started in this editor session.");
    }
    FString Progress;
    FString Result;
    FFileHelper::LoadFileToString(Progress, *(TaskDirectory / TEXT("Progress.txt")));
    FFileHelper::LoadFileToString(Result, *(TaskDirectory / TEXT("Result.txt")));
    if (TaskState == TEXT("Failed") && Result.IsEmpty())
    {
        Result = TEXT("Worker exited before reporting a result. If a module was just prepared, compile the project before building caches. See Worker.log in the task directory.");
    }
    return TaskScope + TEXT(" cache: ") + TaskState + TEXT("\n") + Progress + TEXT("\n") + Result + TEXT("\n") + TaskDirectory;
}

void FNativeCacheTask::GetProgress(FString& Label, TOptional<float>& Fraction)
{
    Fraction.Reset();
    Label = TaskScope + TEXT(" API: ") + TaskState;
    FString Progress;
    if (IsRunning() && FFileHelper::LoadFileToString(Progress, *(TaskDirectory / TEXT("Progress.txt"))))
    {
        FString Counts;
        FString Description;
        FString Completed;
        FString Total;
        if (Progress.Split(TEXT(" "), &Counts, &Description) && Counts.Split(TEXT("/"), &Completed, &Total) &&
            Completed.IsNumeric() && Total.IsNumeric() && FCString::Atoi(*Total) > 0)
        {
            Fraction = FMath::Clamp(static_cast<float>(FCString::Atoi(*Completed)) / FCString::Atoi(*Total), 0.0f, 1.0f);
            Label = TaskScope + TEXT(" API: ") + Counts + TEXT(" modules");
        }
        else
        {
            Label = TaskScope + TEXT(" API: ") + Progress.TrimStartAndEnd();
        }
    }
}

void FNativeCacheTask::Cancel()
{
    if (IsRunning())
    {
        FPlatformProcess::TerminateProc(CacheProcess, true);
        FPlatformProcess::WaitForProc(CacheProcess);
        FPlatformProcess::CloseProc(CacheProcess);
        CacheProcess.Reset();
        TaskState = TEXT("Cancelled; previous index retained unless publication had already completed");
    }
}
