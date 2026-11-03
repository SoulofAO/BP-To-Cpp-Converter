#pragma once

#include "CoreMinimal.h"

class FBlueprintPropertyBindingMigration
{
public:
    void Capture(UObject* Asset, const TMap<FString, FString>& MemberNames);
    int32 Apply(UObject* Asset) const;

private:
    struct FBindingPath
    {
        FGuid StructIdentifier;
        TArray<FName> OriginalNames;
        TArray<FName> NativeNames;
    };
    TMap<UObject*, TArray<FBindingPath>> AssetPaths;
};
