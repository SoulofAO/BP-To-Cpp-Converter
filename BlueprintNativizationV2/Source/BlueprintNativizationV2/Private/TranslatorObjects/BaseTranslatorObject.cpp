/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright 2025 AO All Rights Reserved.
 */

#include "TranslatorObjects/BaseTranslatorObject.h"
#include "EdGraphSchema_K2.h"
#include "BlueprintNativizationLibrary.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "BlueprintNativizationSubsystem.h"
#include "GenerationGraphContext.h"
#include "BlueprintNativizationUniqueNameSubsystem.h"
#include "Engine/Engine.h"

void UTranslatorBPToCppObject::GenerateNewFunction(UK2Node* InputNode, TArray<UK2Node*> Path, TArray<FGenerateFunctionStruct>& EntryNodes, UNativizationV2Subsystem* NativizationV2Subsystem)
{
}

FString UTranslatorBPToCppObject::GenerateGlobalVariables(UK2Node* InputNode, FString Prefix, UNativizationV2Subsystem* NativizationV2Subsystem)
{
    if (InputNode->IsNodePure() || FGenerationGraphContext::IsFunctionGraph(InputNode))
    {
        return FString();
    }
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

TSet<FString> UTranslatorBPToCppObject::GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem)
{
    TSet<FString> Includes;
    for (UEdGraphPin* Pin : Node->Pins)
    {
        for (UObject* TypeObject : { Pin->PinType.PinSubCategoryObject.Get(), Pin->PinType.PinValueType.TerminalSubCategoryObject.Get() })
        {
            UClass* PinClass = Cast<UClass>(TypeObject);
            if (PinClass && PinClass->ClassGeneratedBy == Node->GetBlueprint())
            {
                continue;
            }
            UField* Field = Cast<UField>(TypeObject);
            FString IncludePath;
            if (Field && Subsystem->GetIncludeFromField(Field, IncludePath))
            {
                Includes.Add(IncludePath);
            }
        }
    }
    return Includes;
}

TSet<FString> UTranslatorBPToCppObject::GenerateCSPrivateDependencyModuleNames(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	return TSet<FString>();
}

FGenerateResultStruct UTranslatorBPToCppObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	return FGenerateResultStruct(UBlueprintNativizationLibrary::GetUniquePinName(Pin, NativizationV2Subsystem->EntryNodes));
}

bool UTranslatorBPToCppObject::CanContinueGenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	return true;
}

FString UTranslatorBPToCppObject::GenerateLocalFunctionVariablesToHeaderVariables(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	FString Content;
    if ((Node->IsA<UK2Node_Event>() || Node->IsA<UK2Node_FunctionEntry>()) && NativizationV2Subsystem->EntryNodes.ContainsByPredicate([Node](const FGenerateFunctionStruct& Entry)
    {
        return Entry.Node == Node;
    }))
    {
        return Content;
    }
	TArray<UEdGraphPin*> OutputPins = UBlueprintNativizationLibrary::GetFilteredPins(Node, EPinOutputOrInputFilter::Ouput, EPinExcludeFilter::ExecPin | EPinExcludeFilter::DelegatePin, EPinIncludeOnlyFilter::None);
	for (UEdGraphPin* OutputPin : OutputPins)
	{

		if (OutputPin->LinkedTo.Num() > 0 && !UBlueprintNativizationLibrary::IsPinAllLinkInCurrentFunctionNode(OutputPin, Node, NativizationV2Subsystem->EntryNodes))
		{
			FString PinType = UBlueprintNativizationLibrary::GetPinType(OutputPin->PinType, false);
			FString VarName = UBlueprintNativizationLibrary::GetUniquePinName(OutputPin, NativizationV2Subsystem->EntryNodes);

			Content += TEXT("UPROPERTY(Transient, DuplicateTransient, BlueprintReadOnly)\n");
			Content += FString::Format(TEXT("{0} {1};\n\n"), { *PinType, *VarName });
		}
	}
	return Content;
}

