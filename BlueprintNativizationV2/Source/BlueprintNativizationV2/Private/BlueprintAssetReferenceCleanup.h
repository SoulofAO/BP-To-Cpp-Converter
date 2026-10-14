#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"

class FProperty;
class UStruct;
class UPackage;
struct FEdGraphPinType;

class FBlueprintAssetReferenceCleanup
{
public:
    FBlueprintAssetReferenceCleanup(const TSet<FName>& InMissingPackages, TArray<FString>& InMessages);
    void Apply(UPackage* Package);

private:
    bool GetReplacement(const FSoftObjectPath& Original, FSoftObjectPath& Replacement) const;
    bool VisitValue(FProperty* Property, void* Address, const FString& Location, bool bApply);
    bool VisitStructure(UStruct* Structure, void* Address, const FString& Location, bool bApply);
    bool VisitDefault(const FEdGraphPinType& PinType, FString& DefaultValue, UObject* Owner, const FString& Location);
    const TSet<FName>& MissingPackages;
    TArray<FString>& Messages;
};
