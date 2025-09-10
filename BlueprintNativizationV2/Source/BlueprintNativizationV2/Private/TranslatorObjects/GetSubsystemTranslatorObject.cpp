/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/GetSubsystemTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"

FGenerateResultStruct UGetSubsystemTranslatorObject::GenerateInputParameterCodeForNode(
	UK2Node* Node,
	UEdGraphPin* Pin,
	int PinIndex,
	TArray<UK2Node*> MacroStack,
	UNativizationV2Subsystem* NativizationV2Subsystem
)
{
	if (UK2Node_GetSubsystem* GetSubsystemNode = Cast<UK2Node_GetSubsystem>(Node))
	{
		FProperty* Property = GetSubsystemNode->GetClass()->FindPropertyByName("CustomClass");

		FObjectProperty* ObjProp = CastField<FObjectProperty>(Property);
		if (ObjProp)
		{
			UObject* Value = ObjProp->GetObjectPropertyValue_InContainer(GetSubsystemNode);
			UClass* SubsystemClass = Cast<UClass>(Value);
			FString SubsystemCppName = UBlueprintNativizationLibrary::GetUniqueFieldName(SubsystemClass);
			FString ContextCall;

			TArray<UEdGraphPin*> Pins = UBlueprintNativizationLibrary::GetFilteredPins(Node, EPinOutputOrInputFilter::Input, EPinExcludeFilter::ExecPin | EPinExcludeFilter::DelegatePin, EPinIncludeOnlyFilter::None);
	
			if (Pins[0]->LinkedTo.Num() > 0)
			{
				if (SubsystemClass->IsChildOf(ULocalPlayerSubsystem::StaticClass()))
				{
					FGenerateResultStruct InputResultStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, Pins[0], 0, MacroStack);
					return FGenerateResultStruct(FString::Printf(TEXT("ULocalPlayer::GetSubsystem<%s>(%s->GetLocalPlayer())"), *SubsystemCppName, *InputResultStruct.Code), InputResultStruct.Preparations);
				}
			}
			else
			{
				if (SubsystemClass->IsChildOf(UGameInstanceSubsystem::StaticClass()))
				{
					ContextCall = TEXT("GetGameInstance()");
				}
				else if (SubsystemClass->IsChildOf(UEngineSubsystem::StaticClass()))
				{
					ContextCall = TEXT("GEngine");
				}
				else
				{
					ContextCall = TEXT("GetWorld()");
				}
				return FString::Printf(TEXT("%s->GetSubsystem<%s>()"), *ContextCall, *SubsystemCppName);
			}
		}
	}

	return FGenerateResultStruct();
}

TSet<FString> UGetSubsystemTranslatorObject::GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	TSet<FString> Includes;

	if (UK2Node_GetSubsystem* GetSubsystemNode = Cast<UK2Node_GetSubsystem>(Node))
	{
		FProperty* Property = GetSubsystemNode->GetClass()->FindPropertyByName("CustomClass");

		FObjectProperty* ObjProp = CastField<FObjectProperty>(Property);
		if (ObjProp)
		{
			UObject* Value = ObjProp->GetObjectPropertyValue_InContainer(GetSubsystemNode);
			UClass* SubsystemClass = Cast<UClass>(Value);

			FString IncludePath;
			if (NativizationV2Subsystem->GetIncludeFromField(SubsystemClass, IncludePath))
			{
				Includes.Add(IncludePath);
			}
		}
	}
	return Includes;
}

TSet<FString> UGetSubsystemTranslatorObject::GenerateCSPrivateDependencyModuleNames(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem)
{


	TSet<FString> ModuleSet;
	if (UK2Node_GetSubsystem* GetSubsystemNode = Cast<UK2Node_GetSubsystem>(Node))
	{
		FProperty* Property = GetSubsystemNode->GetClass()->FindPropertyByName("CustomClass");

		FObjectProperty* ObjProp = CastField<FObjectProperty>(Property);
		if (ObjProp)
		{
			UObject* Value = ObjProp->GetObjectPropertyValue_InContainer(GetSubsystemNode);
			UClass* SubsystemClass = Cast<UClass>(Value);

			FString ModuleName;
			if (UBlueprintNativizationLibrary::FindFieldModuleName(SubsystemClass, ModuleName))
			{
				TArray<FString> Modules;
				ModuleName.ParseIntoArray(Modules, TEXT("-"), true);

				ModuleSet.Add(FString::Format(TEXT("\"{0}\""), { Modules[1] }));
			}
		}
	}

	return ModuleSet;
}
