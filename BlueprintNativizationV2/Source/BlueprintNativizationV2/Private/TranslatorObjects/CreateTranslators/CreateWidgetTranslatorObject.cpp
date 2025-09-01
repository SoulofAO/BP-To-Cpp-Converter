/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/CreateTranslators/CreateWidgetTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"

FString UCreateWidgetTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	if (!Node || !NativizationV2Subsystem)
	{
		return FString();
	}
	UK2Node* CreateWidgetNode = Node; // No custom subclass, use as is

	FString Content;

	UEdGraphPin* ClassPin = UBlueprintNativizationLibrary::GetPinByName(Node->Pins, TEXT("Class"));
	UEdGraphPin* OwningPlayerPin = UBlueprintNativizationLibrary::GetPinByName(Node->Pins, TEXT("OwningPlayer"));
	UEdGraphPin* ResultPin = UBlueprintNativizationLibrary::GetPinByName(Node->Pins, TEXT("ReturnValue"));

	FString BaseClassName = ClassPin ? NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, ClassPin, 0, MacroStack) : TEXT("nullptr");
	FString OwningPlayerArg = OwningPlayerPin ? NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, OwningPlayerPin, 0, MacroStack) : TEXT("nullptr");
	FString ResultVar = "";
	FString DeclarationVar = "";

	// Try to get the actual widget class name for the template argument
	FString WidgetClassName = TEXT("UUserWidget");
	if (ClassPin && ClassPin->DefaultObject)
	{
		WidgetClassName = UBlueprintNativizationLibrary::GetUniqueFieldName(Cast<UClass>(ClassPin->DefaultObject));
	}

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
		*BaseClassName
	});

	TArray<FProperty*> ExposeOnSpawnProperties;
	if (ClassPin && ClassPin->DefaultObject)
	{
		ExposeOnSpawnProperties = UBlueprintNativizationLibrary::GetExposeOnSpawnProperties(Cast<UClass>(ClassPin->DefaultObject));
	}

	if (ExposeOnSpawnProperties.Num() > 0)
	{
		Content += FString::Format(TEXT("if({0})\n{\n"), { ResultVar });

		for (FProperty* Property : ExposeOnSpawnProperties)
		{
			UEdGraphPin* FoundPin = CreateWidgetNode->FindPin(Property->GetName());
			if (FoundPin)
			{
				Content += FString::Format(TEXT("{0}->{1} = {2};\n"), { ResultVar, UBlueprintNativizationLibrary::GetUniquePropertyName(Property, NativizationV2Subsystem->EntryNodes), NativizationV2Subsystem->GenerateInputParameterCodeForNode(CreateWidgetNode, FoundPin, 0, MacroStack) });
			}
		}
		Content += "}\n";
	}
	return Content;
}
