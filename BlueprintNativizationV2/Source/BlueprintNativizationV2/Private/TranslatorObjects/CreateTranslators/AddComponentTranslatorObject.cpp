/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/CreateTranslators/AddComponentTranslatorObject.h"
#include "RetainedBlueprintGeneration.h"
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
    if (Component->FindPinChecked(TEXT("RelativeTransform"))->LinkedTo.IsEmpty() && SceneTemplate)
    {
        const FTransform TemplateTransform = SceneTemplate->GetRelativeTransform();
        FString DefaultTransform;
        TBaseStructure<FTransform>::Get()->ExportText(DefaultTransform, &TemplateTransform, nullptr, nullptr, PPF_None, nullptr);
        Transform.Code = UBlueprintNativizationLibrary::GenerateOneLineDefaultConstructor(TEXT("RelativeTransform"),
            Component->FindPinChecked(TEXT("RelativeTransform"))->PinType, DefaultTransform, nullptr, Subsystem->LeftAllAssetRefInBlueprint);
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
    if (!Subsystem->LeftAllAssetRefInBlueprint)
    {
        FString TemplateAssignments;
        UObject* ClassDefault = ComponentClass->GetDefaultObject();
        for (TFieldIterator<FProperty> PropertyIterator(ComponentClass); PropertyIterator; ++PropertyIterator)
        {
            FProperty* Property = *PropertyIterator;
            if (!Property->HasAnyPropertyFlags(CPF_Edit) || Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated) ||
                Property->Identical_InContainer(Template, ClassDefault))
            {
                continue;
            }
            if (SceneTemplate && Property->GetOwnerClass() == USceneComponent::StaticClass() &&
                (Property->GetFName() == TEXT("RelativeLocation") || Property->GetFName() == TEXT("RelativeRotation") ||
                    Property->GetFName() == TEXT("RelativeScale3D")))
            {
                continue;
            }
            void* ValueAddress = Property->ContainerPtrToValuePtr<void>(Template);
            if (FRetainedBlueprintGeneration::IsRetained(Property->GetOwnerClass()))
            {
                if (!UBlueprintNativizationLibrary::IsAvailibleOneLineDefaultConstructorForProperty(Property))
                {
                    FNativeApiCatalog::AddError(TEXT("Complex retained Blueprint component defaults require explicit initialization: ") + Property->GetPathName());
                    continue;
                }
                const FString Value = UBlueprintNativizationLibrary::GenerateOneLineDefaultConstructorForProperty(Property,
                    Subsystem->LeftAllAssetRefInBlueprint, ValueAddress);
                TemplateAssignments += FRetainedBlueprintGeneration::WriteProperty(Property, TemplateValue, Value);
                continue;
            }
            if (!UBlueprintNativizationLibrary::IsAvailibleOneLineDefaultConstructorForProperty(Property))
            {
                FString PropertyName;
                TemplateAssignments += UBlueprintNativizationLibrary::GenerateManyLinesDefaultConstructorForProperty(Property,
                    ValueAddress, Property->ContainerPtrToValuePtr<void>(ClassDefault), Subsystem->EntryNodes,
                    Node->GetBlueprint()->GeneratedClass, false, Subsystem->LeftAllAssetRefInBlueprint,
                    TemplateValue + TEXT("->"), PropertyName, TemplateValue);
                continue;
            }
            const FString Value = UBlueprintNativizationLibrary::GenerateOneLineDefaultConstructorForProperty(Property,
                Subsystem->LeftAllAssetRefInBlueprint, ValueAddress);
            FGetterAndSetterPropertyDescriptor Descriptor;
            if (UBlueprintNativizationSettingsLibrary::FindGetterAndSetterDescriptorDescriptorByProperty(Property, Descriptor) &&
                !Descriptor.SetterPropertyFunctionName.IsEmpty())
            {
                TemplateAssignments += TemplateValue + TEXT("->") + Descriptor.SetterPropertyFunctionName + TEXT("(") + Value + TEXT(");\n");
            }
            else
            {
                TemplateAssignments += TemplateValue + TEXT("->") + UBlueprintNativizationLibrary::GetUniquePropertyName(Property) +
                    TEXT(" = ") + Value + TEXT(";\n");
            }
        }
        const FString TemplateInitialization = TEXT("{\nUClass* ComponentClass = ") +
            FRetainedBlueprintGeneration::GetClassValue(ComponentClass) + TEXT(";\nif (ComponentClass)\n{\nauto* ") +
            TemplateValue + TEXT(" = NewObject<") + ClassName + TEXT(">(UObject::GetClass(), ComponentClass, FName(") +
            FRetainedBlueprintGeneration::Quote(Template->GetName()) + TEXT("), RF_ArchetypeObject | RF_Public);\n") + TemplateAssignments +
            TemplateMember + TEXT(" = ") + TemplateValue + TEXT(";\n}\n}\n");
        FRetainedBlueprintGeneration::SetComponentTemplateInitialization(Template, TemplateInitialization);
        }
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
