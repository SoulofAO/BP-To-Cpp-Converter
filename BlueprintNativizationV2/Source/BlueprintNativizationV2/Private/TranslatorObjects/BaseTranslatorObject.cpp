/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/BaseTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "K2Node_MacroInstance.h"
#include "BlueprintNativizationSubsystem.h"

void UTranslatorBPToCppObject::GenerateNewFunction(UK2Node* InputNode, TArray<UK2Node*> Path, TArray<FGenerateFunctionStruct>& EntryNodes, UNativizationV2Subsystem* NativizationV2Subsystem)
{
}

FString UTranslatorBPToCppObject::GenerateGlobalVariables(UK2Node* InputNode, FString Prefix, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	return GenerateLocalFunctionVariablesToHeaderVariables(InputNode, NativizationV2Subsystem);
}

TSet<FString> UTranslatorBPToCppObject::GenerateLocalVariables(UK2Node* InputNode, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	return GenerateLocalFunctionVariables(InputNode, NativizationV2Subsystem);
}

bool UTranslatorBPToCppObject::CanContinueCodeGeneration(UK2Node* InputNode, FString EntryPinName)
{
	return true;
}

TSet<FString> UTranslatorBPToCppObject::GenerateHeaderIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	return TSet<FString>();
}

TSet<FString> UTranslatorBPToCppObject::GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	return TSet<FString>();
}

TSet<FString> UTranslatorBPToCppObject::GenerateCSPrivateDependencyModuleNames(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	return TSet<FString>();
}

FString UTranslatorBPToCppObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	return UBlueprintNativizationLibrary::GetUniquePinName(Pin, NativizationV2Subsystem->EntryNodes);
}

bool UTranslatorBPToCppObject::CanContinueGenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	return true;
}

FString UTranslatorBPToCppObject::GenerateLocalFunctionVariablesToHeaderVariables(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	FString Content;
	TArray<UEdGraphPin*> OutputPins = UBlueprintNativizationLibrary::GetFilteredPins(Node, EPinOutputOrInputFilter::Ouput, EPinExcludeFilter::ExecPin | EPinExcludeFilter::DelegatePin, EPinIncludeOnlyFilter::None);
	for (UEdGraphPin* OutputPin : OutputPins)
	{

		if (OutputPin->LinkedTo.Num() > 0 && !UBlueprintNativizationLibrary::IsPinAllLinkInCurrentFunctionNode(OutputPin, Node, NativizationV2Subsystem->EntryNodes))
		{
			FString PinType = UBlueprintNativizationLibrary::GetPinType(OutputPin->PinType, false);
			FString VarName = UBlueprintNativizationLibrary::GetUniquePinName(OutputPin, NativizationV2Subsystem->EntryNodes);

			Content += TEXT("UPROPERTY(BlueprintReadOnly)\n");
			Content += FString::Format(TEXT("{0} {1};\n\n"), { *PinType, *VarName });
		}
	}
	return Content;
}

TSet<FString> UTranslatorBPToCppObject::GenerateLocalFunctionVariables(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	TSet<FString> Contents;

	TArray<UEdGraphPin*> OutputPins = UBlueprintNativizationLibrary::GetFilteredPins(Node, EPinOutputOrInputFilter::Ouput, EPinExcludeFilter::ExecPin, EPinIncludeOnlyFilter::None);
	OutputPins = UBlueprintNativizationLibrary::GetParentPins(OutputPins);
	for (UEdGraphPin* OutputPin : OutputPins)
	{
		if (UBlueprintNativizationLibrary::CheckAnySubPinLinked(OutputPin) && UBlueprintNativizationLibrary::IsPinAllLinkInCurrentFunctionNode(OutputPin, Node, NativizationV2Subsystem->EntryNodes))
		{
			if (!CanGenerateOutPinVariable(OutputPin, NativizationV2Subsystem))
			{
				continue;
			}

			FGenerateFunctionStruct GenerateFunctionStruct;
			UBlueprintNativizationDataLibrary::FindGenerateFunctionStructByNode(NativizationV2Subsystem->EntryNodes, Node, GenerateFunctionStruct);
			FString VarName = UBlueprintNativizationLibrary::GetUniquePinName(OutputPin, NativizationV2Subsystem->EntryNodes);

			Contents.Add(FString::Format(TEXT("{0} {1} = {2};\n"), { *UBlueprintNativizationLibrary::GetPinType(OutputPin->PinType, true), *VarName, UBlueprintNativizationLibrary::GenerateOneLineDefaultConstructor(OutputPin->GetName(), OutputPin->PinType, OutputPin->DefaultValue, OutputPin->DefaultObject, NativizationV2Subsystem->LeftAllAssetRefInBlueprint) }));
		}
	}

	TArray<UEdGraphPin*> InputPins = UBlueprintNativizationLibrary::GetFilteredPins(Node, EPinOutputOrInputFilter::Input, EPinExcludeFilter::ExecPin, EPinIncludeOnlyFilter::None);
	InputPins = UBlueprintNativizationLibrary::GetParentPins(InputPins);

	for (UEdGraphPin* InputPin : InputPins)
	{
		if (!UBlueprintNativizationLibrary::CheckAnySubPinLinked(InputPin))
		{
			FGenerateFunctionStruct GenerateFunctionStruct;
			UBlueprintNativizationDataLibrary::FindGenerateFunctionStructByNode(NativizationV2Subsystem->EntryNodes, Node, GenerateFunctionStruct);
			FString VarName = UBlueprintNativizationLibrary::GetUniquePinName(InputPin, NativizationV2Subsystem->EntryNodes);

			if (!UBlueprintNativizationLibrary::IsAvailibleOneLineDefaultConstructor(InputPin->PinType))
			{
				FString OutString;
				Contents.Add(FString::Format(TEXT("{0} {1};\n"), { *UBlueprintNativizationLibrary::GetPinType(InputPin->PinType, true), *VarName }));
				Contents.Add(UBlueprintNativizationLibrary::GenerateManyLinesDefaultConstructor(VarName, InputPin->PinType, InputPin->DefaultValue, Node->GetBlueprint()->GeneratedClass, false, Node, NativizationV2Subsystem->EntryNodes, false, NativizationV2Subsystem->LeftAllAssetRefInBlueprint, OutString));
			}

		}
	}

	return Contents;
}

bool UTranslatorBPToCppObject::CanApply(UK2Node* Node)
{
	if (Node == nullptr)
	{
		return false;
	}

	bool Sucsess = false;
	for (const TSubclassOf<UK2Node>& ClassNode : ClassNodeForCheck)
	{
		if (Node->GetClass()->IsChildOf(ClassNode) || Node->GetClass() == ClassNode)
		{
			Sucsess = true;
		}
	}

	if (Sucsess)
	{
		if (NodeNamesForCheck.IsEmpty())
		{
			return true;
		}
		else
		{
			FString Name = UBlueprintNativizationLibrary::GetFunctionNameByInstanceNode(Node);
			if (UK2Node_MacroInstance* MacroInstance = Cast<UK2Node_MacroInstance>(Node))
			{
				Name = MacroInstance->GetMacroGraph()->GetName();
			}

			if (NodeNamesForCheck.Contains(Name))
			{
				return true;
			}
		}
	}
	return false;
}

