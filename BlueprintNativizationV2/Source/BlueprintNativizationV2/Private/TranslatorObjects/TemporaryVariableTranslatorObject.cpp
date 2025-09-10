/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/TemporaryVariableTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"

FGenerateResultStruct UTemporaryVariableTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	if (UK2Node_TemporaryVariable* TemporaryVariable = Cast<UK2Node_TemporaryVariable>(Node))
	{
		return FString::Format(TEXT("{0}"), { *TemporaryVariable->GetName() });
	}
	return FGenerateResultStruct("");
}

TSet<FString> UTemporaryVariableTranslatorObject::GenerateLocalVariables(UK2Node* InputNode, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	if (UK2Node_TemporaryVariable* TemporaryVariable = Cast<UK2Node_TemporaryVariable>(InputNode))
	{
		return TSet<FString>({FString::Format(TEXT("{0} {1};\n"), {
					TemporaryVariable->VariableType.PinCategory.ToString(),
					UBlueprintNativizationLibrary::GetLambdaUniqueVariableNameByNode(*TemporaryVariable->GetName(), InputNode, NativizationV2Subsystem->EntryNodes)
			})
	});
	}
	return TSet<FString>();
}
