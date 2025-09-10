/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/CreateTranslators/ConstructObjectTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"

FString UConstructObjectTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, TSet<FString>& Preparations, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	if (!Node || !NativizationV2Subsystem)
	{
		return FString();
	}
	UK2Node_ConstructObjectFromClass* ConstructNode = Cast<UK2Node_ConstructObjectFromClass>(Node);
	if (!ConstructNode)
	{
		return FString();
	}

	FString Content;
	TSet<FString> LocalPreparation;

	UEdGraphPin* ClassPin = UBlueprintNativizationLibrary::GetPinByName(Node->Pins, TEXT("Class"));
	UEdGraphPin* OuterPin = UBlueprintNativizationLibrary::GetPinByName(Node->Pins, TEXT("Outer"));
	UEdGraphPin* ResultPin = UBlueprintNativizationLibrary::GetPinByName(Node->Pins, TEXT("ReturnValue"));

	FString BaseClassName = UBlueprintNativizationLibrary::GetUniqueFieldName(ConstructNode->GetClassToSpawn());
	FGenerateResultStruct ClassPinResultStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, ClassPin, 0, MacroStack);
	FGenerateResultStruct OuterPinResultStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, OuterPin, 0, MacroStack);
	LocalPreparation.Append(ClassPinResultStruct.Preparations);
	LocalPreparation.Append(OuterPinResultStruct.Preparations);

	FString ClassArg = ClassPin ? ClassPinResultStruct.Code : TEXT("nullptr");
	FString OuterArg = OuterPin ? OuterPinResultStruct.Code : TEXT("nullptr");
	FString ResultVar = "";
	FString DeclarationVar = "";

	if (ResultPin && ResultPin->LinkedTo.Num() > 0)
	{
		ResultVar = UBlueprintNativizationLibrary::GetUniquePinName(ResultPin, NativizationV2Subsystem->EntryNodes);
	}
	else
	{
		ResultVar = UBlueprintNativizationLibrary::GetLambdaUniqueVariableNameByNode(TEXT("ConstructedObject"), ConstructNode, NativizationV2Subsystem->EntryNodes);
		DeclarationVar = BaseClassName + TEXT("* ");
	}

	Content += FString::Format(TEXT("{0}{1} = NewObject<{2}>({3}, {4});\n"),
	{
		*DeclarationVar,
		*ResultVar,
		*BaseClassName,
		*OuterArg,
		*ClassArg
	});

	TArray<FProperty*> ExposeOnSpawnProperties = UBlueprintNativizationLibrary::GetExposeOnSpawnProperties(ConstructNode->GetClassToSpawn());
	if (ExposeOnSpawnProperties.Num() > 0)
	{
		Content += FString::Format(TEXT("if({0})\n{\n"), { ResultVar });
		for (FProperty* Property : ExposeOnSpawnProperties)
		{
			UEdGraphPin* FoundPin = ConstructNode->FindPin(Property->GetName());
			if (FoundPin)
			{
				FGenerateResultStruct FoundPinResultStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(ConstructNode, FoundPin, 0, MacroStack);
				LocalPreparation.Append(FoundPinResultStruct.Preparations);
				Content += FString::Format(TEXT("{0}->{1} = {2};\n"), {
					ResultVar,
					UBlueprintNativizationLibrary::GetUniquePropertyName(Property),
					FoundPinResultStruct.Code
					});
			}
		}
		Content += "}\n";
	}

	FString Preparation = GenerateNewPreparations(Preparations, LocalPreparation);
	Preparations.Append(LocalPreparation);
	return Preparation + Content;
}
