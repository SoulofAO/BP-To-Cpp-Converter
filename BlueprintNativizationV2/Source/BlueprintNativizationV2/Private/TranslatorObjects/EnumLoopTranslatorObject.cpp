#include "TranslatorObjects/EnumLoopTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"
#include "EdGraphSchema_K2.h"

FGenerateResultStruct UEnumLoopTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin,
    int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem)
{
    if (UK2Node_GetNumEnumEntries* Count = Cast<UK2Node_GetNumEnumEntries>(Node))
    {
        return FString::FromInt(FMath::Max(0, Count->Enum->NumEnums() - 1));
    }
    return UBlueprintNativizationLibrary::GetUniquePinName(Pin, Subsystem->EntryNodes);
}

FString UEnumLoopTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName,
    TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations,
    UNativizationV2Subsystem* Subsystem)
{
    UK2Node_ForEachElementInEnum* Loop = CastChecked<UK2Node_ForEachElementInEnum>(Node);
    const FString Counter = Subsystem->GetNodeStateName(Node, TEXT("EnumIndex"), true);
    UEdGraphPin* Output = Node->FindPinChecked(UK2Node_ForEachElementInEnum::EnumOuputPinName);
    const FString Value = UBlueprintNativizationLibrary::GetUniquePinName(Output, Subsystem->EntryNodes);
    const FString Type = UBlueprintNativizationLibrary::GetPinType(Output->PinType, false);
    FString Content;
    if (!UBlueprintNativizationLibrary::CheckAnySubPinLinked(Output))
    {
        Content += Type + TEXT(" ") + Value + TEXT("{};\n");
    }
    const int32 Count = FMath::Max(0, Loop->Enum->NumEnums() - 1);
    TArray<FString> CheckedValues;
    for (int32 Index = 0; Index < Count; ++Index)
    {
        uint8 CheckedValue = static_cast<uint8>(Index);
        if (!Loop->Enum->IsValidEnumValue(CheckedValue))
        {
            CheckedValue = static_cast<uint8>(Loop->Enum->GetMaxEnumValue());
        }
        CheckedValues.Add(FString::FromInt(CheckedValue));
    }
    const FString Values = Subsystem->GetNodeStateName(Node, TEXT("EnumValues"), true);
    if (Count > 0)
    {
        Content += TEXT("const uint8 ") + Values + TEXT("[] = {") + FString::Join(CheckedValues, TEXT(", ")) + TEXT("};\n");
    }
    Content += FString::Format(TEXT("for (int32 {0} = 0; {0} < {1}; ++{0})\n{\n"), {Counter, Count});
    if (Count > 0)
    {
        Content += Value + TEXT(" = static_cast<") + Type + TEXT(">(") + Values + TEXT("[") + Counter + TEXT("]);\n");
    }
    TArray<FString> Hidden;
    for (int32 Index = 0; Index < Count; ++Index)
    {
        if (Loop->Enum->HasMetaData(TEXT("Hidden"), Index) || Loop->Enum->HasMetaData(TEXT("Spacer"), Index))
        {
            Hidden.Add(TEXT("static_cast<int64>(") + Value + TEXT(") == ") + FString::Format(TEXT("{0}"), {Loop->Enum->GetValueByIndex(Index)}));
        }
    }
    if (!Hidden.IsEmpty())
    {
        const FGenerateResultStruct Skip = Subsystem->GenerateInputParameterCodeForNode(Node, Node->FindPinChecked(UK2Node_ForEachElementInEnum::SkipHiddenPinName), 0, MacroStack);
        Content += FString::Join(Skip.Preparations.Array(), TEXT("\n"));
        Content += TEXT("if ((") + Skip.Code + TEXT(") && (") + FString::Join(Hidden, TEXT(" || ")) + TEXT("))\n{\ncontinue;\n}\n");
    }
    TArray<UK2Node*> BodyPath = MacroStack;
    if (UEdGraphPin* Target = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Node->FindPinChecked(UK2Node_ForEachElementInEnum::InsideLoopPinName), 0, false, BodyPath, Subsystem))
    {
        Content += Subsystem->GenerateCodeFromNode(CastChecked<UK2Node>(Target->GetOwningNode()), Target->GetName(), VisitedNodes, Preparations, BodyPath);
    }
    Content += TEXT("}\n");
    if (UEdGraphPin* Target = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Node->FindPin(UEdGraphSchema_K2::PN_Then), 0, false, MacroStack, Subsystem))
    {
        Content += Subsystem->GenerateCodeFromNode(CastChecked<UK2Node>(Target->GetOwningNode()), Target->GetName(), VisitedNodes, Preparations, MacroStack);
    }
    return Content;
}
