/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/ClassDefaultsTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationSettings.h"
#include "RetainedBlueprintGeneration.h"
#include "NativeApiCatalog.h"
#include "NativeGenerationContext.h"
#include "K2Node_GetClassDefaults.h"
#include "K2Node_MakeVariable.h"
#include "K2Node_TunnelBoundary.h"

bool UClassDefaultsTranslatorObject::CanApply(UK2Node* Node)
{
    return Node->IsA<UK2Node_GetClassDefaults>() || Node->IsA<UK2Node_MakeVariable>() || Node->IsA<UK2Node_TunnelBoundary>();
}

TSet<FString> UClassDefaultsTranslatorObject::GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem)
{
    TSet<FString> Includes = Super::GenerateCppIncludeInstructions(Node, Subsystem);
    Includes.Add(TEXT("#include \"UObject/UnrealType.h\""));
    return Includes;
}

FGenerateResultStruct UClassDefaultsTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node,
    UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem)
{
    Pin = UBlueprintNativizationLibrary::GetParentPin(Pin);
    const FString ValueType = UBlueprintNativizationLibrary::GetPinType(Pin->PinType, false);
    if (Node->IsA<UK2Node_MakeVariable>())
    {
        TArray<FString> Values;
        FGenerationPreparations Preparations;
        for (UEdGraphPin* Input : Node->Pins)
        {
            if (Input->Direction == EGPD_Input && !Input->ParentPin)
            {
                const FGenerateResultStruct Value = Subsystem->GenerateInputParameterCodeForNode(Node, Input, 0, MacroStack);
                Preparations.Append(Value.Preparations);
                Values.Add(Value.Code);
            }
        }
        if (Pin->PinType.IsMap())
        {
            TArray<FString> Pairs;
            for (int32 ValueIndex = 0; ValueIndex + 1 < Values.Num(); ValueIndex += 2)
            {
                Pairs.Add(TEXT("{ ") + Values[ValueIndex] + TEXT(", ") + Values[ValueIndex + 1] + TEXT(" }"));
            }
            Values = MoveTemp(Pairs);
        }
        return FGenerateResultStruct(ValueType + TEXT("{ ") + FString::Join(Values, TEXT(", ")) + TEXT(" }"), Preparations);
    }
    UK2Node_GetClassDefaults* Defaults = CastChecked<UK2Node_GetClassDefaults>(Node);
    FGenerateResultStruct Result = Subsystem->GenerateInputParameterCodeForNode(Node, Defaults->FindClassPin(), 0, MacroStack);
    UClass* InputClass = Defaults->GetInputClass();
    FProperty* Property = FindFProperty<FProperty>(InputClass, Pin->PinName);
    if (!Property)
    {
        FNativeApiCatalog::AddError(TEXT("Cannot resolve class default property: ") + Pin->PinName.ToString());
        return FGenerateResultStruct();
    }
    const FString DefaultClassName = Subsystem->GetNodeStateName(Node, TEXT("DefaultClass"));
    const FString DefaultObjectName = Subsystem->GetNodeStateName(Node, TEXT("DefaultObject"));
    const FString ResultName = Subsystem->GetNodeStateName(Node, Pin->PinName.ToString() + TEXT("DefaultValue"), true);
    FString ReadStatements;
    if (FRetainedBlueprintGeneration::IsRetained(Property->GetOwnerClass()))
    {
        FGenerationPreparations ReadPreparations;
        const FString ReadValue = FRetainedBlueprintGeneration::PreparePropertyRead(Property,
            DefaultClassName + TEXT("->GetDefaultObject()"), Node, Subsystem, ReadPreparations);
        ReadStatements = FString::Join(ReadPreparations.Array(), TEXT("\n")) + TEXT("\n") + ResultName + TEXT(" = ") + ReadValue + TEXT(";\n");
    }
    else
    {
        FString MemberAccess = UBlueprintNativizationLibrary::GetUniquePropertyName(Property);
        if (Property->GetOwnerStruct()->IsNative() &&
            Property->HasAnyPropertyFlags(CPF_NativeAccessSpecifierPrivate | CPF_NativeAccessSpecifierProtected))
        {
            FGetterAndSetterPropertyDescriptor Descriptor;
            if (!UBlueprintNativizationSettingsLibrary::FindGetterAndSetterDescriptorDescriptorByProperty(Property, Descriptor) ||
                Descriptor.GetterPropertyFunctionName.IsEmpty())
            {
                FNativeApiCatalog::AddError(TEXT("Class default property requires an accessible C++ getter: ") + Property->GetPathName());
                return FGenerateResultStruct();
            }
            MemberAccess = Descriptor.GetterPropertyFunctionName + TEXT("()");
        }
        FString IncludePath;
        if (Subsystem->GetIncludeFromField(InputClass, IncludePath))
        {
            FNativeGenerationContext::AddImplementationInclude(IncludePath);
        }
        ReadStatements = FString::Format(TEXT("auto* {2} = {3}->GetDefaultObject<{0}>();\n{4} = {2}->{1};\n"),
            { FRetainedBlueprintGeneration::GetClassName(InputClass), MemberAccess, DefaultObjectName, DefaultClassName, ResultName });
    }
    Result.Preparations.Add(FString::Format(TEXT("{0} {4}{};\n{\nconst UClass* {3} = {1};\nif ({3})\n{\n{2}}\n}\n"),
        { ValueType, Result.Code, ReadStatements, DefaultClassName, ResultName }));
    Result.Code = ResultName;
    return Result;
}
