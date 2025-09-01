/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/EnhancedInputTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationData.h"
#include "InputAction.h"


void UEnhancedInputTranslatorObject::GenerateNewFunction(UK2Node* InputNode, TArray<UK2Node*> Path, TArray<FGenerateFunctionStruct>& EntryNodes, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	if (UK2Node_EnhancedInputAction* EnhancedInputAction = Cast<UK2Node_EnhancedInputAction>(InputNode))
	{
		FGenerateFunctionStruct GenerateFunctionStruct;
		UBlueprintNativizationDataLibrary::FindGenerateFunctionStructByNode(EntryNodes, InputNode, GenerateFunctionStruct);
		EntryNodes.FindByKey(GenerateFunctionStruct)->IsTransientGenerateFunction = true;

		UEdGraphPin* ExecPins[] = {
			EnhancedInputAction->FindPin(TEXT("Triggered")),
			EnhancedInputAction->FindPin(TEXT("Started")),
			EnhancedInputAction->FindPin(TEXT("Ongoing")),
			EnhancedInputAction->FindPin(TEXT("Canceled")),
			EnhancedInputAction->FindPin(TEXT("Completed"))
		};

		const FString ExecNames[] = { TEXT("Triggered"), TEXT("Started"), TEXT("Ongoing"), TEXT("Canceled"), TEXT("Completed") };

		UEdGraphPin* ActionValuePin = EnhancedInputAction->FindPin(TEXT("ActionValue"));
		FString ActionValuePinName = UBlueprintNativizationLibrary::GetUniquePinName(ActionValuePin, EntryNodes);

		for (int32 i = 0; i < UE_ARRAY_COUNT(ExecPins); ++i)
		{
			TArray<UK2Node*> MacroStack;
			if (UEdGraphPin* NextPin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(ExecPins[i], 0, true, MacroStack, NativizationV2Subsystem))
			{
				UK2Node* NextNode = Cast<UK2Node>(NextPin->GetOwningNode());
				FGenerateFunctionStruct NewGenerateFunctionStruct;
				NewGenerateFunctionStruct.Node = NextNode;
				NewGenerateFunctionStruct.CustomParametersString.Add(FString::Format(TEXT("{0} {1}"), { "const FInputActionInstance&", ActionValuePinName}));
				EntryNodes.Add(NewGenerateFunctionStruct);

				if (FGenerateFunctionStruct* FoundStruct = EntryNodes.FindByKey(NewGenerateFunctionStruct))
				{
					FoundStruct->Name = *UBlueprintNativizationLibrary::GetUniqueEntryNodeName(NextNode, EntryNodes, FString::Format(TEXT("{0}_{1}"), { ExecNames[i], EnhancedInputAction->InputAction->GetName() }));
				}
			}
		}
	}
}


FString UEnhancedInputTranslatorObject::GenerateSetupPlayerInputComponentFunction(UK2Node* InputNode, FString Prefix, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	FString Content;

	if (UK2Node_EnhancedInputAction* EnhancedInputAction = Cast<UK2Node_EnhancedInputAction>(InputNode))
	{
		const FString ExecNames[] = { TEXT("Triggered"), TEXT("Started"), TEXT("Ongoing"), TEXT("Canceled"), TEXT("Completed") };

		UEdGraphPin* ExecPins[] = {
		EnhancedInputAction->FindPin(TEXT("Triggered")),
		EnhancedInputAction->FindPin(TEXT("Started")),
		EnhancedInputAction->FindPin(TEXT("Ongoing")),
		EnhancedInputAction->FindPin(TEXT("Canceled")),
		EnhancedInputAction->FindPin(TEXT("Completed"))
		};

		TArray<UK2Node*> MacroStack;
		MacroStack.Empty();
		bool HaveAnyNonKnotExecPin = false;
		for (int32 i = 0; i < UE_ARRAY_COUNT(ExecPins); ++i)
		{
			if (UBlueprintNativizationLibrary::GetFirstNonKnotPin(ExecPins[i], 0, true, MacroStack, NativizationV2Subsystem))
			{
				HaveAnyNonKnotExecPin = true;
				break;
			}
		}
		if (!HaveAnyNonKnotExecPin)
		{
			return Content;
		}

		Content += "if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) \n {\n";

		for (int32 i = 0; i < UE_ARRAY_COUNT(ExecPins); ++i)
		{
			MacroStack.Empty();
			if (UEdGraphPin* NextPin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(ExecPins[i], 0, true, MacroStack, NativizationV2Subsystem))
			{
				UK2Node* NextNode = Cast<UK2Node>(NextPin->GetOwningNode());

				FGenerateFunctionStruct GenerateFunctionStruct;
				UBlueprintNativizationDataLibrary::FindGenerateFunctionStructByNode(NativizationV2Subsystem->EntryNodes, NextNode, GenerateFunctionStruct);

				Content += FString::Format(TEXT("EnhancedInputComponent->BindAction({0}, ETriggerEvent::{1}, this, &{2}::{3});\n"),
					{ UBlueprintNativizationLibrary::GetLambdaUniqueVariableNameByClass(EnhancedInputAction->InputAction->GetName(), InputNode->GetBlueprint()->GeneratedClass),
					ExecNames[i],
					UBlueprintNativizationLibrary::GetUniqueFieldName(InputNode->GetBlueprint()->GeneratedClass),
					GenerateFunctionStruct.Name.ToString()
					});
			}
		}
		Content += "};\n";
	}
	return Content;
}

