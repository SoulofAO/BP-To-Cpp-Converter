#include "CreationNodeTranslation.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"
#include "RetainedBlueprintGeneration.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"

FString FCreationNodeTranslation::Input(UK2Node* Node, UEdGraphPin* Pin, const TArray<UK2Node*>& MacroStack,
    UNativizationV2Subsystem* Subsystem, const FString& Stage, FString& Content, const FString& Default)
{
    if (!Pin || (Pin->LinkedTo.IsEmpty() && !Pin->DefaultObject && Pin->DefaultValue.IsEmpty() && Pin->SubPins.IsEmpty()))
    {
        return Default;
    }
    TGuardValue<FString> StageScope(Subsystem->InputEvaluationPurpose, Stage);
    const FGenerateResultStruct Value = Subsystem->GenerateInputParameterCodeForNode(Node, Pin, 0, MacroStack);
    for (const FString& Instruction : Value.Preparations.Difference(Subsystem->EmittedInputPreparations))
    {
        Content += Instruction + TEXT("\n");
    }
    Subsystem->EmittedInputPreparations.Append(Value.Preparations);
    const FString Name = Subsystem->GetNodeStateName(Node, Stage + Pin->PinName.ToString(), true);
    Content += TEXT("const auto ") + Name + TEXT(" = ") + Value.Code + TEXT(";\n");
    return Name;
}

FString FCreationNodeTranslation::Result(UK2Node* Node, UEdGraphPin* Pin, UNativizationV2Subsystem* Subsystem, FString& Content)
{
    const FString Name = UBlueprintNativizationLibrary::GetUniquePinName(Pin, Subsystem->EntryNodes);
    if (!UBlueprintNativizationLibrary::CheckAnySubPinLinked(Pin))
    {
        Content += UBlueprintNativizationLibrary::GetPinType(Pin->PinType, false) + TEXT(" ") + Name + TEXT("{};\n");
    }
    return Name;
}

FString FCreationNodeTranslation::AssignExposedProperties(UK2Node* Node, UClass* Class, const FString& Target,
    const TArray<UK2Node*>& MacroStack, UNativizationV2Subsystem* Subsystem)
{
    FString Content;
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin->Direction != EGPD_Input || Pin->ParentPin || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
        {
            continue;
        }
        FProperty* Property = FindFProperty<FProperty>(Class, Pin->PinName);
        if (!Property || !UEdGraphSchema_K2::IsPropertyExposedOnSpawn(Property))
        {
            continue;
        }
        if (!UBlueprintNativizationLibrary::CheckAnySubPinLinked(Pin))
        {
            const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
            if (!Schema->IsCurrentPinDefaultValid(Pin).IsEmpty())
            {
                continue;
            }
            UEdGraphPin* ClassPin = Node->FindPin(TEXT("Class"));
            const bool bDynamicClass = ClassPin && !ClassPin->LinkedTo.IsEmpty();
            if (!bDynamicClass || Property->HasAnyPropertyFlags(CPF_DisableEditOnTemplate) || !Property->HasAnyPropertyFlags(CPF_Edit))
            {
                if (UObject* Defaults = Class->GetDefaultObject(false))
                {
                    FString Default;
                    FBlueprintEditorUtils::PropertyValueToString(Property, reinterpret_cast<const uint8*>(Defaults), Default);
                    if (Schema->DoesDefaultValueMatch(*Pin, Default))
                    {
                        continue;
                    }
                    FString Value;
                    TObjectPtr<UObject> Object;
                    FText Text;
                    Schema->GetPinDefaultValuesFromString(Pin->PinType, Node, Default, Value, Object, Text, true);
                    if (Pin->DefaultValue == Value && Pin->DefaultObject == Object && Pin->DefaultTextValue.IdenticalTo(Text))
                    {
                        continue;
                    }
                }
            }
        }
        const FString Value = Input(Node, Pin, MacroStack, Subsystem, TEXT("Assign") + Pin->PinName.ToString(), Content);
        Content += TEXT("if (IsValid(") + Target + TEXT("))\n{\n");
        if (FRetainedBlueprintGeneration::IsRetained(Property->GetOwnerClass()) ||
            Property->HasAnyPropertyFlags(CPF_NativeAccessSpecifierPrivate | CPF_NativeAccessSpecifierProtected))
        {
            Content += FRetainedBlueprintGeneration::WriteProperty(Property, Target, Value);
        }
        else
        {
            Content += Target + TEXT("->") + UBlueprintNativizationLibrary::GetUniquePropertyName(Property) + TEXT(" = ") + Value + TEXT(";\n");
        }
        Content += TEXT("}\n");
    }
    return Content;
}
