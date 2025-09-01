/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/CreateTranslators/SpawnActorTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"
#include "Kismet/GameplayStatics.h"

FString USpawnActorTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	if (!Node || !NativizationV2Subsystem)
	{
		return FString();
	}
	UK2Node_SpawnActorFromClass* SpawnActorNode = Cast<UK2Node_SpawnActorFromClass>(Node);
	if (!SpawnActorNode)
	{
		return FString();
	}

	FString Content;

	UEdGraphPin* ClassPin = UBlueprintNativizationLibrary::GetPinByName(Node->Pins, TEXT("Class"));
	UEdGraphPin* TransformPin = UBlueprintNativizationLibrary::GetPinByName(Node->Pins, TEXT("SpawnTransform"));
	UEdGraphPin* OwnerPin = UBlueprintNativizationLibrary::GetPinByName(Node->Pins, TEXT("Owner"));
	UEdGraphPin* InstigatorPin = UBlueprintNativizationLibrary::GetPinByName(Node->Pins, TEXT("Instigator"));
	UEdGraphPin* CollisionHandlingPin = UBlueprintNativizationLibrary::GetPinByName(Node->Pins, TEXT("CollisionHandlingOverride"));
	UEdGraphPin* ScaleMethodPin = UBlueprintNativizationLibrary::GetPinByName(Node->Pins, TEXT("TransformScaleMethod"));
	UEdGraphPin* ResultPin = UBlueprintNativizationLibrary::GetPinByName(Node->Pins, TEXT("ReturnValue"));

	FString BaseClassName = UBlueprintNativizationLibrary::GetUniqueFieldName(SpawnActorNode->GetClassToSpawn());
	FString ClassArg = ClassPin ? NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, ClassPin, 0, MacroStack) : TEXT("nullptr");
	FString TransformArg = TransformPin ? NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, TransformPin, 0, MacroStack) : TEXT("FTransform()") ;
	FString OwnerArg = OwnerPin ? NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, OwnerPin, 0, MacroStack) : TEXT("nullptr");
	FString InstigatorArg = InstigatorPin ? NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, InstigatorPin, 0, MacroStack) : TEXT("nullptr");
	FString CollisionHandlingArg = CollisionHandlingPin ? NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, CollisionHandlingPin, 0, MacroStack) : TEXT("ESpawnActorCollisionHandlingMethod::Undefined");
	FString ScaleMethodArg = ScaleMethodPin ? NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, ScaleMethodPin, 0, MacroStack) : TEXT("ESpawnActorScaleMethod::MultiplyWithRoot");
	FString ResultVar = "";
	FString DeclarationVar = "";

	if (ResultPin->LinkedTo.Num() > 0)
	{
		ResultVar = UBlueprintNativizationLibrary::GetUniquePinName(ResultPin, NativizationV2Subsystem->EntryNodes);
	}
	else
	{
		ResultVar = UBlueprintNativizationLibrary::GetLambdaUniqueVariableNameByNode(TEXT("SpawnedActor"), SpawnActorNode, NativizationV2Subsystem->EntryNodes);
		DeclarationVar = "AActor* ";
	}

	bool bHasAnyExposedProperty = false;
	TArray<FProperty*> ExposeOnSpawnProperties = UBlueprintNativizationLibrary::GetExposeOnSpawnProperties(SpawnActorNode->GetClassToSpawn());
	for (FProperty* Property : ExposeOnSpawnProperties)
	{
		UEdGraphPin* FoundPin = SpawnActorNode->FindPin(Property->GetName());
		if (FoundPin && FoundPin != InstigatorPin)
		{
			bHasAnyExposedProperty = true;
			break;
		}
	}

	if (!bHasAnyExposedProperty)
	{
		Content += FString::Format(TEXT("{0}{1} = GetWorld()->SpawnActor<{2}>({3}, {4}, {5}, {6}, {7});\n"),
			{
				*DeclarationVar,
				*ResultVar,
				BaseClassName,
				*ClassArg,
				*TransformArg,
				*OwnerArg,
				*InstigatorArg,
				*CollisionHandlingArg
			});
	}
	else
	{
		Content += FString::Format(TEXT("{0}{1} = GetWorld()->SpawnActorDeferred<{2}>({3}, {4}, {5}, {6}, {7}, {8});\n"),
			{
				*DeclarationVar,
				*ResultVar,
				BaseClassName,
				*ClassArg,
				*TransformArg,
				*OwnerArg,
				*InstigatorArg,
				*CollisionHandlingArg,
				*ScaleMethodArg
			});

		Content += FString::Format(TEXT("if({0})\n{\n"), { ResultVar });
		for (FProperty* Property : ExposeOnSpawnProperties)
		{
			UEdGraphPin* FoundPin = SpawnActorNode->FindPin(Property->GetName());
			if (FoundPin && FoundPin != InstigatorPin)
			{
				Content += FString::Format(TEXT("{0}->{1} = {2};\n"),
					{
						ResultVar,
						UBlueprintNativizationLibrary::GetUniquePropertyName(Property, NativizationV2Subsystem->EntryNodes),
						NativizationV2Subsystem->GenerateInputParameterCodeForNode(SpawnActorNode, FoundPin, 0, MacroStack)
					});
			}
		}
		Content += FString::Format(TEXT("{0}->FinishSpawning({1}, false, nullptr, {2});\n"), { ResultVar, TransformArg, ScaleMethodArg });
		Content += "}\n";
	}
	return Content;
}
