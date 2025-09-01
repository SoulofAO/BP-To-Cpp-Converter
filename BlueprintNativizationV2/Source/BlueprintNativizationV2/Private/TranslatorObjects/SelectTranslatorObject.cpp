/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/SelectTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"

FString USelectTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
    if (UK2Node_Select* SelectNode = Cast<UK2Node_Select>(Node))
    {
        // ���������� ��� ��������
        FName ElementCppType = *UBlueprintNativizationLibrary::GetPinType(Pin->PinType, false);

        // �������� ��� ������� ���� (����� Exec � ������� ������)
        TArray<UEdGraphPin*> InputPins = UBlueprintNativizationLibrary::GetFilteredPins(
            SelectNode,
            EPinOutputOrInputFilter::Input,
            EPinExcludeFilter::None,
            EPinIncludeOnlyFilter::None
        );

        // ��������� OptionPins
        TArray<FString> Elements;
        for (UEdGraphPin* InputPin : InputPins)
        {
            // ���������� IndexPin (��� ��������)
            if (InputPin->PinName == SelectNode->GetIndexPin()->PinName)
                continue;

            Elements.Add(NativizationV2Subsystem->GenerateInputParameterCodeForNode(SelectNode, InputPin, 0, MacroStack));
        }

        // ��� ��� �������
        FString IndexCode = NativizationV2Subsystem->GenerateInputParameterCodeForNode(SelectNode, SelectNode->GetIndexPin(), 0, MacroStack);

        FString ElementsJoined;
        for (int32 i = 0; i < Elements.Num(); ++i)
        {
            if (i > 0) ElementsJoined += TEXT(", ");
            ElementsJoined += Elements[i];
        }

        return FString::Format(TEXT("TArray<{0}>{{1}}[{2}]"), {
            ElementCppType.ToString(),
            ElementsJoined,
            IndexCode
            });
    }

    return "";
}
