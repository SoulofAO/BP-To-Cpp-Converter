/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright 2026 AO All Rights Reserved.
 */

#include "GeneratedComponentInputGeneration.h"

FNativizationCode FGeneratedComponentInputGeneration::Generate(const FString& ModuleName)
{
    FNativizationCode Result;
    Result.OutputHeaderString = TEXT(R"INPUT(
class UActorComponent;
class UInputComponent;
class AActor;

class MODULE_EXPORT FGeneratedComponentInputRuntime
{
public:
    static void Register(UActorComponent* Component, UClass* Layer, TFunction<void(UInputComponent*)> Bind);
    static void BindForActor(AActor* Actor);
    static void Shutdown();
};
)INPUT");
    Result.OutputCppString = TEXT(R"INPUT(
#include "Components/ActorComponent.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

struct FGeneratedComponentInputRegistration
{
    TWeakObjectPtr<UActorComponent> Component;
    TWeakObjectPtr<UClass> Layer;
    TArray<TWeakObjectPtr<UInputComponent>> BoundInputs;
    TFunction<void(UInputComponent*)> Bind;
};

static TArray<FGeneratedComponentInputRegistration> GeneratedComponentInputs;
static FDelegateHandle GeneratedComponentInputTick;

static void BindGeneratedComponentInput(FGeneratedComponentInputRegistration& Registration)
{
    UActorComponent* Component = Registration.Component.Get();
    if (!Component || !Component->IsRegistered())
    {
        return;
    }
    AActor* Owner = Component->GetOwner();
    UWorld* World = Component->GetWorld();
    if (!Owner || !World || !World->IsGameWorld() || !Owner->InputComponent)
    {
        return;
    }
    UInputComponent* InputComponent = Owner->InputComponent;
    Registration.BoundInputs.RemoveAll([](const TWeakObjectPtr<UInputComponent>& Input)
    {
        return !Input.IsValid();
    });
    if (!Registration.BoundInputs.Contains(InputComponent))
    {
        Registration.BoundInputs.Add(InputComponent);
        Registration.Bind(InputComponent);
    }
}

void FGeneratedComponentInputRuntime::Register(UActorComponent* Component, UClass* Layer, TFunction<void(UInputComponent*)> Bind)
{
    if (Component->IsTemplate())
    {
        return;
    }
    if (!GeneratedComponentInputs.ContainsByPredicate([Component, Layer](const FGeneratedComponentInputRegistration& Registration)
    {
        return Registration.Component == Component && Registration.Layer == Layer;
    }))
    {
        FGeneratedComponentInputRegistration& Registration = GeneratedComponentInputs.AddDefaulted_GetRef();
        Registration.Component = Component;
        Registration.Layer = Layer;
        Registration.Bind = MoveTemp(Bind);
    }
    if (!GeneratedComponentInputTick.IsValid())
    {
        GeneratedComponentInputTick = FWorldDelegates::OnWorldPreActorTick.AddLambda([](UWorld* World, ELevelTick TickType, float DeltaSeconds)
        {
            GeneratedComponentInputs.RemoveAll([](const FGeneratedComponentInputRegistration& Registration)
            {
                return !Registration.Component.IsValid() || !Registration.Layer.IsValid();
            });
            for (FGeneratedComponentInputRegistration& Registration : GeneratedComponentInputs)
            {
                if (Registration.Component->GetWorld() == World)
                {
                    BindGeneratedComponentInput(Registration);
                }
            }
        });
    }
}

void FGeneratedComponentInputRuntime::BindForActor(AActor* Actor)
{
    for (FGeneratedComponentInputRegistration& Registration : GeneratedComponentInputs)
    {
        if (Registration.Component.IsValid() && Registration.Component->GetOwner() == Actor)
        {
            BindGeneratedComponentInput(Registration);
        }
    }
}

void FGeneratedComponentInputRuntime::Shutdown()
{
    FWorldDelegates::OnWorldPreActorTick.Remove(GeneratedComponentInputTick);
    GeneratedComponentInputTick.Reset();
    GeneratedComponentInputs.Reset();
}
)INPUT");
    Result.OutputHeaderString.ReplaceInline(TEXT("MODULE_EXPORT"), *(ModuleName.ToUpper() + TEXT("_API")));
    const TArray<FString> Names = {
        TEXT("FGeneratedComponentInputRuntime"), TEXT("FGeneratedComponentInputRegistration"),
        TEXT("GeneratedComponentInputs"), TEXT("GeneratedComponentInputTick"), TEXT("BindGeneratedComponentInput")
    };
    for (const FString& Name : Names)
    {
        FString Replacement = Name;
        Replacement.ReplaceInline(TEXT("Generated"), *ModuleName);
        Result.OutputHeaderString.ReplaceInline(*Name, *Replacement);
        Result.OutputCppString.ReplaceInline(*Name, *Replacement);
    }
    return Result;
}
