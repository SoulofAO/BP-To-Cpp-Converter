/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/MultiGateTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "GenerationGraphContext.h"
#include "BlueprintNativizationSubsystem.h"
#include "Engine/Blueprint.h"


FString UMultiGateTranslatorObject::GenerateGlobalVariables(UK2Node* InputNode, FString Prefix, UNativizationV2Subsystem* Subsystem)
{
    if (FGenerationGraphContext::IsFunctionGraph(InputNode))
    {
        return FString();
    }
    const FString StateName = Subsystem->GetNodeStateName(InputNode, TEXT("MultiGateState"));
    return FString::Format(TEXT("int32 {0} = 0;\nbool b{0}Started = false;\n"), { StateName });
}

TSet<FString> UMultiGateTranslatorObject::GenerateLocalVariables(UK2Node* InputNode, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem)
{
    if (FGenerationGraphContext::IsFunctionGraph(InputNode))
    {
        return { FString::Format(TEXT("int32 {0} = 0;\nbool b{0}Started = false;\n"), { Subsystem->GetNodeStateName(InputNode, TEXT("MultiGateState")) }) };
    }
    return {};
}

TSet<FString> UMultiGateTranslatorObject::GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem)
{
    return { TEXT("#include \"Kismet/KismetNodeHelperLibrary.h\"") };
}

FString UMultiGateTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName,
    TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations,
    UNativizationV2Subsystem* Subsystem)
{
    UK2Node_MultiGate* Gate = CastChecked<UK2Node_MultiGate>(Node);
    const FString StateName = Subsystem->GetNodeStateName(Node, TEXT("MultiGateState"));
    if (EntryPinName == TEXT("Reset"))
    {
        return FString::Format(TEXT("{0} = 0;\n"), { StateName });
    }

    const FGenerateResultStruct StartIndex = Subsystem->GenerateInputParameterCodeForNode(Node, Gate->FindPinChecked(TEXT("StartIndex")), 0, MacroStack);
    const FGenerateResultStruct Random = Subsystem->GenerateInputParameterCodeForNode(Node, Gate->FindPinChecked(TEXT("IsRandom")), 0, MacroStack);
    const FGenerateResultStruct Loop = Subsystem->GenerateInputParameterCodeForNode(Node, Gate->FindPinChecked(TEXT("Loop")), 0, MacroStack);
    FGenerationPreparations LocalPreparations = Preparations;
    LocalPreparations.Append(StartIndex.Preparations);
    LocalPreparations.Append(Random.Preparations);
    LocalPreparations.Append(Loop.Preparations);
    FString Content = GenerateNewPreparations(Preparations, LocalPreparations);
    TArray<UEdGraphPin*> Outputs = UBlueprintNativizationLibrary::GetFilteredPins(Node,
        EPinOutputOrInputFilter::Ouput, EPinExcludeFilter::None, EPinIncludeOnlyFilter::ExecPin);
    const FString IndexName = UBlueprintNativizationLibrary::GenerateLambdaConstructorUniqueVariableName(
        Node->GetBlueprint()->GeneratedClass, TEXT("MultiGateIndex"));
    Content += FString::Format(TEXT(
        "{\nint32 {0} = INDEX_NONE;\n"
        "if (!b{1}Started)\n{\nb{1}Started = true;\n"
        "{0} = {2};\nif ({0} < 0)\n{\n{0} = UKismetNodeHelperLibrary::GetUnmarkedBit({1}, {2}, {3}, {4});\n}\n}\n"
        "else\n{\nif (!UKismetNodeHelperLibrary::HasUnmarkedBit({1}, {3}) && ({5}))\n{\n{1} = 0;\n}\n"
        "if (UKismetNodeHelperLibrary::HasUnmarkedBit({1}, {3}))\n{\n{0} = UKismetNodeHelperLibrary::GetUnmarkedBit({1}, {2}, {3}, {4});\n}\n}\n"
        "if ({0} >= 0 && {0} < {3})\n{\nUKismetNodeHelperLibrary::MarkBit({1}, {0});\n"
        "switch ({0})\n{\n"), { IndexName, StateName, StartIndex.Code, Outputs.Num(), Random.Code, Loop.Code });
    for (int32 OutputIndex = 0; OutputIndex < Outputs.Num(); ++OutputIndex)
    {
        TArray<UK2Node*> BranchMacroStack = MacroStack;
        UEdGraphPin* ConnectedPin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Outputs[OutputIndex], 0, false, BranchMacroStack, Subsystem);
        FString Branch;
        if (ConnectedPin)
        {
            Branch = Subsystem->GenerateCodeFromNode(Cast<UK2Node>(ConnectedPin->GetOwningNode()), ConnectedPin->GetName(),
                VisitedNodes, LocalPreparations, BranchMacroStack);
        }
        Content += FString::Format(TEXT("case {0}:\n{\n{1}\nbreak;\n}\n"), { OutputIndex, Branch });
    }
    Content += TEXT("}\n}\n}\n");
    return Content;
}
