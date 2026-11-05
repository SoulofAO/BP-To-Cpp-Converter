/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright 2025 AO All Rights Reserved.
 */

#include "TranslatorObjects/MakeMapTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"

FGenerateResultStruct UMakeMapTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex,
    TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem)
{
    UK2Node_MakeMap* Map = CastChecked<UK2Node_MakeMap>(Node);
    TArray<UEdGraphPin*> Keys;
    TArray<UEdGraphPin*> Values;
    Map->GetKeyAndValuePins(Keys, Values);
    TArray<FString> Entries;
    FGenerationPreparations Preparations;
    for (int32 EntryIndex = 0; EntryIndex < Keys.Num(); ++EntryIndex)
    {
        const FGenerateResultStruct Key = Subsystem->GenerateInputParameterCodeForNode(Node, Keys[EntryIndex], 0, MacroStack);
        const FGenerateResultStruct Value = Subsystem->GenerateInputParameterCodeForNode(Node, Values[EntryIndex], 0, MacroStack);
        Preparations.Append(Key.Preparations);
        Preparations.Append(Value.Preparations);
        Entries.Add(FString::Format(TEXT("{ {0}, {1} }"), { Key.Code, Value.Code }));
    }
    return FGenerateResultStruct(FString::Format(TEXT("{0}{ {1} }"), {
        UBlueprintNativizationLibrary::GetPinType(Pin->PinType, false), FString::Join(Entries, TEXT(", ")) }), Preparations);
}
