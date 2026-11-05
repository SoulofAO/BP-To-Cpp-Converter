/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright 2025 AO All Rights Reserved.
 */

#include "TranslatorObjects/VariableSetTranslatorObject.h"
#include "RetainedBlueprintGeneration.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSettings.h"
#include "BlueprintNativizationSubsystem.h"
#include "EdGraphSchema_K2.h"

bool UVariableSetTranslatorObject::CanApply(UK2Node* Node)
{
    UK2Node_VariableSet* Setter = Cast<UK2Node_VariableSet>(Node);
    if (!Setter)
    {
        return false;
    }
    FProperty* Property = UBlueprintNativizationLibrary::ResolveVariableProperty(Setter);
    return true;
}

FString UVariableSetTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName,
    TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations,
    UNativizationV2Subsystem* Subsystem)
{
    UK2Node_VariableSet* Setter = CastChecked<UK2Node_VariableSet>(Node);
    FProperty* Property = UBlueprintNativizationLibrary::ResolveVariableProperty(Setter);
    FGenerateResultStruct Target(UBlueprintNativizationLibrary::GetUniquePropertyName(Property));
    UEdGraphPin* TargetPin = Setter->FindPin(UEdGraphSchema_K2::PN_Self);
    bool bExternalTarget = TargetPin && (TargetPin->LinkedTo.Num() > 0 || TargetPin->DefaultObject);
    FString ReceiverName;
    if (bExternalTarget)
    {
        FGenerateResultStruct Receiver = Subsystem->GenerateInputParameterCodeForNode(Setter, TargetPin, 0, MacroStack);
        ReceiverName = Subsystem->GetNodeStateName(Setter, TEXT("VariableTarget"));
        Target.Preparations = Receiver.Preparations;
        Target.Preparations.Add(TEXT("const auto ") + ReceiverName + TEXT(" = ") + Receiver.Code + TEXT(";\n"));
        Target.Code = ReceiverName + TEXT("->") + Target.Code;
    }
    FGenerateResultStruct Value = Subsystem->GenerateInputParameterCodeForNode(Setter,
        Setter->FindPinChecked(Setter->GetVarName(), EGPD_Input), 0, MacroStack);
    UEdGraphPin* ValuePin = Setter->FindPinChecked(Setter->GetVarName(), EGPD_Input);
    UEdGraphPin* SourcePin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(ValuePin, 0, true, MacroStack, Subsystem);
    Value.Code = UBlueprintNativizationLibrary::ConvertArrayValue(SourcePin, ValuePin->PinType, Value.Code);
    FString Content = GenerateNewPreparations(Preparations, Target.Preparations);
    Preparations.Append(Target.Preparations);
    Content += GenerateNewPreparations(Preparations, Value.Preparations);
    Preparations.Append(Value.Preparations);
    FString Assignment = Target.Code + TEXT(" = ") + Value.Code + TEXT(";\n");
    const bool bRetainedOwner = FRetainedBlueprintGeneration::IsRetained(Property->GetOwnerClass());
    FString Notification;
    if (Setter->HasLocalRepNotify())
    {
        UFunction* NotificationFunction = Property->GetOwnerClass()->FindFunctionByName(Setter->GetRepNotifyName());
        if (bRetainedOwner || NotificationFunction->HasAnyFunctionFlags(FUNC_Private))
        {
            FString NotificationReceiver = ReceiverName;
            if (NotificationReceiver.IsEmpty())
            {
                NotificationReceiver = TEXT("this");
            }
            FString NotificationName = NotificationFunction->GetName();
            if (!bRetainedOwner)
            {
                NotificationName = UBlueprintNativizationLibrary::GetUniqueFunctionName(NotificationFunction, TEXT(""));
            }
            Notification = FString::Format(TEXT("if (UFunction* ReplicationNotification = {0}->FindFunction(TEXT(\"{1}\")))\n{{\n{0}->ProcessEvent(ReplicationNotification, nullptr);\n}}\n"),
                { NotificationReceiver, NotificationName });
            if (bRetainedOwner && bExternalTarget)
            {
                Notification = TEXT("if (IsValid(") + NotificationReceiver + TEXT("))\n{\n") + Notification + TEXT("}\n");
            }
        }
        else
        {
            Notification = UBlueprintNativizationLibrary::GetUniqueFunctionName(NotificationFunction, TEXT("")) + TEXT("();\n");
            if (bExternalTarget)
            {
                Notification = ReceiverName + TEXT("->") + Notification;
            }
        }
    }
    if (Property->HasMetaData(FBlueprintMetadata::MD_PropertySetFunction))
    {
        UFunction* SetterFunction = Property->GetOwnerClass()->FindFunctionByName(FName(*Property->GetMetaData(FBlueprintMetadata::MD_PropertySetFunction)));
        FString Receiver = ReceiverName;
        if (Receiver.IsEmpty())
        {
            Receiver = TEXT("this");
        }
        return Content + FRetainedBlueprintGeneration::CallPropertyAccessor(Property, SetterFunction, Receiver, Value.Code, true) + TEXT(";\n") + Notification;
    }
    if (bRetainedOwner)
    {
        if (ReceiverName.IsEmpty())
        {
            ReceiverName = TEXT("this");
        }
        return Content + FRetainedBlueprintGeneration::WriteProperty(Property, ReceiverName, Value.Code) + Notification;
    }
    FGetterAndSetterPropertyDescriptor Descriptor;
    const bool bUsesSetter = UBlueprintNativizationSettingsLibrary::FindGetterAndSetterDescriptorDescriptorByProperty(Property, Descriptor) &&
        !Descriptor.SetterPropertyFunctionName.IsEmpty();
    if (bUsesSetter)
    {
        Assignment = Descriptor.SetterPropertyFunctionName + TEXT("(") + Value.Code + TEXT(");\n");
        if (bExternalTarget)
        {
            Assignment = ReceiverName + TEXT("->") + Assignment;
        }
    }
    Assignment += Notification;
    if (bExternalTarget)
    {
        Assignment = TEXT("if (IsValid(") + ReceiverName + TEXT("))\n{\n") + Assignment + TEXT("}\n");
    }
    return Content + Assignment;
}

FGenerateResultStruct UVariableSetTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node,
    UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem)
{
    return UBlueprintNativizationLibrary::GenerateVariableAccess(CastChecked<UK2Node_VariableSet>(Node), MacroStack, Subsystem);
}
