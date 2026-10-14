/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/ReturnTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSettings.h"
#include "Engine/Blueprint.h"

FString UReturnTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations, UNativizationV2Subsystem* NativizationV2Subsystem)
{
    UFunction* Function = UBlueprintNativizationLibrary::GetFunctionByNodeAndEntryNodes(Node, NativizationV2Subsystem->EntryNodes);
    FString Content;
    FString Assignments;
    FString ReturnValue;
    if (Function)
    {
        FFunctionDescriptor NativeFunction;
        const bool bNativeVoidReturn = UBlueprintNativizationSettingsLibrary::FindNativeByClassAndName(
            Node->GetBlueprint()->GeneratedClass, Function->GetName(), NativeFunction);
        int32 OutputCount = 0;
        for (TFieldIterator<FProperty> PropertyIterator(Function); PropertyIterator && PropertyIterator->HasAnyPropertyFlags(CPF_Parm); ++PropertyIterator)
        {
            if (PropertyIterator->HasAnyPropertyFlags(CPF_OutParm) && !PropertyIterator->HasAnyPropertyFlags(CPF_ConstParm))
            {
                ++OutputCount;
            }
        }
        for (TFieldIterator<FProperty> PropertyIterator(Function); PropertyIterator && PropertyIterator->HasAnyPropertyFlags(CPF_Parm); ++PropertyIterator)
        {
            FProperty* Property = *PropertyIterator;
            if (!Property->HasAnyPropertyFlags(CPF_OutParm) || Property->HasAnyPropertyFlags(CPF_ConstParm))
            {
                continue;
            }
            UEdGraphPin* Pin = Node->FindPin(Property->GetFName(), EGPD_Input);
            if (!Pin)
            {
                continue;
            }
            FGenerateResultStruct Value = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, Pin, 0, MacroStack);
            FEdGraphPinType TargetType;
            UBlueprintNativizationLibrary::ConvertPropertyToPinType(Property, TargetType);
            UEdGraphPin* SourcePin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Pin, 0, true, MacroStack, NativizationV2Subsystem);
            Value.Code = UBlueprintNativizationLibrary::ConvertArrayValue(SourcePin, TargetType, Value.Code);
            Content += GenerateNewPreparations(Preparations, Value.Preparations);
            Preparations.Append(Value.Preparations);
            FString ValueName = Value.Code;
            if (OutputCount > 1)
            {
                ValueName = NativizationV2Subsystem->GetNodeStateName(Node, Property->GetName() + TEXT("Result"));
                FString SnapshotType = TEXT("auto");
                if (Value.Code.TrimStartAndEnd() == TEXT("{}"))
                {
                    SnapshotType = UBlueprintNativizationLibrary::GetPropertyType(Property);
                }
                Content += FString::Format(TEXT("const {0} {1} = {2};\n"), { SnapshotType, ValueName, Value.Code });
            }
            if (Property->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                if (bNativeVoidReturn)
                {
                    Content += FString::Format(TEXT("static_cast<void>({0});\n"), {ValueName});
                }
                else
                {
                    ReturnValue = ValueName;
                }
            }
            else
            {
                Assignments += FString::Format(TEXT("{0} = {1};\n"), {
                    UBlueprintNativizationLibrary::GetUniquePropertyName(Property), ValueName });
            }
        }
    }
    return Content + Assignments + NativizationV2Subsystem->GenerateFunctionReturnStatement(ReturnValue);
}
