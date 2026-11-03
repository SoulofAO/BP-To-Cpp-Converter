/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#include "BlueprintSubsystemReinstanceScope.h"
#include "StructPrivateMemberAccess.h"
#include "Subsystems/Subsystem.h"
#include "Subsystems/SubsystemCollection.h"
#include "UObject/UObjectGlobals.h"

struct FSubsystemCollectionTypes : FSubsystemCollectionBase
{
    using FSubsystemCollectionBase::FSubsystemArray;
};

struct FSubsystemOwnerAccess
{
    using Member = FSubsystemCollectionBase* USubsystem::*;
    friend Member GetStructPrivateMember(FSubsystemOwnerAccess);
};
template struct TStructPrivateMemberAccess<FSubsystemOwnerAccess, &USubsystem::InternalOwningSubsystem>;

struct FSubsystemInstancesAccess
{
    using Member = TMap<TObjectPtr<UClass>, TObjectPtr<USubsystem>> FSubsystemCollectionBase::*;
    friend Member GetStructPrivateMember(FSubsystemInstancesAccess);
};
template struct TStructPrivateMemberAccess<FSubsystemInstancesAccess, &FSubsystemCollectionBase::SubsystemMap>;

struct FSubsystemArraysAccess
{
    using Member = TMap<UClass*, TUniquePtr<FSubsystemCollectionTypes::FSubsystemArray>> FSubsystemCollectionBase::*;
    friend Member GetStructPrivateMember(FSubsystemArraysAccess);
};
template struct TStructPrivateMemberAccess<FSubsystemArraysAccess, &FSubsystemCollectionBase::SubsystemArrayMap>;

FBlueprintSubsystemReinstanceScope::FBlueprintSubsystemReinstanceScope()
{
    ReinstanceRegistration = FCoreUObjectDelegates::OnObjectsReinstanced.AddLambda([](const TMap<UObject*, UObject*>& Replacements)
    {
        for (const TPair<UObject*, UObject*>& Replacement : Replacements)
        {
            if (Replacement.Key == Replacement.Value)
            {
                continue;
            }
            USubsystem* PreviousSubsystem = Cast<USubsystem>(Replacement.Key);
            if (!PreviousSubsystem)
            {
                continue;
            }
            FSubsystemCollectionBase*& PreviousOwner = PreviousSubsystem->*GetStructPrivateMember(FSubsystemOwnerAccess{});
            if (!PreviousOwner)
            {
                continue;
            }
            USubsystem* ReplacementSubsystem = Cast<USubsystem>(Replacement.Value);
            checkf(ReplacementSubsystem, TEXT("A registered subsystem was reinstanced without a subsystem replacement: %s"), *PreviousSubsystem->GetPathName());
            auto& CachedArrays = PreviousOwner->*GetStructPrivateMember(FSubsystemArraysAccess{});
            for (const auto& CachedArray : CachedArrays)
            {
                checkf(!CachedArray.Value->bIsIterating, TEXT("Subsystem reinstancing during collection iteration is unsupported: %s"), *PreviousSubsystem->GetPathName());
            }
            auto& Instances = PreviousOwner->*GetStructPrivateMember(FSubsystemInstancesAccess{});
            bool bRegistered = false;
            for (auto Instance = Instances.CreateIterator(); Instance; ++Instance)
            {
                if (Instance.Value() == PreviousSubsystem || Instance.Value() == ReplacementSubsystem)
                {
                    Instance.RemoveCurrent();
                    bRegistered = true;
                }
            }
            if (!bRegistered)
            {
                continue;
            }
            // GC reference replacement does not update the raw collection caches or the non-reflected owner.
            ReplacementSubsystem->*GetStructPrivateMember(FSubsystemOwnerAccess{}) = PreviousOwner;
            Instances.Add(ReplacementSubsystem->GetClass(), ReplacementSubsystem);
            CachedArrays.Reset();
            PreviousOwner = nullptr;
            UE_LOG(LogTemp, Display, TEXT("Reassociated registered subsystem after Blueprint reinstancing: %s"), *ReplacementSubsystem->GetPathName());
        }
    });
}

FBlueprintSubsystemReinstanceScope::~FBlueprintSubsystemReinstanceScope()
{
    FCoreUObjectDelegates::OnObjectsReinstanced.Remove(ReinstanceRegistration);
}
