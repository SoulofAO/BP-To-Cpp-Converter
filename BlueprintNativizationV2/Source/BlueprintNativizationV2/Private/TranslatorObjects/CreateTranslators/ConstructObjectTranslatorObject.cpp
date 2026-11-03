/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/CreateTranslators/ConstructObjectTranslatorObject.h"
#include "CreationNodeTranslation.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"
#include "RetainedBlueprintGeneration.h"
#include "EdGraphSchema_K2.h"

FString UConstructObjectTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations, UNativizationV2Subsystem* Subsystem)
{
    FString Content;
    UK2Node_GenericCreateObject* Creation = CastChecked<UK2Node_GenericCreateObject>(Node);
    const FString Result = FCreationNodeTranslation::Result(Node, Creation->GetResultPin(), Subsystem, Content);
    const FString Class = FCreationNodeTranslation::Input(Node, Creation->GetClassPin(), MacroStack, Subsystem, TEXT("Create"), Content);
    const FString Outer = FCreationNodeTranslation::Input(Node, Creation->GetOuterPin(), MacroStack, Subsystem, TEXT("Create"), Content, TEXT("this"));
    Content += Result + TEXT(" = Cast<") + FRetainedBlueprintGeneration::GetClassName(CastChecked<UClass>(Creation->GetResultPin()->PinType.PinSubCategoryObject.Get())) +
        TEXT(">(UGameplayStatics::SpawnObject(") + Class + TEXT(", ") + Outer + TEXT("));\n");
    Content += FCreationNodeTranslation::AssignExposedProperties(Node, Creation->GetClassToSpawn(), Result, MacroStack, Subsystem);
    return Content;
}

TSet<FString> UConstructObjectTranslatorObject::GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem)
{
    TSet<FString> Includes = Super::GenerateCppIncludeInstructions(Node, Subsystem);
    Includes.Add(TEXT("#include \"Kismet/GameplayStatics.h\""));
    return Includes;
}
