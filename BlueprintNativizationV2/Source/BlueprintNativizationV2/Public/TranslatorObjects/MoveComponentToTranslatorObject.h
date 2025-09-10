/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "BlueprintNativizationData.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_CallFunction.h"
#include "MoveComponentToTranslatorObject.generated.h"

/**
 * 
 */

UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UMoveComponentToTranslatorObject : public UTranslatorBPToCppObject
{
    GENERATED_BODY()

public:
    UMoveComponentToTranslatorObject()
    {
        ClassNodeForCheck = {UK2Node_CallFunction::StaticClass()};
        NodeNamesForCheck = { "MoveComponentTo" };
    }

    virtual FString GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, TSet<FString>& Preparations, UNativizationV2Subsystem* NativizationV2Subsystem) override;

	virtual void GenerateNewFunction(UK2Node* InputNode, TArray<UK2Node*> Path, TArray<FGenerateFunctionStruct>& EntryNodes, UNativizationV2Subsystem* NativizationV2Subsystem) override;

    virtual bool CanContinueCodeGeneration(UK2Node* InputNode, FString EntryPinName) override
    {
        return false;
    }

    virtual bool CanCreateCircleWithPin(UK2Node* Node, FString EnterPinName) override
    {
        return true;
    }
};




DECLARE_DELEGATE_OneParam(FOnMoveComponentFinished, USceneComponent*);

USTRUCT()
struct FMoveComponentRequest
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<USceneComponent> Component;

	FVector StartLocation;
	FRotator StartRotation;

	FVector TargetLocation;
	FRotator TargetRotation;

	float Duration = 0.f;
	float Elapsed = 0.f;

	bool bEaseIn = false;
	bool bEaseOut = false;
	bool bForceShortestRotationPath = true;

	FOnMoveComponentFinished OnFinished;

	FMoveComponentRequest() {}

	FMoveComponentRequest(USceneComponent* InComponent, FVector InTargetLocation, FRotator InTargetRotation, float InDuration, bool bInEaseOut, bool bInEaseIn, bool bInForceShortestRotationPath, FOnMoveComponentFinished InOnFinished = FOnMoveComponentFinished())
	{
		Component = InComponent;
		StartLocation = InComponent->GetRelativeLocation();
		StartRotation = InComponent->GetRelativeRotation();

		TargetLocation = InTargetLocation;
		TargetRotation = InTargetRotation;

		Duration = InDuration;
		Elapsed = 0.f;

		bEaseIn = bInEaseIn;
		bEaseOut = bInEaseOut;
		bForceShortestRotationPath = bInForceShortestRotationPath;
		OnFinished = InOnFinished;
	}
};

UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UMoveComponentSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void EnqueueMove(USceneComponent* Component, FVector TargetLocation, FRotator TargetRotation, float Duration, bool bEaseOut, bool bEaseIn, bool bForceShortestPath);
	void EnqueueMove(USceneComponent* Component, FVector TargetLocation, FRotator TargetRotation, float Duration, bool bEaseOut, bool bEaseIn, bool bForceShortestPath, FOnMoveComponentFinished OnFinished);
	void Stop(USceneComponent* Component);
	void Return(USceneComponent* Component);

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UMoveComponentSubsystem, STATGROUP_Tickables); }
	virtual bool IsTickable() const override { return true; }

private:
	TArray<FMoveComponentRequest> ActiveRequests;
};