/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright 2025 AO All Rights Reserved.
 */

#include "TranslatorObjects/CreateTranslators/AddComponentTranslatorObject.h"
#include "RetainedBlueprintGeneration.h"
#include "CreationNodeTranslation.h"
#include "NativeApiCatalog.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSettings.h"
#include "Components/SceneComponent.h"
#include "EdGraphSchema_K2.h"

FString UAddComponentTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName,
    TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations,
    UNativizationV2Subsystem* Subsystem)
{
    if (UK2Node_AddComponentByClass* Creation = Cast<UK2Node_AddComponentByClass>(Node))
    {
        FString Content;
        const FString Result = FCreationNodeTranslation::Result(Node, Creation->GetResultPin(), Subsystem, Content);
        const FString Class = FCreationNodeTranslation::Input(Node, Creation->GetClassPin(), MacroStack, Subsystem, TEXT("Begin"), Content);
        const FString Owner = FCreationNodeTranslation::Input(Node, Creation->FindPin(UEdGraphSchema_K2::PN_Self), MacroStack, Subsystem, TEXT("Begin"), Content, TEXT("this"));
        const FString Manual = FCreationNodeTranslation::Input(Node, Creation->FindPin(TEXT("bManualAttachment")), MacroStack, Subsystem, TEXT("Begin"), Content, TEXT("false"));
        const FString Transform = FCreationNodeTranslation::Input(Node, Creation->FindPin(TEXT("RelativeTransform")), MacroStack, Subsystem, TEXT("Begin"), Content, TEXT("FTransform::Identity"));
        const FString Assignments = FCreationNodeTranslation::AssignExposedProperties(Node, Creation->GetClassToSpawn(), Result, MacroStack, Subsystem);
        FString Deferred = TEXT("false");
        if (!Assignments.IsEmpty())
        {
            Deferred = TEXT("true");
        }
        Content += TEXT("if (IsValid(") + Owner + TEXT("))\n{\n") + Result + TEXT(" = Cast<") +
            FRetainedBlueprintGeneration::GetClassName(CastChecked<UClass>(Creation->GetResultPin()->PinType.PinSubCategoryObject.Get())) +
            TEXT(">(") + Owner + TEXT("->AddComponentByClass(") + Class + TEXT(", ") + Manual + TEXT(", ") + Transform + TEXT(", ") + Deferred + TEXT("));\n");
        if (!Assignments.IsEmpty())
        {
            Content += Assignments;
            const FString FinishManual = FCreationNodeTranslation::Input(Node, Creation->FindPin(TEXT("bManualAttachment")), MacroStack, Subsystem, TEXT("Finish"), Content, TEXT("false"));
            const FString FinishTransform = FCreationNodeTranslation::Input(Node, Creation->FindPin(TEXT("RelativeTransform")), MacroStack, Subsystem, TEXT("Finish"), Content, TEXT("FTransform::Identity"));
            Content += Owner + TEXT("->FinishAddComponent(") + Result + TEXT(", ") + FinishManual + TEXT(", ") + FinishTransform + TEXT(");\n");
        }
        Content += TEXT("}\n");
        return Content;
    }
    UK2Node_AddComponent* Component = CastChecked<UK2Node_AddComponent>(Node);
    UActorComponent* Template = Component->GetTemplateFromNode();
    UClass* ComponentClass = Template->GetClass();
    const FString ClassName = FRetainedBlueprintGeneration::GetClassName(ComponentClass);
    const FString TemplateMember = FRetainedBlueprintGeneration::RegisterComponentTemplate(Template);
    const FString TemplateValue = TemplateMember + TEXT("Value");
    UEdGraphPin* Output = Component->GetReturnValuePin();
    FString ResultName = UBlueprintNativizationLibrary::GetUniquePinName(Output, Subsystem->EntryNodes);
    FString Content;
    if (!UBlueprintNativizationLibrary::CheckAnySubPinLinked(Output))
    {
        ResultName = Subsystem->GetNodeStateName(Node, TEXT("AddedComponent"));
        Content += ClassName + TEXT("* ") + ResultName + TEXT(" = nullptr;\n");
    }
    const FGenerateResultStruct Owner = Subsystem->GenerateInputParameterCodeForNode(Node,
        Component->FindPinChecked(UEdGraphSchema_K2::PN_Self), 0, MacroStack);
    const FGenerateResultStruct ManualAttachment = Subsystem->GenerateInputParameterCodeForNode(Node,
        Component->FindPinChecked(TEXT("bManualAttachment")), 0, MacroStack);
    FGenerateResultStruct Transform;
    USceneComponent* SceneTemplate = Cast<USceneComponent>(Template);
    if (!UBlueprintNativizationLibrary::CheckAnySubPinLinked(Component->FindPinChecked(TEXT("RelativeTransform"))) && SceneTemplate)
    {
        const FTransform TemplateTransform = SceneTemplate->GetRelativeTransform();
        FString DefaultTransform;
        TBaseStructure<FTransform>::Get()->ExportText(DefaultTransform, &TemplateTransform, nullptr, nullptr, PPF_None, nullptr);
        Transform.Code = UBlueprintNativizationLibrary::GenerateOneLineDefaultConstructor(TEXT("RelativeTransform"),
            Component->FindPinChecked(TEXT("RelativeTransform"))->PinType, DefaultTransform, nullptr);
    }
    else
    {
        Transform = Subsystem->GenerateInputParameterCodeForNode(Node, Component->FindPinChecked(TEXT("RelativeTransform")), 0, MacroStack);
    }
    const FGenerateResultStruct Deferred = Subsystem->GenerateInputParameterCodeForNode(Node,
        Component->FindPinChecked(TEXT("bDeferredFinish")), 0, MacroStack);
    FGenerationPreparations InputPreparations = Owner.Preparations;
    InputPreparations.Append(ManualAttachment.Preparations);
    InputPreparations.Append(Transform.Preparations);
    InputPreparations.Append(Deferred.Preparations);
    Content += GenerateNewPreparations(Preparations, InputPreparations);
    Preparations.Append(InputPreparations);
    FString OwnerCode = Owner.Code;
    if (OwnerCode.IsEmpty())
    {
        OwnerCode = TEXT("this");
    }
    FString OwnerName = OwnerCode;
    const bool bCheckOwner = OwnerCode != TEXT("this");
    if (bCheckOwner)
    {
        OwnerName = Subsystem->GetNodeStateName(Node, TEXT("ComponentOwner"));
        Content += TEXT("const auto ") + OwnerName + TEXT(" = ") + OwnerCode + TEXT(";\n");
        Content += TEXT("if (IsValid(") + OwnerName + TEXT("))\n{\n");
    }
    Content += ResultName + TEXT(" = Cast<") + ClassName + TEXT(">(") + OwnerName + TEXT("->CreateComponentFromTemplate(") +
        TEXT("Cast<UActorComponent>(") + TemplateMember + TEXT(".Get())));\n");

    Content += FCreationNodeTranslation::AssignExposedProperties(Node, ComponentClass, ResultName, MacroStack, Subsystem);
    const FString Finish = OwnerName + TEXT("->FinishAddComponent(") + ResultName + TEXT(", ") +
        ManualAttachment.Code + TEXT(", ") + Transform.Code + TEXT(");\n");
    if (Deferred.Code == TEXT("false"))
    {
        Content += Finish;
    }
    else if (Deferred.Code != TEXT("true"))
    {
        Content += TEXT("if (!(") + Deferred.Code + TEXT("))\n{\n") + Finish + TEXT("}\n");
    }
    if (bCheckOwner)
    {
        Content += TEXT("}\n");
    }
    return Content;
}
