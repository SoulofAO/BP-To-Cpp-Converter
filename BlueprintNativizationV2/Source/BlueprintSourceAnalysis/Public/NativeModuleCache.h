/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class BLUEPRINTSOURCEANALYSIS_API FNativeModuleCache
{
public:
    static FString GetIdentity();
    static FString GetCacheRootDirectory();
    static FString GetConfigurationDirectory();
    static FString GetStorageDirectory(bool bEngine);
    static FString GetIndexFile(bool bEngine);
    static void MarkCacheInvalid(const FString& CacheFile, const FString& Reason);
    static FString GetCacheInvalidReason(bool bEngine);
    static bool ExportTarget(FString& TargetFile, FString& Report);
    static bool Build(const FString& TargetFile, bool bEngine, const FString& StorageDirectory,
                      const FString& IndexFile, FString& Report,
                      TFunction<void(int32, int32, const FString&)> ProgressCallback = {});
    static bool ReadIndex(const FString& IndexFile, TSharedPtr<FJsonObject>& Index, FString& Report, bool bValidateTargetInputs = true);
    static bool FindNativeTypeNameConflicts(const TSet<FString>& CandidateNames, const FString& ExcludedModule,
                                           TMap<FString, FString>& Conflicts, FString& Report);
    static void BuildCurrentTarget(bool bEngine);
};
