/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/SequenceTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"

FString USequenceTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	FString Content;

	TArray<UEdGraphPin*> ExecPins = UBlueprintNativizationLibrary::GetFilteredPins(Node, EPinOutputOrInputFilter::Ouput, EPinExcludeFilter::None, EPinIncludeOnlyFilter::ExecPin);

	for (UEdGraphPin* OutPin : ExecPins)
	{
		if (OutPin->PinName.ToString().StartsWith("Then") && OutPin->LinkedTo.Num() > 0)
		{
            TArray<UK2Node*> BranchMacroStack = MacroStack;
            UEdGraphPin* NextPin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(OutPin, 0, false, BranchMacroStack, NativizationV2Subsystem);
            if (!NextPin)
            {
                continue;
            }
            UK2Node* NextNode = Cast<UK2Node>(NextPin->GetOwningNode());
            FString NextPinName = NextPin->GetName();
			FGenerationPreparations BranchPreparations = Preparations;
			Content += TEXT("{\n") + NativizationV2Subsystem->GenerateCodeFromNode(NextNode, NextPinName, VisitedNodes, BranchPreparations, BranchMacroStack) + TEXT("\n}\n");
		}
	}

	return Content;
}
