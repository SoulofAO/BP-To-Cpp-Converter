/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/CreateTranslators/CreateWidgetTranslatorObject.h"
#include "CreationNodeTranslation.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"
#include "RetainedBlueprintGeneration.h"
#include "EdGraphSchema_K2.h"

FString UCreateWidgetTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations, UNativizationV2Subsystem* Subsystem)
{
    FString Content;
    UK2Node_CreateWidget* Creation = CastChecked<UK2Node_CreateWidget>(Node);
    const FString Result = FCreationNodeTranslation::Result(Node, Creation->GetResultPin(), Subsystem, Content);
    const FString Class = FCreationNodeTranslation::Input(Node, Creation->GetClassPin(), MacroStack, Subsystem, TEXT("Create"), Content);
    const FString Context = FCreationNodeTranslation::Input(Node, Creation->GetWorldContextPin(), MacroStack, Subsystem, TEXT("Create"), Content, TEXT("this"));
    const FString Player = FCreationNodeTranslation::Input(Node, Creation->FindPin(TEXT("OwningPlayer")), MacroStack, Subsystem, TEXT("Create"), Content);
    Content += Result + TEXT(" = Cast<") + FRetainedBlueprintGeneration::GetClassName(CastChecked<UClass>(Creation->GetResultPin()->PinType.PinSubCategoryObject.Get())) +
        TEXT(">(UWidgetBlueprintLibrary::Create(") + Context + TEXT(", ") + Class + TEXT(", ") + Player + TEXT("));\n");
    Content += FCreationNodeTranslation::AssignExposedProperties(Node, Creation->GetClassToSpawn(), Result, MacroStack, Subsystem);
    return Content;
}

TSet<FString> UCreateWidgetTranslatorObject::GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem)
{
    TSet<FString> Includes = Super::GenerateCppIncludeInstructions(Node, Subsystem);
    Includes.Add(TEXT("#include \"Blueprint/WidgetBlueprintLibrary.h\""));
    Includes.Add(TEXT("#include \"Blueprint/UserWidget.h\""));
    return Includes;
}
