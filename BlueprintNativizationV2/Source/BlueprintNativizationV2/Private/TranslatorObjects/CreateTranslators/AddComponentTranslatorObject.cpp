/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/CreateTranslators/AddComponentTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"

FString UAddComponentTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, TSet<FString>& Preparations, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	if (!Node || !NativizationV2Subsystem)
	{
		return FString();
	}

	FString Content;
	TSet<FString> NewPreparations;

	if (UK2Node_AddComponent* AddComponentNode = Cast<UK2Node_AddComponent>(Node))
	{
		UEdGraphPin* ResultPin = UBlueprintNativizationLibrary::GetPinByName(Node->Pins, TEXT("ReturnValue"));
		FString BaseClassName = UBlueprintNativizationLibrary::GetUniqueFieldName(AddComponentNode->TemplateType);

		FString ResultVar = "";
		FString DeclarationVar = "";


		if (ResultPin && ResultPin->LinkedTo.Num() > 0)
		{
			ResultVar = UBlueprintNativizationLibrary::GetUniquePinName(ResultPin, NativizationV2Subsystem->EntryNodes);
		}
		else
		{
			ResultVar = UBlueprintNativizationLibrary::GetLambdaUniqueVariableNameByNode(TEXT("AddedComponent"), AddComponentNode, NativizationV2Subsystem->EntryNodes);
			DeclarationVar = BaseClassName + TEXT("* ");
		}

		Content += FString::Format(TEXT("{0}{1} = NewObject<{2}>(this, {2}::StaticClass());\n"),
			{
				*DeclarationVar,
				*ResultVar,
				*BaseClassName
			});

		if (AddComponentNode->TemplateType->IsChildOf(USceneComponent::StaticClass()))
		{
			UEdGraphPin* ManualAttachmentPin = UBlueprintNativizationLibrary::GetPinByName(Node->Pins, TEXT("bManualAttachment"));
			UEdGraphPin* RelativeTransformPin = UBlueprintNativizationLibrary::GetPinByName(Node->Pins, TEXT("RelativeTransform"));

			FGenerateResultStruct  ManualAttachmentArgResultStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(AddComponentNode, ManualAttachmentPin, 0, MacroStack);
			FGenerateResultStruct  RelativeTransformArgResultStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(AddComponentNode, RelativeTransformPin, 0, MacroStack);

			NewPreparations.Append(ManualAttachmentArgResultStruct.Preparations);
			NewPreparations.Append(RelativeTransformArgResultStruct.Preparations);

			Content += FString::Format(TEXT("if (!{0}->bManualAttachment)\n{\n\t{0}->SetupAttachment(nullptr);\n}\n"), { *ResultVar });
			Content += FString::Format(TEXT("{0}->SetRelativeTransform({1});\n"), { *ResultVar, *RelativeTransformArgResultStruct.Code });
		}

		TArray<FProperty*> ExposeOnSpawnProperties = UBlueprintNativizationLibrary::GetExposeOnSpawnProperties(AddComponentNode->TemplateType);

		TArray<FProperty*> PropertiesToSetup;

		if (UActorComponent* TemplateComponent = AddComponentNode->GetTemplateFromNode())
		{
			UClass* ComponentClass = TemplateComponent->GetClass();
			UActorComponent* CDO = Cast<UActorComponent>(ComponentClass->GetDefaultObject());
			UObject* SuperCDO = ComponentClass->GetSuperClass() ? ComponentClass->GetSuperClass()->GetDefaultObject() : nullptr;

			for (TFieldIterator<FProperty> PropIt(ComponentClass); PropIt; ++PropIt)
			{
				FProperty* Property = *PropIt;
				if (!Property || !Property->HasAllPropertyFlags(CPF_BlueprintVisible) || Property->HasAnyPropertyFlags(CPF_Transient | CPF_DisableEditOnTemplate))
				{
					continue;
				}

				FString CDOValueStr, SuperValueStr;
				Property->ExportText_InContainer(0, CDOValueStr, CDO, nullptr, CDO, PPF_None);

				if (SuperCDO)
				{
					Property->ExportText_InContainer(0, SuperValueStr, SuperCDO, nullptr, SuperCDO, PPF_None);
				}

				if (!CDOValueStr.Equals(SuperValueStr))
				{
					PropertiesToSetup.Add(Property);
				}
			}
		}

		if (PropertiesToSetup.Num() > 0 || ExposeOnSpawnProperties.Num())
		{
			Content += FString::Format(TEXT("if({0}) \n{\n"), { ResultVar});
			if (UActorComponent* TemplateComponent = AddComponentNode->GetTemplateFromNode())
			{
				UClass* ComponentClass = TemplateComponent->GetClass();
				UActorComponent* CDO = Cast<UActorComponent>(ComponentClass->GetDefaultObject());

				for (FProperty* Property : PropertiesToSetup)
				{
					if (ExposeOnSpawnProperties.Contains(Property))
					{
						continue;
					}
					FString FixedValue = UBlueprintNativizationLibrary::GenerateOneLineDefaultConstructorForProperty(Property, NativizationV2Subsystem->LeftAllAssetRefInBlueprint, Property->ContainerPtrToValuePtr<void>(CDO));
					FString CPPName = FString::Format(TEXT("{0}->{1}"), { *ResultVar, *Property->GetName() });
					Content += FString::Format(TEXT("\t{0} = {1};\n"), { *CPPName, *FixedValue });
				}
			}

			for (FProperty* Property : ExposeOnSpawnProperties)
			{
				UEdGraphPin* FoundPin = AddComponentNode->FindPin(Property->GetName());
				if (FoundPin)
				{
					FGenerateResultStruct GenerateResultStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(AddComponentNode, FoundPin, 0, MacroStack);
					NewPreparations.Append(GenerateResultStruct.Preparations);

					Content += FString::Format(TEXT("{0}->{1} = {2};\n"), {
						ResultVar,
						UBlueprintNativizationLibrary::GetUniquePropertyName(Property),
						GenerateResultStruct.Code
					});
				}
			}

			Content += "}\n";
		}

		Content += FString::Format(TEXT("{0}->RegisterComponent();\n"), { *ResultVar });

	}

	else if (UK2Node_AddComponentByClass* AddComponentByClassNode = Cast<UK2Node_AddComponentByClass>(Node))
	{
		UEdGraphPin* ClassPin = UBlueprintNativizationLibrary::GetPinByName(Node->Pins, TEXT("Class"));
		UEdGraphPin* ResultPin = UBlueprintNativizationLibrary::GetPinByName(Node->Pins, TEXT("ReturnValue"));

		FGenerateResultStruct ClassArgResultStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, ClassPin, 0, MacroStack);
		FString ClassArg = ClassPin ? ClassArgResultStruct.Code : TEXT("nullptr");
		FString ResultVar = "";
		FString DeclarationVar = "";

		if (ResultPin && ResultPin->LinkedTo.Num() > 0)
		{
			ResultVar = UBlueprintNativizationLibrary::GetUniquePinName(ResultPin, NativizationV2Subsystem->EntryNodes);
		}
		else
		{
			ResultVar = UBlueprintNativizationLibrary::GetLambdaUniqueVariableNameByNode(TEXT("AddedComponent"), AddComponentByClassNode, NativizationV2Subsystem->EntryNodes);
			DeclarationVar = TEXT("UActorComponent* ");
		}

		Content += FString::Format(TEXT("{0}{1} = NewObject<{2}>(this, {2}::StaticClass());\n"),
		{
			*DeclarationVar,
			*ResultVar,
			*ClassArg
		});

		if (AddComponentByClassNode->GetClassToSpawn()->IsChildOf(USceneComponent::StaticClass()))
		{
			UEdGraphPin* ManualAttachmentPin = UBlueprintNativizationLibrary::GetPinByName(Node->Pins, TEXT("bManualAttachment"));
			UEdGraphPin* RelativeTransformPin = UBlueprintNativizationLibrary::GetPinByName(Node->Pins, TEXT("RelativeTransform"));

			FGenerateResultStruct  ManualAttachmentArgResultStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(AddComponentNode, ManualAttachmentPin, 0, MacroStack);
			FGenerateResultStruct  RelativeTransformArgResultStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(AddComponentNode, RelativeTransformPin, 0, MacroStack);
			NewPreparations.Append(ManualAttachmentArgResultStruct.Preparations);
			NewPreparations.Append(RelativeTransformArgResultStruct.Preparations);

			Content += FString::Format(TEXT("if (!{0}->bManualAttachment)\n{\n\t{0}->SetupAttachment(nullptr);\n}\n"), { *ResultVar });
			Content += FString::Format(TEXT("{0}->SetRelativeTransform({1});\n"), { *ResultVar, *RelativeTransformArgResultStruct.Code });
		}

		TArray<FProperty*> ExposeOnSpawnProperties = UBlueprintNativizationLibrary::GetExposeOnSpawnProperties(AddComponentByClassNode->GetClassToSpawn());
		if (ExposeOnSpawnProperties.Num() > 0)
		{
			Content += FString::Format(TEXT("if({0})\n{\n"), { ResultVar });
			for (FProperty* Property : ExposeOnSpawnProperties)
			{
				UEdGraphPin* FoundPin = AddComponentByClassNode->FindPin(Property->GetName());
				if (FoundPin)
				{
					FGenerateResultStruct GenerateResultStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(AddComponentNode, FoundPin, 0, MacroStack);
					NewPreparations.Append(GenerateResultStruct.Preparations);

					Content += FString::Format(TEXT("{0}->{1} = {2};\n"), {
						ResultVar,
						UBlueprintNativizationLibrary::GetUniquePropertyName(Property),
						GenerateResultStruct.Code
						});
				}
			}
			Content += "}\n";
		}
		Content += FString::Format(TEXT("{0}->RegisterComponent();\n"), { *ResultVar });
	}

	FString Preparation = GenerateNewPreparations(Preparations, NewPreparations);
	Preparations.Append(NewPreparations);
	return Preparation + Content;
}
