/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/SwitchTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_SwitchString.h"
#include "K2Node_SwitchName.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_Switch.h"

FString USwitchTranslatorObject::GenerateCodeFromNode(
    UK2Node* Node,
    FString EntryPinName,
    TArray<FVisitedNodeStack> VisitedNodes,
    TArray<UK2Node*> MacroStack,
    UNativizationV2Subsystem* NativizationV2Subsystem)
{
    FString Content;

    UK2Node_Switch* SwitchNode = Cast<UK2Node_Switch>(Node);
    if (!SwitchNode) return Content;

    UEdGraphPin* DefaultPin = SwitchNode->GetDefaultPin();
    UEdGraphPin* SelectionPin = SwitchNode->GetSelectionPin();
    FString SelectionExpr = TEXT("/*Selection*/0");

    if (SelectionPin)
    {
        SelectionExpr = NativizationV2Subsystem->GenerateInputParameterCodeForNode(SwitchNode, SelectionPin, 0, MacroStack);
    }

    // �������� ��� �������� ����, ����� default
    TArray<UEdGraphPin*> OptionPins = UBlueprintNativizationLibrary::GetFilteredPins(Node, EPinOutputOrInputFilter::Ouput, EPinExcludeFilter::None, EPinIncludeOnlyFilter::ExecPin);
    OptionPins.Remove(DefaultPin);

    bool bIsStringLikeSwitch = Cast<UK2Node_SwitchString>(SwitchNode) != nullptr || Cast<UK2Node_SwitchName>(SwitchNode) !=nullptr;

    if (bIsStringLikeSwitch)
    {
        // === ��������� Switch on String ����� if/else if ===
        bool bFirst = true;

        for (UEdGraphPin* CasePin : OptionPins)
        {
            if (!CasePin || CasePin->LinkedTo.Num() == 0 || !CasePin->LinkedTo[0]) continue;

            UK2Node* CaseNode = Cast<UK2Node>(CasePin->LinkedTo[0]->GetOwningNode());
            FString CasePinName = CasePin->LinkedTo[0]->GetName();

            // �������� ��� ���������
            FString CaseValue = FString::Format(TEXT("\"{0}\""), { *CasePin->GetName() });
            FString CaseBody = NativizationV2Subsystem->GenerateCodeFromNode(CaseNode, CasePinName, VisitedNodes, MacroStack);

            Content += FString::Format(TEXT("{0}if ({1} == {2})\n{\n{3}\n}\n"), {
                bFirst ? TEXT("") : TEXT("else "),
                *SelectionExpr,
                *CaseValue,
                *CaseBody
                });

            bFirst = false;
        }

        // Default ��� �����
        if (DefaultPin && DefaultPin->LinkedTo.Num() > 0 && DefaultPin->LinkedTo[0])
        {
            UK2Node* DefaultNode = Cast<UK2Node>(DefaultPin->LinkedTo[0]->GetOwningNode());
            FString DefaultPinName = DefaultPin->LinkedTo[0]->GetName();
            FString DefaultBody = NativizationV2Subsystem->GenerateCodeFromNode(DefaultNode, DefaultPinName, VisitedNodes, MacroStack);

            Content += FString::Format(TEXT("else\n{\n{0}\n}\n"), { *DefaultBody });
        }
    }
    else
    {
        // === ������� switch ��� int/enum ===
        Content += FString::Format(TEXT("switch ({0})\n{\n"), { *SelectionExpr });

        for (UEdGraphPin* CasePin : OptionPins)
        {
            if (!CasePin || CasePin->LinkedTo.Num() == 0 || !CasePin->LinkedTo[0]) continue;

            UK2Node* CaseNode = Cast<UK2Node>(CasePin->LinkedTo[0]->GetOwningNode());
            FString CasePinName = CasePin->LinkedTo[0]->GetName();

            FString CaseValue;

            if (UK2Node_SwitchInteger* SwitchInt = Cast<UK2Node_SwitchInteger>(SwitchNode))
            {
                CaseValue = CasePin->GetName(); // "0", "1", ...
            }
            else if (UK2Node_SwitchEnum* SwitchEnum = Cast<UK2Node_SwitchEnum>(SwitchNode))
            {
                UEnum* Enum = SwitchEnum->GetEnum();
                if (Enum)
                {
                    int32 EnumIndex = Enum->GetIndexByName(CasePin->PinName);
                    CaseValue = UBlueprintNativizationLibrary::GetUniqueFieldName(Enum) + TEXT("::") + Enum->GetNameStringByIndex(EnumIndex);
                }
                else
                {
                    CaseValue = CasePin->PinName.ToString();
                }
            }
            else
            {
                CaseValue = CasePin->PinName.ToString();
            }

            FString CaseBody = NativizationV2Subsystem->GenerateCodeFromNode(CaseNode, CasePinName, VisitedNodes, MacroStack);

            Content += FString::Format(TEXT("case {0}:\n{\n{1}\nbreak;\n}\n"), {
                *CaseValue,
                *CaseBody
                });
        }

        if (DefaultPin && DefaultPin->LinkedTo.Num() > 0 && DefaultPin->LinkedTo[0])
        {
            UK2Node* DefaultNode = Cast<UK2Node>(DefaultPin->LinkedTo[0]->GetOwningNode());
            FString DefaultPinName = DefaultPin->LinkedTo[0]->GetName();
            FString DefaultBody = NativizationV2Subsystem->GenerateCodeFromNode(DefaultNode, DefaultPinName, VisitedNodes, MacroStack);

            Content += FString::Format(TEXT("default:\n{\n{0}\nbreak;\n}\n"), { *DefaultBody });
        }

        Content += TEXT("}\n");
    }

    return Content;
}
