
/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/MoveComponentToTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"


FString UMoveComponentToTranslatorObject::GenerateCodeFromNode(UK2Node* Node,
	FString EntryPinName,
	TArray<FVisitedNodeStack> VisitedNodes,
	TArray<UK2Node*> MacroStack, TSet<FString>& Preparations, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	FString Content;

	if (EntryPinName == "Move")
	{
		TSet<FString>  NewPreparations;
		FGenerateResultStruct TargetComponent = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, UBlueprintNativizationLibrary::GetPinByName(Node->Pins, "Component"), 0, MacroStack);
		FGenerateResultStruct TargetRelativeLocation = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, UBlueprintNativizationLibrary::GetPinByName(Node->Pins, "TargetRelativeLocation"), 0, MacroStack);
		FGenerateResultStruct TargetRelativeRotation = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, UBlueprintNativizationLibrary::GetPinByName(Node->Pins, "TargetRelativeRotation"), 0, MacroStack);
		FGenerateResultStruct bEaseOut = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, UBlueprintNativizationLibrary::GetPinByName(Node->Pins, "bEaseOut"), 0, MacroStack);
		FGenerateResultStruct bEaseIn = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, UBlueprintNativizationLibrary::GetPinByName(Node->Pins, "bEaseIn"), 0, MacroStack);
		FGenerateResultStruct OverTime = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, UBlueprintNativizationLibrary::GetPinByName(Node->Pins, "OverTime"), 0, MacroStack);
		FGenerateResultStruct ForceShortestRotationPath = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, UBlueprintNativizationLibrary::GetPinByName(Node->Pins, "bForceShortestRotationPath"), 0, MacroStack);


		NewPreparations.Append(TargetComponent.Preparations);
		NewPreparations.Append(TargetRelativeLocation.Preparations);
		NewPreparations.Append(TargetRelativeRotation.Preparations);
		NewPreparations.Append(bEaseOut.Preparations);
		NewPreparations.Append(bEaseIn.Preparations);
		NewPreparations.Append(OverTime.Preparations);
		NewPreparations.Append(ForceShortestRotationPath.Preparations);

		Content += GenerateNewPreparations(Preparations, NewPreparations);
		Preparations.Append(NewPreparations);
		Content += FString::Format(
			TEXT("GetWorld()->GetSubsystem<UMoveComponentSubsystem>()->EnqueueMove({0}, {1}, {2}, {3}, {4}, {5}, {6});"),
			{ TargetComponent.Code, TargetRelativeLocation.Code, TargetRelativeRotation.Code, OverTime.Code, bEaseOut.Code, bEaseIn.Code, ForceShortestRotationPath.Code});

	}
	else if (EntryPinName == "Stop")
	{
		FGenerateResultStruct TargetComponent = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, UBlueprintNativizationLibrary::GetPinByName(Node->Pins, "Component"), 0, MacroStack);

		Content += GenerateNewPreparations(Preparations, TargetComponent.Preparations);
		Content += FString::Format(
			TEXT("GetWorld()->GetSubsystem<UMoveComponentSubsystem>()->Stop({0});"),
			{ TargetComponent.Code }
		);

	}
	else if (EntryPinName == "Return")
	{
		FGenerateResultStruct TargetComponent = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, UBlueprintNativizationLibrary::GetPinByName(Node->Pins, "Component"), 0, MacroStack);

		Content += GenerateNewPreparations(Preparations, TargetComponent.Preparations);
		Content += FString::Format(
			TEXT("GetWorld()->GetSubsystem<UMoveComponentSubsystem>()->Return({0});"),
			{ TargetComponent.Code }
		);
	}

	return Content;
}