FString UEnhancedInputTranslatorObject::GenerateGlobalVariables(UK2Node* InputNode, FString Prefix, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	FString Content;

	if (UK2Node_EnhancedInputAction* EnhancedInputAction = Cast<UK2Node_EnhancedInputAction>(InputNode))
	{
		Content += FString::Format(TEXT("UPROPERTY(EditAnywhere, BlueprintReadOnly) \n {0} {1};\n"),
			{UBlueprintNativizationLibrary::GetUniqueFieldName(EnhancedInputAction->InputAction->GetClass()) + "*",
			UBlueprintNativizationLibrary::GetLambdaUniqueVariableNameByClass(EnhancedInputAction->InputAction->GetName(), InputNode->GetBlueprint()->GeneratedClass)});
	}
	return Content;
}

FString UEnhancedInputTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	if (UK2Node_EnhancedInputAction* EnhancedInputAction = Cast<UK2Node_EnhancedInputAction>(Node))
	{
		UEdGraphPin* ParentPin = UBlueprintNativizationLibrary::GetParentPin(Pin);
		UEdGraphPin* TriggeredSecondsPin = EnhancedInputAction->FindPin(TEXT("TriggeredSeconds"));
		UEdGraphPin* ElapsedSecondsPin = EnhancedInputAction->FindPin(TEXT("ElapsedSeconds"));
		UEdGraphPin* InputActionPin = EnhancedInputAction->FindPin(TEXT("InputAction"));

		UEdGraphPin* ActionValuePin = EnhancedInputAction->FindPin(TEXT("ActionValue"));
		FString ActionValuePinName = UBlueprintNativizationLibrary::GetUniquePinName(ActionValuePin, NativizationV2Subsystem->EntryNodes);

		if (InputActionPin == Pin)
		{
			return ActionValuePinName;
		}
		if (TriggeredSecondsPin == Pin)
		{
			return FString::Format(
				TEXT("{0}.GetTriggeredTime()"), 
				{ ActionValuePinName }
			);
		}
		else if (ElapsedSecondsPin == Pin)
		{
			return FString::Format(
				TEXT("{0}.GetElapsedTime()"),
				{ ActionValuePinName }
			);

		}
		else
		{ 
			TArray<UEdGraphPin*> PathPins = UBlueprintNativizationLibrary::GetParentPathPins(Pin);
			Algo::Reverse(PathPins);
			if (PathPins.IsValidIndex(0))
			{
				PathPins.RemoveAt(0);
			}

			FString Value = FString::Format(TEXT("{0}.GetValue().Get<{1}>()"), { ActionValuePinName, UBlueprintNativizationLibrary::GetPinType(ParentPin->PinType, false)});
			
			for (UEdGraphPin* CheckPin : PathPins)
			{
				FString PinName = CheckPin->GetName();

				if (PinName == TEXT("ActionValue_X"))
				{
					PinName = TEXT("X");
				}
				else if (PinName == TEXT("ActionValue_Y"))
				{
					PinName = TEXT("Y");
				}
				else if (PinName == TEXT("ActionValue_Z"))
				{
					PinName = TEXT("Z");
				}

				Value += TEXT(".") + PinName;
			}
			return Value;
		}
	}

	return "";
}

bool UEnhancedInputTranslatorObject::CanContinueGenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	return false;
}

TSet<FString> UEnhancedInputTranslatorObject::GenerateHeaderIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	return TSet<FString>();
}

TSet<FString> UEnhancedInputTranslatorObject::GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	TSet<FString> HeaderIncludes;
	HeaderIncludes.Add(TEXT("#include \"EnhancedInputComponent.h\""));
	return HeaderIncludes;
}

TSet<FString> UEnhancedInputTranslatorObject::GenerateCSPrivateDependencyModuleNames(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	TSet<FString> PrivateDependencyModules;
	PrivateDependencyModules.Add("\"EnhancedInput\"");
	return PrivateDependencyModules;
}
