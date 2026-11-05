/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright 2026 AO All Rights Reserved.
 */


#include "TranslatorObjects/ComponentEventTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"
#include "NativeApiCatalog.h"
#include "BlueprintNativizationSettings.h"
#include "RetainedBlueprintGeneration.h"
#include "NativeGenerationContext.h"
#include "UObject/UnrealType.h"
#include "Engine/MemberReference.h"


FMulticastDelegateProperty* UComponentEventTranslatorObject::ResolveComponentDelegate(UK2Node_ComponentBoundEvent* Event, FObjectPropertyBase*& ComponentProperty, FString& Error)
{
    ComponentProperty = FindFProperty<FObjectPropertyBase>(Event->GetBlueprint()->GeneratedClass, Event->ComponentPropertyName);
    if (!ComponentProperty)
    {
        Error = FString::Format(TEXT("Cannot resolve component {0} for event in {1}."),
            {Event->ComponentPropertyName.ToString(), Event->GetBlueprint()->GetPathName()});
        return nullptr;
    }
    FMulticastDelegateProperty* PropertyComponentName = FindFProperty<FMulticastDelegateProperty>(ComponentProperty->PropertyClass, Event->DelegatePropertyName);
    if (!PropertyComponentName)
    {
        PropertyComponentName = FMemberReference::FindRemappedField<FMulticastDelegateProperty>(
            ComponentProperty->PropertyClass, Event->DelegatePropertyName);
    }
    if (!PropertyComponentName)
    {
        TArray<FString> AvailableDelegates;
        for (TFieldIterator<FMulticastDelegateProperty> DelegateIterator(ComponentProperty->PropertyClass); DelegateIterator; ++DelegateIterator)
        {
            AvailableDelegates.Add(DelegateIterator->GetName());
        }
        Error = FString::Format(TEXT("Cannot bind component delegate {0}.{1} in {2}. The source event does not match a delegate of component class {3}. Available delegates: {4}. Repair the source Blueprint event or its component class."),
            {Event->ComponentPropertyName.ToString(), Event->DelegatePropertyName.ToString(),
                Event->GetBlueprint()->GetPathName(), ComponentProperty->PropertyClass->GetPathName(), FString::Join(AvailableDelegates, TEXT(", "))});
        return nullptr;
    }
    return PropertyComponentName;
}

FString UComponentEventTranslatorObject::GenerateConstruction(UK2Node* InputNode, FString Prefix, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	if (UK2Node_ComponentBoundEvent* ComponentBoundEvent = Cast<UK2Node_ComponentBoundEvent>(InputNode))
	{	
        FObjectPropertyBase* ComponentProperty = nullptr;
        FString Error;
        FMulticastDelegateProperty* PropertyComponentName = ResolveComponentDelegate(ComponentBoundEvent, ComponentProperty, Error);
        if (!PropertyComponentName)
        {
            FNativeApiCatalog::AddError(Error);
            return FString();
        }
        FString PropertyName = UBlueprintNativizationLibrary::GetUniquePropertyName(ComponentProperty);
        FString ComponentInclude;
        if (NativizationV2Subsystem->GetIncludeFromField(ComponentProperty->PropertyClass, ComponentInclude))
        {
            FNativeGenerationContext::AddImplementationInclude(ComponentInclude);
        }
        FGetterAndSetterPropertyDescriptor Descriptor;
        if (UBlueprintNativizationSettingsLibrary::FindGetterAndSetterDescriptorDescriptorByProperty(ComponentProperty, Descriptor) &&
            !Descriptor.GetterPropertyFunctionName.IsEmpty())
        {
            PropertyName = Descriptor.GetterPropertyFunctionName + TEXT("()");
        }
        else if (ComponentProperty->HasAnyPropertyFlags(CPF_NativeAccessSpecifierPrivate | CPF_NativeAccessSpecifierProtected))
        {
            PropertyName = FRetainedBlueprintGeneration::ReadProperty(ComponentProperty, TEXT("this"));
        }

        FString Binding;
        if (FRetainedBlueprintGeneration::IsRetained(PropertyComponentName->GetOwnerClass()) ||
            PropertyComponentName->HasAnyPropertyFlags(CPF_NativeAccessSpecifierPrivate | CPF_NativeAccessSpecifierProtected))
        {
            FNativeGenerationContext::AddImplementationInclude(TEXT("#include \"UObject/UnrealType.h\""));
            FNativeGenerationContext::AddImplementationInclude(TEXT("#include \"UObject/ScriptDelegates.h\""));
            const FString EventName = UBlueprintNativizationLibrary::GetUniqueEntryNodeName(InputNode,
                NativizationV2Subsystem->EntryNodes, TEXT(""));
            Binding = TEXT("{\n") + FString::Format(TEXT("auto* BoundComponent = {0};\nFMulticastDelegateProperty* ComponentDelegate = FindFProperty<FMulticastDelegateProperty>(BoundComponent->GetClass(), {1});\ncheck(ComponentDelegate);\nFScriptDelegate ComponentBinding;\nComponentBinding.BindUFunction(this, FName({2}));\nComponentDelegate->RemoveDelegate(ComponentBinding, BoundComponent);\nComponentDelegate->AddDelegate(ComponentBinding, BoundComponent);\n"),
                { PropertyName, FRetainedBlueprintGeneration::Quote(PropertyComponentName->GetName()), FRetainedBlueprintGeneration::Quote(EventName) }) + TEXT("}\n");
        }
        else
        {
		Binding = FString::Format(TEXT("{0}->{1}.AddUniqueDynamic(this, &{2}::{3});\n"),
			{ PropertyName,
			UBlueprintNativizationLibrary::GetUniquePropertyName(PropertyComponentName),
			UBlueprintNativizationLibrary::GetUniqueFieldName(InputNode->GetBlueprint()->GeneratedClass),
			UBlueprintNativizationLibrary::GetUniqueEntryNodeName(InputNode, NativizationV2Subsystem->EntryNodes, TEXT(""))
		});
        }
        if (!FRetainedBlueprintGeneration::bGeneratingConstructor)
        {
            Binding = TEXT("if (IsValid(") + PropertyName + TEXT("))\n{\n") + Binding + TEXT("}\n");
        }
        return Binding;
	}
	return FString();
}