TSet<FString> UTranslatorBPToCppObject::GenerateLocalFunctionVariables(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	TSet<FString> Contents;
    if ((Node->IsA<UK2Node_Event>() || Node->IsA<UK2Node_FunctionEntry>()) && NativizationV2Subsystem->EntryNodes.ContainsByPredicate([Node](const FGenerateFunctionStruct& Entry)
    {
        return Entry.Node == Node;
    }))
    {
        return Contents;
    }


	TArray<UEdGraphPin*> OutputPins = UBlueprintNativizationLibrary::GetFilteredPins(Node, EPinOutputOrInputFilter::Ouput, EPinExcludeFilter::ExecPin, EPinIncludeOnlyFilter::None);
	OutputPins = UBlueprintNativizationLibrary::GetParentPins(OutputPins);
	for (UEdGraphPin* OutputPin : OutputPins)
	{

        if (GEngine->GetEngineSubsystem<UBlueprintNativizationUniqueNameSubsystem>()->IsPersistentPinVariable(OutputPin))
        {
            continue;
        }
		if (!Node->IsNodePure() && UBlueprintNativizationLibrary::CheckAnySubPinLinked(OutputPin) && (FGenerationGraphContext::IsFunctionGraph(Node) || UBlueprintNativizationLibrary::IsPinAllLinkInCurrentFunctionNode(OutputPin, Node, NativizationV2Subsystem->EntryNodes)))
		{
			if (!CanGenerateOutPinVariable(OutputPin, NativizationV2Subsystem))
			{
				continue;
			}

			FGenerateFunctionStruct GenerateFunctionStruct;
			UBlueprintNativizationDataLibrary::FindGenerateFunctionStructByNode(NativizationV2Subsystem->EntryNodes, Node, GenerateFunctionStruct);
			FString VarName = UBlueprintNativizationLibrary::GetUniquePinName(OutputPin, NativizationV2Subsystem->EntryNodes);

			Contents.Add(FString::Format(TEXT("{0} {1} = {2};\n"), { *UBlueprintNativizationLibrary::GetPinType(OutputPin->PinType, false), *VarName, UBlueprintNativizationLibrary::GenerateOneLineDefaultConstructor(OutputPin->GetName(), OutputPin->PinType, OutputPin->DefaultValue, OutputPin->DefaultObject) }));
		}
	}

	TArray<UEdGraphPin*> InputPins = UBlueprintNativizationLibrary::GetFilteredPins(Node, EPinOutputOrInputFilter::Input, EPinExcludeFilter::ExecPin, EPinIncludeOnlyFilter::None);
	InputPins = UBlueprintNativizationLibrary::GetParentPins(InputPins);

	for (UEdGraphPin* InputPin : InputPins)
	{
        if (InputPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct && !InputPin->PinType.IsContainer() && InputPin->DefaultValue.IsEmpty())
        {
            continue;
        }
		if (!UBlueprintNativizationLibrary::CheckAnySubPinLinked(InputPin))
		{
			FGenerateFunctionStruct GenerateFunctionStruct;
			UBlueprintNativizationDataLibrary::FindGenerateFunctionStructByNode(NativizationV2Subsystem->EntryNodes, Node, GenerateFunctionStruct);
			FString VarName = UBlueprintNativizationLibrary::GetUniquePinName(InputPin, NativizationV2Subsystem->EntryNodes);

			if (!UBlueprintNativizationLibrary::IsAvailibleOneLineDefaultConstructor(InputPin->PinType))
			{
				FString OutString;
				Contents.Add(FString::Format(TEXT("{0} {1};\n"), { *UBlueprintNativizationLibrary::GetPinType(InputPin->PinType, true), *VarName }));
				Contents.Add(UBlueprintNativizationLibrary::GenerateManyLinesDefaultConstructor(VarName, InputPin->PinType, InputPin->DefaultValue, Node->GetBlueprint()->GeneratedClass, false, Node, NativizationV2Subsystem->EntryNodes, false, OutString));
			}

		}
	}

	return Contents;
}

FString UTranslatorBPToCppObject::GenerateNewPreparations(FGenerationPreparations OldPreparations, FGenerationPreparations NewPreparations)
{
	FGenerationPreparations DifferencePreparations = NewPreparations.Difference(OldPreparations);
	FString JoinedString = FString::Join(DifferencePreparations.Array(), TEXT("\n"));
	if (!JoinedString.IsEmpty())
	{
		JoinedString = JoinedString + "\n";
	}
	return JoinedString;
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

			if (NodeNamesForCheck.Contains(Name))
			{
				return true;
			}
		}
	}
	return false;
}
