/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/VariableSetTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"

FString UVariableSetTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, TSet<FString>& Preparations, UNativizationV2Subsystem* NativizationV2Subsystem)
{
    UK2Node_VariableSet* VarSetNode = Cast<UK2Node_VariableSet>(Node);
    if (!VarSetNode) return FString();
    FString VarName = VarSetNode->GetVarNameString();
    TArray<UEdGraphPin*> Pins = UBlueprintNativizationLibrary::GetFilteredPins(VarSetNode, EPinOutputOrInputFilter::Input, EPinExcludeFilter::ExecPin, EPinIncludeOnlyFilter::None);
    FGenerateResultStruct AssignedValue = NativizationV2Subsystem->GenerateInputParameterCodeForNode(VarSetNode, Pins[0], 0, MacroStack);
    FString Content;
	Content += GenerateNewPreparations(Preparations, AssignedValue.Preparations);
	Preparations.Append(AssignedValue.Preparations);
    Content += FString::Format(TEXT("{0} = {1};\n"), { *VarName, *AssignedValue.Code });
    return Content;
}

FGenerateResultStruct UVariableSetTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
    if (UK2Node_VariableSet* VariableSet = Cast<UK2Node_VariableSet>(Node))
    {
        return FString::Format(TEXT("{0}"), { *VariableSet->GetVarName().ToString() });
    }

    return FGenerateResultStruct();
}
