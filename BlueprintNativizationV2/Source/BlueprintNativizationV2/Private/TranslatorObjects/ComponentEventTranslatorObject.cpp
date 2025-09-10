// Fill out your copyright notice in the Description page of Project Settings.


#include "TranslatorObjects/ComponentEventTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"


FString UComponentEventTranslatorObject::GenerateConstruction(UK2Node* InputNode, FString Prefix, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	if (UK2Node_ComponentBoundEvent* ComponentBoundEvent = Cast<UK2Node_ComponentBoundEvent>(InputNode))
	{	
		FProperty* ComponentProperty = InputNode->GetBlueprint()->GeneratedClass->FindPropertyByName(ComponentBoundEvent->ComponentPropertyName);
		FString PropertyName = UBlueprintNativizationLibrary::GetUniquePropertyName(ComponentProperty);
		UActorComponent* Component = UBlueprintNativizationLibrary::GetComponentByPropertyName(ComponentProperty->GetName(), InputNode->GetBlueprint());

		FProperty* PropertyComponentName = Component->GetClass()->FindPropertyByName(ComponentBoundEvent->DelegatePropertyName);

		return FString::Format(TEXT("{0}->{1}.AddUniqueDynamic(this, {2}::{3});\n"), 
			{ PropertyName,
			UBlueprintNativizationLibrary::GetUniquePropertyName(PropertyComponentName),
			UBlueprintNativizationLibrary::GetUniqueFieldName(InputNode->GetBlueprint()->GeneratedClass),
			UBlueprintNativizationLibrary::GetEntryFunctionNameByNode(InputNode).ToString()
		});
		
	}
	return FString();
}
