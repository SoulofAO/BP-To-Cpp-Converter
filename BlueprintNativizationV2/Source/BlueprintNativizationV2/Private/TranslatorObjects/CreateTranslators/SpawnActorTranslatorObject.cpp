/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/CreateTranslators/SpawnActorTranslatorObject.h"
#include "CreationNodeTranslation.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"
#include "RetainedBlueprintGeneration.h"
#include "EdGraphSchema_K2.h"

FString USpawnActorTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations, UNativizationV2Subsystem* Subsystem)
{
    FString Content;
    UK2Node_SpawnActorFromClass* Creation = CastChecked<UK2Node_SpawnActorFromClass>(Node);
    const FString Result = FCreationNodeTranslation::Result(Node, Creation->GetResultPin(), Subsystem, Content);
    const FString Class = FCreationNodeTranslation::Input(Node, Creation->GetClassPin(), MacroStack, Subsystem, TEXT("Begin"), Content);
    const FString Context = FCreationNodeTranslation::Input(Node, Creation->GetWorldContextPin(), MacroStack, Subsystem, TEXT("Begin"), Content, TEXT("this"));
    const FString Transform = FCreationNodeTranslation::Input(Node, Creation->FindPin(TEXT("SpawnTransform")), MacroStack, Subsystem, TEXT("Begin"), Content, TEXT("FTransform::Identity"));
    const FString Collision = FCreationNodeTranslation::Input(Node, Creation->FindPin(TEXT("CollisionHandlingOverride")), MacroStack, Subsystem, TEXT("Begin"), Content, TEXT("ESpawnActorCollisionHandlingMethod::Undefined"));
    const FString Owner = FCreationNodeTranslation::Input(Node, Creation->FindPin(TEXT("Owner")), MacroStack, Subsystem, TEXT("Begin"), Content);
    const FString Scale = FCreationNodeTranslation::Input(Node, Creation->FindPin(TEXT("TransformScaleMethod")), MacroStack, Subsystem, TEXT("Begin"), Content, TEXT("ESpawnActorScaleMethod::SelectDefaultAtRuntime"));
    Content += Result + TEXT(" = Cast<") + FRetainedBlueprintGeneration::GetClassName(CastChecked<UClass>(Creation->GetResultPin()->PinType.PinSubCategoryObject.Get())) +
        TEXT(">(UGameplayStatics::BeginDeferredActorSpawnFromClass(") + Context + TEXT(", ") + Class + TEXT(", ") + Transform + TEXT(", ") + Collision + TEXT(", ") + Owner + TEXT(", ") + Scale + TEXT("));\n");
    Content += FCreationNodeTranslation::AssignExposedProperties(Node, Creation->GetClassToSpawn(), Result, MacroStack, Subsystem);
    const FString FinishTransform = FCreationNodeTranslation::Input(Node, Creation->FindPin(TEXT("SpawnTransform")), MacroStack, Subsystem, TEXT("Finish"), Content, TEXT("FTransform::Identity"));
    const FString FinishScale = FCreationNodeTranslation::Input(Node, Creation->FindPin(TEXT("TransformScaleMethod")), MacroStack, Subsystem, TEXT("Finish"), Content, TEXT("ESpawnActorScaleMethod::SelectDefaultAtRuntime"));
    Content += TEXT("UGameplayStatics::FinishSpawningActor(") + Result + TEXT(", ") + FinishTransform + TEXT(", ") + FinishScale + TEXT(");\n");
    return Content;
}

TSet<FString> USpawnActorTranslatorObject::GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem)
{
    TSet<FString> Includes = Super::GenerateCppIncludeInstructions(Node, Subsystem);
    Includes.Add(TEXT("#include \"Kismet/GameplayStatics.h\""));
    return Includes;
}
