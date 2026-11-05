/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright 2025 AO All Rights Reserved.
 */

#include "TranslatorObjects/SetMemberStructTranslatorObject.h"
#include "RetainedBlueprintGeneration.h"
#include "NativeApiCatalog.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSettings.h"
#include "BlueprintNativizationSubsystem.h"
#include "K2Node_VariableGet.h"

bool USetMemberStructTranslatorObject::CanApply(UK2Node* Node)
{
    return Node->IsA<UK2Node_SetFieldsInStruct>() || Node->GetClass() == UK2Node_StructMemberSet::StaticClass();
}

bool USetMemberStructTranslatorObject::HasSharedInput(UK2Node* Node, UNativizationV2Subsystem* Subsystem)
{
    if (!Node->IsA<UK2Node_SetFieldsInStruct>())
    {
        return true;
    }
    TArray<UK2Node*> MacroStack;
    UEdGraphPin* Source = UBlueprintNativizationLibrary::GetFirstNonKnotPin(
        Node->FindPinChecked(TEXT("StructRef")), 0, false, MacroStack, Subsystem);
    return Source && (Source->PinType.bIsReference || Source->GetOwningNode()->IsA<UK2Node_VariableGet>());
}

FString USetMemberStructTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName,
    TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations,
    UNativizationV2Subsystem* Subsystem)
{
    UK2Node_StructMemberSet* Setter = CastChecked<UK2Node_StructMemberSet>(Node);
    FString Content;
    FString Target;
    FString WriteBack;
    if (Node->IsA<UK2Node_SetFieldsInStruct>())
    {
        UEdGraphPin* Input = Setter->FindPinChecked(TEXT("StructRef"));
        UEdGraphPin* Output = Setter->FindPinChecked(TEXT("StructOut"));
        const FGenerateResultStruct Source = Subsystem->GenerateInputParameterCodeForNode(Node, Input, 0, MacroStack);
        Content = GenerateNewPreparations(Preparations, Source.Preparations);
        Preparations.Append(Source.Preparations);
        Target = Subsystem->GetNodeStateName(Node, TEXT("StructValue"));
        UK2Node_VariableGet* SourceVariable = nullptr;
        if (!Input->LinkedTo.IsEmpty())
        {
            SourceVariable = Cast<UK2Node_VariableGet>(Input->LinkedTo[0]->GetOwningNode());
        }
        if (SourceVariable && FRetainedBlueprintGeneration::IsRetained(SourceVariable->GetPropertyForVariable()->GetOwnerClass()))
        {
            if (Input->LinkedTo[0]->ParentPin)
            {
                FNativeApiCatalog::AddError(TEXT("Split structure writeback requires an explicit variable: ") + SourceVariable->GetPropertyForVariable()->GetPathName());
            }
            FString Receiver = TEXT("this");
            UEdGraphPin* ReceiverPin = SourceVariable->FindPin(TEXT("self"));
            if (ReceiverPin && (!ReceiverPin->LinkedTo.IsEmpty() || ReceiverPin->DefaultObject))
            {
                FGenerateResultStruct ReceiverValue = Subsystem->GenerateInputParameterCodeForNode(SourceVariable, ReceiverPin, 0, MacroStack);
                Content += GenerateNewPreparations(Preparations, ReceiverValue.Preparations);
                Preparations.Append(ReceiverValue.Preparations);
                Receiver = ReceiverValue.Code;
            }
            const FString Owner = Subsystem->GetNodeStateName(Node, TEXT("StructureOwner"));
            Content += TEXT("auto* ") + Owner + TEXT(" = ") + Receiver + TEXT(";\n");
            Content += TEXT("auto ") + Target + TEXT(" = ") + FRetainedBlueprintGeneration::ReadProperty(SourceVariable->GetPropertyForVariable(), Owner) + TEXT(";\n");
            WriteBack = FRetainedBlueprintGeneration::WriteProperty(SourceVariable->GetPropertyForVariable(), Owner, Target);
        }
        else if (HasSharedInput(Node, Subsystem))
        {
            Content += TEXT("auto&& ") + Target + TEXT(" = ") + Source.Code + TEXT(";\n");
        }
        else if (UBlueprintNativizationLibrary::CheckAnySubPinLinked(Output))
        {
            Target = UBlueprintNativizationLibrary::GetUniquePinName(Output, Subsystem->EntryNodes);
            Content += Target + TEXT(" = ") + Source.Code + TEXT(";\n");
        }
        else
        {
            Content += TEXT("auto ") + Target + TEXT(" = ") + Source.Code + TEXT(";\n");
        }
    }
    else
    {
        Target = UBlueprintNativizationLibrary::GetUniquePropertyName(Setter->GetPropertyForVariable());
    }
    TArray<FString> Assignments;
    FGenerationPreparations FieldPreparations;
    const TArray<UEdGraphPin*> Fields = UBlueprintNativizationLibrary::GetParentPins(
        UBlueprintNativizationLibrary::GetFilteredPins(Node, EPinOutputOrInputFilter::Input,
            EPinExcludeFilter::ExecPin, EPinIncludeOnlyFilter::None));
    for (UEdGraphPin* Field : Fields)
    {
        FProperty* Property = Setter->StructType->FindPropertyByName(Field->PinName);
        if (!Property)
        {
            continue;
        }
        const FGenerateResultStruct Value = Subsystem->GenerateInputParameterCodeForNode(Node, Field, 0, MacroStack);
        FieldPreparations.Append(Value.Preparations);
        if (UBlueprintNativizationSettingsLibrary::FindStructTranslator(Setter->StructType))
        {
            FStructTranslationRequest Request;
            Request.Structure = Setter->StructType;
            Request.Operation = EStructTranslationOperation::WriteMember;
            Request.MemberName = Property->GetFName();
            Request.SourceExpression = Target;
            Request.Arguments.Add({Property->GetFName(), Value.Code, {}});
            Assignments.Add(UBlueprintNativizationSettingsLibrary::TranslateStructure(Request) + TEXT(";\n"));
        }
        else
        {
            Assignments.Add(Target + TEXT(".") + UBlueprintNativizationLibrary::GetUniquePropertyName(Property) + TEXT(" = ") + Value.Code + TEXT(";\n"));
        }
    }
    Content += GenerateNewPreparations(Preparations, FieldPreparations);
    Preparations.Append(FieldPreparations);
    return Content + FString::Join(Assignments, TEXT("")) + WriteBack;
}

FGenerateResultStruct USetMemberStructTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node,
    UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem)
{
    if (HasSharedInput(Node, Subsystem))
    {
        return Subsystem->GenerateInputParameterCodeForNode(Node, Node->FindPinChecked(TEXT("StructRef")), 0, MacroStack);
    }
    return UBlueprintNativizationLibrary::GetUniquePinName(Node->FindPinChecked(TEXT("StructOut")), Subsystem->EntryNodes);
}

TSet<FString> USetMemberStructTranslatorObject::GenerateLocalVariables(UK2Node* Node, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem)
{
    if (HasSharedInput(Node, Subsystem))
    {
        return {};
    }
    return Super::GenerateLocalVariables(Node, MacroStack, Subsystem);
}

FString USetMemberStructTranslatorObject::GenerateGlobalVariables(UK2Node* Node, FString Prefix, UNativizationV2Subsystem* Subsystem)
{
    if (HasSharedInput(Node, Subsystem))
    {
        return FString();
    }
    return Super::GenerateGlobalVariables(Node, Prefix, Subsystem);
}
