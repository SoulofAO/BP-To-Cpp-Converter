/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/SequenceTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"

FString USequenceTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	FString Content;

	// Получаем все Exec output пины (Then0, Then1, ...)
	TArray<UEdGraphPin*> ExecPins = UBlueprintNativizationLibrary::GetFilteredPins(Node, EPinOutputOrInputFilter::Ouput, EPinExcludeFilter::None, EPinIncludeOnlyFilter::ExecPin);

	for (UEdGraphPin* OutPin : ExecPins)
	{
		if (OutPin->PinName.ToString().StartsWith("Then") && OutPin->LinkedTo.Num() > 0)
		{
			UK2Node* NextNode = Cast<UK2Node>(OutPin->LinkedTo[0]->GetOwningNode());
			FString NextPinName = OutPin->LinkedTo[0]->GetName();
			Content += NativizationV2Subsystem->GenerateCodeFromNode(NextNode, NextPinName, VisitedNodes, MacroStack) + "\n";
		}
	}

	return Content;
}