void UMoveComponentToTranslatorObject::GenerateNewFunction(UK2Node* InputNode, TArray<UK2Node*> Path, TArray<FGenerateFunctionStruct>& EntryNodes, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	UEdGraphPin* Pin = UBlueprintNativizationLibrary::GetFilteredPins(
		InputNode,
		EPinOutputOrInputFilter::Ouput,
		EPinExcludeFilter::None,
		EPinIncludeOnlyFilter::ExecPin)[0];

	TArray<UK2Node*> MacroStack;
	UEdGraphPin* NonKnotPin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Pin,0, true, MacroStack, NativizationV2Subsystem);
	if (!NonKnotPin)
	{
		return;
	}

	UK2Node* TargetNode = Cast<UK2Node>(NonKnotPin->GetOwningNode());
	if (!TargetNode)
	{
		return;
	}

	FGenerateFunctionStruct OutGenerateFunctionStruct;
	if (!UBlueprintNativizationDataLibrary::FindGenerateFunctionStructByNode(EntryNodes, TargetNode, OutGenerateFunctionStruct))
	{
		FGenerateFunctionStruct NewStruct;
		NewStruct.Node = TargetNode;
		NewStruct.Name = *UBlueprintNativizationLibrary::GetUniqueEntryNodeName(TargetNode, EntryNodes, TEXT("MoveComponent"));
		EntryNodes.Add(NewStruct);
	}
}


void UMoveComponentSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UMoveComponentSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

void UMoveComponentSubsystem::EnqueueMove(USceneComponent* Component, FVector TargetLocation, FRotator TargetRotation, float Duration, bool bEaseOut, bool bEaseIn, bool bForceShortestPath)
{
	EnqueueMove(Component, TargetLocation, TargetRotation, Duration, bEaseOut, bEaseIn, bForceShortestPath, FOnMoveComponentFinished());
}

void UMoveComponentSubsystem::EnqueueMove(USceneComponent* Component, FVector TargetLocation, FRotator TargetRotation, float Duration, bool bEaseOut, bool bEaseIn, bool bForceShortestPath, FOnMoveComponentFinished OnFinished)
{
	if (!Component)
		return;

	FMoveComponentRequest Request(Component, TargetLocation, TargetRotation, Duration, bEaseOut, bEaseIn, bForceShortestPath, OnFinished);
	ActiveRequests.Add(Request);
}

void UMoveComponentSubsystem::Stop(USceneComponent* Component)
{
	if (!Component)
		return;

	for (int32 i = ActiveRequests.Num() - 1; i >= 0; --i)
	{
		if (ActiveRequests[i].Component == Component)
		{
			ActiveRequests.RemoveAtSwap(i);
			break; 
		}
	}
}

void UMoveComponentSubsystem::Return(USceneComponent* Component)
{
	if (!Component)
		return;

	for (int32 i = 0; i < ActiveRequests.Num(); ++i)
	{
		auto& Req = ActiveRequests[i];
		if (Req.Component == Component)
		{
			Req.StartLocation = Component->GetRelativeLocation();
			Req.StartRotation = Component->GetRelativeRotation();

			Swap(Req.StartLocation, Req.TargetLocation);
			Swap(Req.StartRotation, Req.TargetRotation);

			Req.Elapsed = 0.f;

			break;
		}
	}
}


void UMoveComponentSubsystem::Tick(float DeltaTime)
{
	for (int32 i = ActiveRequests.Num() - 1; i >= 0; --i)
	{
		auto& Req = ActiveRequests[i];
		if (!Req.Component.IsValid())
		{
			ActiveRequests.RemoveAtSwap(i);
			continue;
		}

		Req.Elapsed += DeltaTime;
		float Alpha = FMath::Clamp(Req.Elapsed / Req.Duration, 0.f, 1.f);

		if (Req.bEaseIn || Req.bEaseOut)
		{
			Alpha = FMath::InterpEaseInOut(0.f, 1.f, Alpha, 2.f);
		}

		FVector NewLoc = FMath::Lerp(Req.StartLocation, Req.TargetLocation, Alpha);
		FRotator NewRot = FMath::Lerp(Req.StartRotation, Req.TargetRotation, Alpha);

		Req.Component->SetRelativeLocationAndRotation(NewLoc, NewRot);

		if (Alpha >= 1.f)
		{
			if (Req.OnFinished.IsBound())
			{
				Req.OnFinished.Execute(Req.Component.Get());
			}
			ActiveRequests.RemoveAtSwap(i);
		}
	}
}

