#include "TranslatorObjects/InputEventTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationSettings.h"
#include "Engine/InputDelegateBinding.h"
#include "K2Node_Event.h"
#include "K2Node_EnhancedInputAction.h"
#include "UObject/UnrealType.h"

bool UInputEventTranslatorObject::CanApply(UK2Node* Node)
{
    return Node->IsA<UK2Node_Event>() && HasInputBinding(Node);
}

bool UInputEventTranslatorObject::HasInputBinding(UK2Node* Node)
{
    if (Node->IsA<UK2Node_EnhancedInputAction>())
    {
        return true;
    }
    UClass* BindingClass = Node->GetDynamicBindingClass();
    return BindingClass && BindingClass->IsChildOf(UInputDelegateBinding::StaticClass());
}

void UInputEventTranslatorObject::GenerateNewFunction(UK2Node* InputNode, TArray<UK2Node*> Path,
    TArray<FGenerateFunctionStruct>& EntryNodes, UNativizationV2Subsystem* Subsystem)
{
    for (FGenerateFunctionStruct& Entry : EntryNodes)
    {
        if (Entry.Node == InputNode)
        {
            Entry.bRequiresReflection = true;
            Entry.OriginalUFunction = nullptr;
            Entry.CustomParametersString.Reset();
            for (UEdGraphPin* Pin : InputNode->Pins)
            {
                if (Pin->Direction == EGPD_Output && !Pin->ParentPin && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec &&
                    Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Delegate)
                {
                    Entry.CustomParametersString.Add(UBlueprintNativizationLibrary::GetPinType(Pin->PinType, false) + TEXT(" ") +
                        UBlueprintNativizationLibrary::GetUniquePinName(Pin, EntryNodes));
                }
            }
        }
    }
}

FString UInputEventTranslatorObject::GenerateSetupPlayerInputComponentFunction(UK2Node* InputNode, FString Prefix,
    UNativizationV2Subsystem* Subsystem)
{
    UClass* BindingClass = InputNode->GetDynamicBindingClass();
    UDynamicBlueprintBinding* Binding = NewObject<UDynamicBlueprintBinding>(GetTransientPackage(), BindingClass);
    InputNode->RegisterDynamicBinding(Binding);
    const FString BindingName = Subsystem->GetNodeStateName(InputNode, TEXT("Binding"));
    const FString BindingClassName = UBlueprintNativizationLibrary::GetUniqueFieldName(BindingClass);
    const FGenerateFunctionStruct* Entry = Subsystem->EntryNodes.FindByPredicate([&](const FGenerateFunctionStruct& Candidate)
    {
        return Candidate.Node == InputNode;
    });
    FString Content = TEXT("{\nauto* ") + BindingName + TEXT(" = NewObject<") + BindingClassName + TEXT(">(this);\n");
    for (TFieldIterator<FArrayProperty> PropertyIterator(BindingClass); PropertyIterator; ++PropertyIterator)
    {
        FArrayProperty* Property = *PropertyIterator;
        FStructProperty* Element = CastField<FStructProperty>(Property->Inner);
        if (!Element)
        {
            continue;
        }
        FScriptArrayHelper Values(Property, Property->ContainerPtrToValuePtr<void>(Binding));
        Content += BindingName + TEXT("->") + Property->GetNameCPP() + TEXT(".Reset();\n");
        for (int32 ValueIndex = 0; ValueIndex < Values.Num(); ++ValueIndex)
        {
            if (Entry)
            {
                if (FNameProperty* FunctionName = FindFProperty<FNameProperty>(Element->Struct, TEXT("FunctionNameToBind")))
                {
                    FunctionName->SetPropertyValue_InContainer(Values.GetRawPtr(ValueIndex), Entry->Name);
                }
            }
            FStructTranslationRequest Request;
            Request.Structure = Element->Struct;
            Request.Operation = EStructTranslationOperation::PropertyValue;
            Request.PropertyValue = Values.GetRawPtr(ValueIndex);
            Request.OwnerClass = InputNode->GetBlueprint()->GeneratedClass;
            Request.OwnerExpression = TEXT("this");
            FGenerationPreparations Preparations;
            const FString Expression = UBlueprintNativizationSettingsLibrary::TranslateStructure(Request, &Preparations);
            Content += FString::Join(Preparations.Array(), TEXT("\n")) + TEXT("\n");
            Content += BindingName + TEXT("->") + Property->GetNameCPP() + TEXT(".Add(") + Expression + TEXT(");\n");
        }
    }
    Content += BindingName + TEXT("->BindToInputComponent(PlayerInputComponent, this);\n}\n");
    return Content;
}

TSet<FString> UInputEventTranslatorObject::GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem)
{
    FString Include;
    Subsystem->GetIncludeFromField(Node->GetDynamicBindingClass(), Include);
    return { Include, TEXT("#include \"Components/InputComponent.h\""), TEXT("#include \"GameFramework/InputSettings.h\"") };
}
