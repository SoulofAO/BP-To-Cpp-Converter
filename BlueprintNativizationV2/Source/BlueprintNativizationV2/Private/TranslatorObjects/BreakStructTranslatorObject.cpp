/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright 2025 AO All Rights Reserved.
 */

#include "TranslatorObjects/BreakStructTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSettings.h"
#include "BlueprintNativizationSubsystem.h"
#include "K2Node_CallFunction.h"
#include "RetainedBlueprintGeneration.h"
#include "NativeApiCatalog.h"
#include "StructInitialization.h"

bool UBreakStructTranslatorObject::CanApply(UK2Node* Node)
{
    return Super::CanApply(Node);
}

FGenerateResultStruct UBreakStructTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
    if (UK2Node_BreakStruct* BreakStruct = Cast<UK2Node_BreakStruct>(Node))
    {
        TArray<UEdGraphPin*> Pins = UBlueprintNativizationLibrary::GetFilteredPins(Node, EPinOutputOrInputFilter::Input, EPinExcludeFilter::None, EPinIncludeOnlyFilter::None);

        FGenerateResultStruct GenerateInputCodeStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, Pins[0], 0, MacroStack);

        TArray<UEdGraphPin*> PathPins = UBlueprintNativizationLibrary::GetParentPathPins(Pin);
        Algo::Reverse(PathPins);

        UScriptStruct* CurrentStructure = BreakStruct->StructType;
        for (UEdGraphPin* CheckPin : PathPins)
        {
            FString MemberName = CheckPin->PinName.ToString();
            if (CheckPin->ParentPin)
            {
                MemberName.RemoveFromStart(CheckPin->ParentPin->PinName.ToString() + TEXT("_"));
            }
            FProperty* Property = CurrentStructure->FindPropertyByName(FName(*MemberName));
            const bool bHasTranslator = UBlueprintNativizationSettingsLibrary::FindStructTranslator(CurrentStructure) != nullptr;
            FGenerateResultStruct NativeMember;
            UEdGraphPin* StructurePin = CheckPin->ParentPin;
            if (!StructurePin)
            {
                StructurePin = Pins[0];
            }
            if (!bHasTranslator && FStructInitialization::TryReadNativeMember(CurrentStructure, StructurePin,
                FName(*MemberName), GenerateInputCodeStruct.Code, NativizationV2Subsystem, NativeMember))
            {
                GenerateInputCodeStruct.Preparations.Append(NativeMember.Preparations);
                GenerateInputCodeStruct.Code = NativeMember.Code;
                if (UScriptStruct* OutputStructure = Cast<UScriptStruct>(CheckPin->PinType.PinSubCategoryObject.Get()))
                {
                    CurrentStructure = OutputStructure;
                }
                continue;
            }
            if (!Property && !bHasTranslator)
            {
                FNativeApiCatalog::AddError(TEXT("Cannot resolve a structure member: ") + CurrentStructure->GetPathName() + TEXT(".") + MemberName);
                return {};
            }
            if (bHasTranslator)
            {
                FStructTranslationRequest Request;
                Request.Structure = CurrentStructure;
                Request.Operation = EStructTranslationOperation::ReadMember;
                Request.MemberName = FName(*MemberName);
                Request.DestinationType = UBlueprintNativizationLibrary::GetPinType(CheckPin->PinType, false);
                Request.SourceExpression = TEXT("(") + GenerateInputCodeStruct.Code + TEXT(")");
                GenerateInputCodeStruct.Code = UBlueprintNativizationSettingsLibrary::TranslateStructure(Request);
            }
            else
            {
                FGetterAndSetterPropertyDescriptor Descriptor;
                if (UBlueprintNativizationSettingsLibrary::FindGetterAndSetterDescriptorDescriptorByProperty(Property, Descriptor) &&
                    !Descriptor.GetterPropertyFunctionName.IsEmpty())
                {
                    GenerateInputCodeStruct.Code = TEXT("(") + GenerateInputCodeStruct.Code + TEXT(").") + Descriptor.GetterPropertyFunctionName + TEXT("()");
                }
                else if (Property->GetOwnerStruct()->IsNative() &&
                    Property->HasAnyPropertyFlags(CPF_NativeAccessSpecifierPrivate | CPF_NativeAccessSpecifierProtected))
                {
                    const FString ValueType = UBlueprintNativizationLibrary::GetPropertyType(Property);
                    const FString StructureType = UBlueprintNativizationLibrary::GetUniqueFieldName(Property->GetOwnerStruct());
                    FString ReadValue = TEXT("([](const ") + StructureType + TEXT("& StructureValue)\n{\n") + ValueType + TEXT(" MemberValue{};\nFProperty* MemberProperty = FindFProperty<FProperty>(") + StructureType +
                        TEXT("::StaticStruct(), ") + FRetainedBlueprintGeneration::Quote(Property->GetName()) +
                        TEXT(");\nconst void* MemberAddress = MemberProperty->ContainerPtrToValuePtr<void>(&StructureValue);\n");
                    ReadValue += FRetainedBlueprintGeneration::Transfer(Property, TEXT("MemberProperty"), TEXT("MemberAddress"), TEXT("MemberValue"), false);
                    GenerateInputCodeStruct.Code = ReadValue + TEXT("return MemberValue;\n})(") + GenerateInputCodeStruct.Code + TEXT(")");
                }
                else
                {
                    GenerateInputCodeStruct.Code += TEXT(".") + UBlueprintNativizationLibrary::GetUniquePropertyName(Property);
                }
            }
            if (UScriptStruct* OutputStructure = Cast<UScriptStruct>(CheckPin->PinType.PinSubCategoryObject.Get()))
            {
                CurrentStructure = OutputStructure;
            }
        }

        return GenerateInputCodeStruct;
    }
    return FGenerateResultStruct();
}

