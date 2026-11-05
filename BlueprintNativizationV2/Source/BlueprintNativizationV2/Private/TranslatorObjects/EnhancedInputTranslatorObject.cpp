/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright 2025 AO All Rights Reserved.
 */

#include "TranslatorObjects/EnhancedInputTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationData.h"
#include "BlueprintNativizationSettings.h"
#include "InputAction.h"
#include "RetainedBlueprintGeneration.h"


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

        const FString ActionValuePinName = NativizationV2Subsystem->GetNodeStateName(InputNode, TEXT("InputActionArgument"));
        const FString ActionState = NativizationV2Subsystem->GetNodeStateName(InputNode, TEXT("InputActionState"));

		for (int32 OutputIndex = 0; OutputIndex < UE_ARRAY_COUNT(ExecPins); ++OutputIndex)
		{
			TArray<UK2Node*> MacroStack = Path;
			if (UEdGraphPin* NextPin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(ExecPins[OutputIndex], 0, false, MacroStack, NativizationV2Subsystem))
			{
				UK2Node* NextNode = Cast<UK2Node>(NextPin->GetOwningNode());
				FGenerateFunctionStruct NewGenerateFunctionStruct;
				NewGenerateFunctionStruct.Node = NextNode;
                NewGenerateFunctionStruct.MacroStack = MacroStack;
                NewGenerateFunctionStruct.EntryPinName = NextPin->GetName();
				NewGenerateFunctionStruct.CustomParametersString.Add(FString::Format(TEXT("{0} {1}"), { "const FInputActionInstance&", ActionValuePinName}));
                NewGenerateFunctionStruct.CustomArguments.Add(ActionState);
                NewGenerateFunctionStruct.EntryInitializationCode = ActionState + TEXT(" = ") + ActionValuePinName + TEXT(";\n");
                NewGenerateFunctionStruct.Name = *NativizationV2Subsystem->GetNodeStateName(InputNode,
                    TEXT("Input") + ExecNames[OutputIndex] + EnhancedInputAction->InputAction->GetName());
                EntryNodes.Add(NewGenerateFunctionStruct);
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
		for (int32 OutputIndex = 0; OutputIndex < UE_ARRAY_COUNT(ExecPins); ++OutputIndex)
		{
			if (UBlueprintNativizationLibrary::GetFirstNonKnotPin(ExecPins[OutputIndex], 0, false, MacroStack, NativizationV2Subsystem))
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

		for (int32 OutputIndex = 0; OutputIndex < UE_ARRAY_COUNT(ExecPins); ++OutputIndex)
		{
			MacroStack.Empty();
			if (UEdGraphPin* NextPin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(ExecPins[OutputIndex], 0, false, MacroStack, NativizationV2Subsystem))
			{
                const FString CallbackName = NativizationV2Subsystem->GetNodeStateName(InputNode,
                    TEXT("Input") + ExecNames[OutputIndex] + EnhancedInputAction->InputAction->GetName());

				const FString InputActionContent = FRetainedBlueprintGeneration::GetAssetValue(
                    const_cast<UInputAction*>(EnhancedInputAction->InputAction.Get()), UInputAction::StaticClass());
				Content += FString::Format(TEXT("EnhancedInputComponent->BindAction({0}, ETriggerEvent::{1}, this, &{2}::{3});\n"),
					{ InputActionContent,
					ExecNames[OutputIndex],
					UBlueprintNativizationLibrary::GetUniqueFieldName(InputNode->GetBlueprint()->GeneratedClass),
					CallbackName
					});
			}
		}
		Content += "};\n";
	}
	return Content;
}

FString UEnhancedInputTranslatorObject::GenerateGlobalVariables(UK2Node* InputNode, FString Prefix, UNativizationV2Subsystem* NativizationV2Subsystem)
{
    return TEXT("UPROPERTY(Transient)\nFInputActionInstance ") +
        NativizationV2Subsystem->GetNodeStateName(InputNode, TEXT("InputActionState")) + TEXT(";\n");
}

FGenerateResultStruct UEnhancedInputTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	if (UK2Node_EnhancedInputAction* EnhancedInputAction = Cast<UK2Node_EnhancedInputAction>(Node))
	{
		UEdGraphPin* ParentPin = UBlueprintNativizationLibrary::GetParentPin(Pin);
		UEdGraphPin* TriggeredSecondsPin = EnhancedInputAction->FindPin(TEXT("TriggeredSeconds"));
		UEdGraphPin* ElapsedSecondsPin = EnhancedInputAction->FindPin(TEXT("ElapsedSeconds"));
		UEdGraphPin* InputActionPin = EnhancedInputAction->FindPin(TEXT("InputAction"));

		UEdGraphPin* ActionValuePin = EnhancedInputAction->FindPin(TEXT("ActionValue"));
        const FString ActionValuePinName = NativizationV2Subsystem->GetNodeStateName(Node, TEXT("InputActionState"));

		if (InputActionPin == Pin)
		{
            return FRetainedBlueprintGeneration::GetAssetValue(const_cast<UInputAction*>(EnhancedInputAction->InputAction.Get()), UInputAction::StaticClass());
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

			FString ValueType = UBlueprintNativizationLibrary::GetPinType(ParentPin->PinType, false);
            if (ValueType == TEXT("double"))
            {
                ValueType = TEXT("float");
            }
            const FString ActionType = StaticEnum<EInputActionValueType>()->GetNameStringByValue(static_cast<int64>(EnhancedInputAction->InputAction->ValueType));
            FString Value = FString::Format(TEXT("{0}.GetValue().ConvertToType(EInputActionValueType::{2}).Get<{1}>()"), { ActionValuePinName, ValueType, ActionType });
			
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

	return FGenerateResultStruct();
}

bool UEnhancedInputTranslatorObject::CanContinueGenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	return true;
}

TSet<FString> UEnhancedInputTranslatorObject::GenerateHeaderIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem)
{
    return { TEXT("#include \"InputAction.h\"") };
}

TSet<FString> UEnhancedInputTranslatorObject::GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	TSet<FString> HeaderIncludes;
	HeaderIncludes.Add(TEXT("#include \"EnhancedInputComponent.h\""));
	HeaderIncludes.Add(TEXT("#include \"InputAction.h\""));
	HeaderIncludes.Add(TEXT("#include \"InputMappingContext.h\""));
	return HeaderIncludes;
}

TSet<FString> UEnhancedInputTranslatorObject::GenerateCSPrivateDependencyModuleNames(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	TSet<FString> PrivateDependencyModules;
	PrivateDependencyModules.Add("\"EnhancedInput\"");
	return PrivateDependencyModules;
}
