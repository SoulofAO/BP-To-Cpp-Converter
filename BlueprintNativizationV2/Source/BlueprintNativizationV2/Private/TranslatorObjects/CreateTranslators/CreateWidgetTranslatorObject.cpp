/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/CreateTranslators/CreateWidgetTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"
FString UCreateWidgetTranslatorObject::GenerateCodeFromNode(
	UK2Node* Node,
	FString EntryPinName,
	TArray<FVisitedNodeStack> VisitedNodes,
	TArray<UK2Node*> MacroStack,
	TSet<FString>& Preparations,
	UNativizationV2Subsystem* NativizationV2Subsystem
)
{
	if (!Node || !NativizationV2Subsystem)
	{
		return FString();
	}

	UK2Node* CreateWidgetNode = Node; 
	FString Content;
	TSet<FString> LocalPreparation;

	UEdGraphPin* ClassPin = UBlueprintNativizationLibrary::GetPinByName(Node->Pins, TEXT("Class"));
	UEdGraphPin* OwningPlayerPin = UBlueprintNativizationLibrary::GetPinByName(Node->Pins, TEXT("OwningPlayer"));
	UEdGraphPin* ResultPin = UBlueprintNativizationLibrary::GetPinByName(Node->Pins, TEXT("ReturnValue"));

	FGenerateResultStruct ClassPinResultStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, ClassPin, 0, MacroStack);
	FGenerateResultStruct OwningPlayerResultStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, OwningPlayerPin, 0, MacroStack);

	LocalPreparation.Append(ClassPinResultStruct.Preparations);
	LocalPreparation.Append(OwningPlayerResultStruct.Preparations);

	FString ClassArg = ClassPin ? ClassPinResultStruct.Code : TEXT("nullptr");
	FString OwningPlayerArg = OwningPlayerPin ? OwningPlayerResultStruct.Code : TEXT("nullptr");
	FString ResultVar;
	FString DeclarationVar;

	FString WidgetClassName = TEXT("UUserWidget");
	if (ClassPin && ClassPin->DefaultObject)
	{
		WidgetClassName = UBlueprintNativizationLibrary::GetUniqueFieldName(Cast<UClass>(ClassPin->DefaultObject));
	}

	// Имя переменной
	if (ResultPin && ResultPin->LinkedTo.Num() > 0)
	{
		ResultVar = UBlueprintNativizationLibrary::GetUniquePinName(ResultPin, NativizationV2Subsystem->EntryNodes);
	}
	else
	{
		ResultVar = UBlueprintNativizationLibrary::GetLambdaUniqueVariableNameByNode(TEXT("CreatedWidget"), CreateWidgetNode, NativizationV2Subsystem->EntryNodes);
		DeclarationVar = WidgetClassName + TEXT("* ");
	}
	Content += FString::Format(TEXT("{0}{1} = CreateWidget<{2}>({3}, {4});\n"),
		{
			*DeclarationVar,
			*ResultVar,
			*WidgetClassName,
			*OwningPlayerArg,
			*ClassArg
		});

	TArray<FProperty*> ExposeOnSpawnProperties;
	if (ClassPin && ClassPin->DefaultObject)
	{
		ExposeOnSpawnProperties = UBlueprintNativizationLibrary::GetExposeOnSpawnProperties(Cast<UClass>(ClassPin->DefaultObject));
	}

	if (ExposeOnSpawnProperties.Num() > 0)
	{
		Content += FString::Format(TEXT("if({0})\n{{\n"), { ResultVar });
		for (FProperty* Property : ExposeOnSpawnProperties)
		{
			UEdGraphPin* FoundPin = CreateWidgetNode->FindPin(Property->GetName());
			if (FoundPin)
			{
				FGenerateResultStruct FoundPinResultStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(CreateWidgetNode, FoundPin, 0, MacroStack);
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

