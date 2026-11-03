/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#include "EngineAssetPolicy.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/BlueprintSupport.h"
#include "Engine/Blueprint.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

bool FEngineAssetPolicy::IsEnginePackage(const FString& PackageName)
{
    if (PackageName == GetTransientPackage()->GetName())
    {
        return false;
    }
    FString PackageFilename;
    return FPackageName::TryConvertLongPackageNameToFilename(PackageName, PackageFilename) &&
        FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(PackageFilename), FPaths::ConvertRelativePathToFull(FPaths::EngineDir()));
}

FString FEngineAssetPolicy::GetExclusionReason(const UObject* Object, bool bExcludeEngineAssets)
{
    if (!bExcludeEngineAssets || !Object)
    {
        return {};
    }
    const UBlueprint* Blueprint = Cast<UBlueprint>(Object);
    if (const UClass* ObjectClass = Cast<UClass>(Object))
    {
        if (ObjectClass->HasAnyClassFlags(CLASS_Native))
        {
            return {};
        }
        Blueprint = Cast<UBlueprint>(ObjectClass->ClassGeneratedBy);
    }
    if (IsEnginePackage(Object->GetOutermost()->GetName()))
    {
        return FString::Format(TEXT("Engine asset is excluded from conversion: {0}"), { Object->GetPathName() });
    }
    if (Blueprint)
    {
        for (const UClass* ParentClass = Blueprint->ParentClass; ParentClass; ParentClass = ParentClass->GetSuperClass())
        {
            if (ParentClass->HasAnyClassFlags(CLASS_Native))
            {
                break;
            }
            if (IsEnginePackage(ParentClass->GetOutermost()->GetName()))
            {
                return FString::Format(TEXT("Blueprint inherits from an excluded Engine Blueprint: {0} -> {1}"),
                    { Blueprint->GetPathName(), GetPathNameSafe(ParentClass->ClassGeneratedBy) });
            }
        }
    }
    return {};
}

FString FEngineAssetPolicy::GetExclusionReason(const FAssetData& Asset, bool bExcludeEngineAssets)
{
    if (!bExcludeEngineAssets)
    {
        return {};
    }
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    FAssetData CurrentAsset = Asset;
    TSet<FName> VisitedPackages;
    while (CurrentAsset.IsValid() && !VisitedPackages.Contains(CurrentAsset.PackageName))
    {
        VisitedPackages.Add(CurrentAsset.PackageName);
        if (IsEnginePackage(CurrentAsset.PackageName.ToString()))
        {
            return FString::Format(TEXT("Asset is excluded by Engine ownership: {0} -> {1}"),
                { Asset.GetObjectPathString(), CurrentAsset.GetObjectPathString() });
        }
        FString ParentClassPath;
        if (!CurrentAsset.GetTagValue(FBlueprintTags::ParentClassPath, ParentClassPath))
        {
            break;
        }
        ParentClassPath = FPackageName::ExportTextPathToObjectPath(ParentClassPath);
        if (ParentClassPath.StartsWith(TEXT("/Script/")))
        {
            break;
        }
        if (IsEnginePackage(FPackageName::ObjectPathToPackageName(ParentClassPath)))
        {
            return FString::Format(TEXT("Blueprint inherits from an excluded Engine Blueprint: {0} -> {1}"),
                { Asset.GetObjectPathString(), ParentClassPath });
        }
        ParentClassPath.RemoveFromEnd(TEXT("_C"));
        CurrentAsset = Registry.GetAssetByObjectPath(FSoftObjectPath(ParentClassPath));
    }
    return {};
}
