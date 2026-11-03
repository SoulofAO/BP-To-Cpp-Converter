#include "GeneratedSubsystemRuntimeGeneration.h"

FNativizationCode FGeneratedSubsystemRuntimeGeneration::Generate(const FString& ModuleName)
{
    FNativizationCode Result;
    Result.ObjectName = TEXT("GeneratedSubsystemRuntime");
    Result.OutputHeaderString = TEXT(R"SUBSYSTEM(#pragma once
#include "CoreMinimal.h"

class MODULE_EXPORT FGeneratedSubsystemRuntime
{
public:
    static void Startup();
    static void Shutdown();
    static bool ShouldCreateSubsystem(const UObject* Object, UObject* Outer, UClass* NativeClass);
    static void RegisterSubsystemClass(UClass* Class);
};
)SUBSYSTEM");
    Result.OutputCppString = TEXT(R"SUBSYSTEM(#include "MODULE_HEADER.h"
#include "CoreGlobals.h"
#include "Misc/CoreDelegates.h"
#include "Subsystems/Subsystem.h"
#include "Subsystems/SubsystemCollection.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectThreadContext.h"

struct FGeneratedSubsystemActivation
{
    using FOperation = void (*)(TConstArrayView<UClass*>);
    friend FOperation GetGeneratedSubsystemActivation(FGeneratedSubsystemActivation);
};

template <typename ActivationType, typename ActivationType::FOperation Operation>
struct TGeneratedSubsystemActivationAccess
{
    friend typename ActivationType::FOperation GetGeneratedSubsystemActivation(ActivationType)
    {
        return Operation;
    }
};

template struct TGeneratedSubsystemActivationAccess<FGeneratedSubsystemActivation, &FSubsystemCollectionBase::AddAllInstances>;

class FGeneratedSubsystemRegistry
{
public:
    TMap<UClass*, TStrongObjectPtr<UClass>> SubsystemClasses;
    TSet<UClass*> ActivatedSubsystemClasses;
    bool bActivatingSubsystems = false;
    FDelegateHandle ModuleLoadingRegistration;
    FDelegateHandle PackageLoadingRegistration;
    FDelegateHandle FrameRegistration;

    static FGeneratedSubsystemRegistry& Get()
    {
        static FGeneratedSubsystemRegistry Registry;
        return Registry;
    }

    void ActivateSubsystems()
    {
        if (bActivatingSubsystems || !IsEngineStartupModuleLoadingComplete() ||
            FUObjectThreadContext::Get().IsRoutingPostLoad)
        {
            return;
        }
        TGuardValue<bool> ActivationGuard(bActivatingSubsystems, true);
        bool bHasNewClasses = true;
        while (bHasNewClasses)
        {
            bHasNewClasses = false;
            TArray<UClass*> Classes;
            for (const auto& Entry : SubsystemClasses)
            {
                UClass* Class = Entry.Key;
                if (!ActivatedSubsystemClasses.Contains(Class) &&
                    !Class->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad) &&
                    !Class->GetDefaultObject()->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad))
                {
                    Classes.Add(Class);
                }
            }
            if (!Classes.IsEmpty())
            {
                for (UClass* Class : Classes)
                {
                    ActivatedSubsystemClasses.Add(Class);
                }
                GetGeneratedSubsystemActivation(FGeneratedSubsystemActivation{})(Classes);
                bHasNewClasses = true;
            }
        }
    }
};

bool FGeneratedSubsystemRuntime::ShouldCreateSubsystem(const UObject* Object, UObject* Outer,
    UClass* NativeClass)
{
    return Object->GetClass() != NativeClass;
}

void FGeneratedSubsystemRuntime::RegisterSubsystemClass(UClass* Class)
{
    FGeneratedSubsystemRegistry& Registry = FGeneratedSubsystemRegistry::Get();
    Registry.SubsystemClasses.FindOrAdd(Class, TStrongObjectPtr<UClass>(Class));
    Registry.ActivateSubsystems();
}

void FGeneratedSubsystemRuntime::Startup()
{
    FGeneratedSubsystemRegistry& Registry = FGeneratedSubsystemRegistry::Get();
    Registry.ModuleLoadingRegistration = FCoreDelegates::OnAllModuleLoadingPhasesComplete.AddRaw(&Registry, &FGeneratedSubsystemRegistry::ActivateSubsystems);
#if WITH_EDITOR || UE_ENABLE_ASSET_READ_LOGGER
    Registry.PackageLoadingRegistration = FCoreUObjectDelegates::OnEndLoadPackage.AddLambda([](const FEndLoadPackageContext&)
    {
        FGeneratedSubsystemRegistry::Get().ActivateSubsystems();
    });
#endif
    Registry.FrameRegistration = FCoreDelegates::OnEndFrame.AddRaw(&Registry, &FGeneratedSubsystemRegistry::ActivateSubsystems);
    Registry.ActivateSubsystems();
}

void FGeneratedSubsystemRuntime::Shutdown()
{
    FGeneratedSubsystemRegistry& Registry = FGeneratedSubsystemRegistry::Get();
    FCoreDelegates::OnAllModuleLoadingPhasesComplete.Remove(Registry.ModuleLoadingRegistration);
#if WITH_EDITOR || UE_ENABLE_ASSET_READ_LOGGER
    FCoreUObjectDelegates::OnEndLoadPackage.Remove(Registry.PackageLoadingRegistration);
#endif
    FCoreDelegates::OnEndFrame.Remove(Registry.FrameRegistration);
    Registry.SubsystemClasses.Reset();
    Registry.ActivatedSubsystemClasses.Reset();
}
)SUBSYSTEM");
    Result.OutputHeaderString.ReplaceInline(TEXT("MODULE_EXPORT"), *(ModuleName.ToUpper() + TEXT("_API")));
    Result.OutputCppString.ReplaceInline(TEXT("MODULE_HEADER"), *ModuleName);
    for (const FString& TypeName : {FString(TEXT("FGeneratedSubsystemRuntime")), FString(TEXT("FGeneratedSubsystemRegistry")),
        FString(TEXT("FGeneratedSubsystemActivation")), FString(TEXT("TGeneratedSubsystemActivationAccess")), FString(TEXT("GetGeneratedSubsystemActivation"))})
    {
        FString ModuleTypeName = TypeName;
        ModuleTypeName.ReplaceInline(TEXT("Generated"), *ModuleName);
        Result.OutputHeaderString.ReplaceInline(*TypeName, *ModuleTypeName);
        Result.OutputCppString.ReplaceInline(*TypeName, *ModuleTypeName);
    }
    return Result;
}
