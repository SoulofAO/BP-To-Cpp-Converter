/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/SelectTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"

FGenerateResultStruct USelectTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex,
    TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem)
{
    UK2Node_Select* Select = CastChecked<UK2Node_Select>(Node);
    const FString ValueType = UBlueprintNativizationLibrary::GetPinType(Pin->PinType, false);
    const FGenerateResultStruct Index = Subsystem->GenerateInputParameterCodeForNode(Node, Select->GetIndexPin(), 0, MacroStack);
    const FString IndexName = Subsystem->GetNodeStateName(Node, TEXT("SelectIndex"));
    const FString ResultName = Subsystem->GetNodeStateName(Node, TEXT("SelectedValue"), true);
    FString Content = FString::Format(TEXT("{0} {1}{};\n{\nconst auto {2} = {3};\n"),
        { ValueType, ResultName, IndexName, Index.Code });
    TArray<UEdGraphPin*> Options;
    Select->GetOptionPins(Options);
    FGenerationPreparations EmittedPreparations = Index.Preparations;
    TArray<FString> OptionNames;
    for (int32 OptionIndex = 0; OptionIndex < Options.Num(); ++OptionIndex)
    {
        const FGenerateResultStruct Value = Subsystem->GenerateInputParameterCodeForNode(Node, Options[OptionIndex], 0, MacroStack);
        const FString OptionName = Subsystem->GetNodeStateName(Node, FString::Format(TEXT("SelectOption{0}"), { OptionIndex }));
        Content += FString::Format(TEXT("{0} const {1} = {2};\n"),
            { ValueType, OptionName, Value.Code });
        EmittedPreparations.Append(Value.Preparations);
        OptionNames.Add(OptionName);
    }
    Content += FString::Format(TEXT("switch (static_cast<int64>({0}))\n{\n"), { IndexName });
    for (int32 OptionIndex = 0; OptionIndex < Options.Num(); ++OptionIndex)
    {
        int64 OptionValue = OptionIndex;
        if (const UEnum* Enumeration = Select->GetEnum())
        {
            OptionValue = Enumeration->GetValueByName(Options[OptionIndex]->PinName);
        }
        Content += FString::Format(TEXT("case {0}:\n{\n{1} = {2};\nbreak;\n}\n"), { OptionValue, ResultName, OptionNames[OptionIndex] });
    }
    Content += TEXT("default:\n{\nbreak;\n}\n}\n}\n");
    EmittedPreparations.Add(Content);
    return FGenerateResultStruct(ResultName, EmittedPreparations);
}
