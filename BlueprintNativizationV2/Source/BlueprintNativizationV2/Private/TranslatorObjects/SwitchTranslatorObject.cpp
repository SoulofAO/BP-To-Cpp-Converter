/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright 2025 AO All Rights Reserved.
 */

#include "TranslatorObjects/SwitchTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_SwitchString.h"
#include "K2Node_SwitchName.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_Switch.h"

TSet<FString> USwitchTranslatorObject::GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem)
{
    TSet<FString> Includes = Super::GenerateCppIncludeInstructions(Node, Subsystem);
    UK2Node_Switch* Switch = CastChecked<UK2Node_Switch>(Node);
    FString Include;
    if (Switch->FunctionClass && Subsystem->GetIncludeFromField(Switch->FunctionClass, Include))
    {
        Includes.Add(Include);
    }
    return Includes;
}

FString USwitchTranslatorObject::GenerateCodeFromNode(UK2Node* Node,
    FString EntryPinName,
    TArray<FVisitedNodeStack> VisitedNodes,
    TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations, UNativizationV2Subsystem* NativizationV2Subsystem)
{
    FString Content;

    UK2Node_Switch* SwitchNode = Cast<UK2Node_Switch>(Node);
    if (!SwitchNode)
    {
        return Content;
    }

    UEdGraphPin* DefaultPin = SwitchNode->GetDefaultPin();
    UEdGraphPin* SelectionPin = SwitchNode->GetSelectionPin();
    FString SelectionExpiration = TEXT("0");

	FGenerationPreparations LocalPreparations = Preparations;

    if (SelectionPin)
    {
        FGenerateResultStruct SelectionExpirationResultStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(SwitchNode, SelectionPin, 0, MacroStack);
        LocalPreparations.Append(SelectionExpirationResultStruct.Preparations);
        SelectionExpiration = SelectionExpirationResultStruct.Code;
    }

	Content += GenerateNewPreparations(Preparations, LocalPreparations); 

    TArray<UEdGraphPin*> OptionPins = UBlueprintNativizationLibrary::GetFilteredPins(Node, EPinOutputOrInputFilter::Ouput, EPinExcludeFilter::None, EPinIncludeOnlyFilter::ExecPin);
    OptionPins.Remove(DefaultPin);

    bool bIsStringLikeSwitch = Cast<UK2Node_SwitchString>(SwitchNode) != nullptr || Cast<UK2Node_SwitchName>(SwitchNode) !=nullptr;

    TArray<UEdGraphPin*> ConnectedBranches;
    for (UEdGraphPin* Output : OptionPins)
    {
        if (!Output->LinkedTo.IsEmpty())
        {
            ConnectedBranches.Add(Output);
        }
    }
    if (DefaultPin && !DefaultPin->LinkedTo.IsEmpty())
    {
        ConnectedBranches.Add(DefaultPin);
    }
    UEdGraphPin* CommonTail = NativizationV2Subsystem->FindCommonExecutionTail(ConnectedBranches);
    FString ContinuationName;
    if (CommonTail)
    {
        ContinuationName = NativizationV2Subsystem->GetNodeStateName(Node, TEXT("bSwitchContinuation"), true);
        Content += TEXT("{\nbool ") + ContinuationName + TEXT(" = false;\n");
    }

    auto GenerateBranch = [&](UEdGraphPin* OutputPin)
    {
        TArray<UK2Node*> BranchMacroStack = MacroStack;
        UEdGraphPin* ConnectedPin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(OutputPin, 0, false, BranchMacroStack, NativizationV2Subsystem);
        if (!ConnectedPin)
        {
            return FString();
        }
        TSet<UK2Node*> DeferredNodes = NativizationV2Subsystem->DeferredExecutionNodes;
        if (CommonTail)
        {
            DeferredNodes.Add(CastChecked<UK2Node>(CommonTail->GetOwningNode()));
        }
        TGuardValue<TSet<UK2Node*>> DeferredScope(NativizationV2Subsystem->DeferredExecutionNodes, MoveTemp(DeferredNodes));
        FString BranchCode = NativizationV2Subsystem->GenerateCodeFromNode(Cast<UK2Node>(ConnectedPin->GetOwningNode()),
            ConnectedPin->GetName(), VisitedNodes, LocalPreparations, BranchMacroStack);
        if (CommonTail)
        {
            BranchCode += ContinuationName + TEXT(" = true;\n");
        }
        return BranchCode;
    };

    const bool bUsesPredicate = SelectionPin && SelectionPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct && SwitchNode->FunctionClass;
    if (bIsStringLikeSwitch || bUsesPredicate)
    {
        bool bFirst = true;
        const FString SelectionName = UBlueprintNativizationLibrary::GenerateLambdaConstructorUniqueVariableName(
            Node->GetBlueprint()->GeneratedClass, TEXT("SwitchSelection"));
        FString SelectionType = TEXT("FName");
        UK2Node_SwitchString* StringSwitch = Cast<UK2Node_SwitchString>(SwitchNode);
        if (StringSwitch)
        {
            SelectionType = TEXT("FString");
        }
        if (bUsesPredicate)
        {
            SelectionType = UBlueprintNativizationLibrary::GetPinType(SelectionPin->PinType, false);
        }
        Content += FString::Format(TEXT("{\nconst {0} {1}({2});\n"), { SelectionType, SelectionName, SelectionExpiration });

        for (UEdGraphPin* CasePin : OptionPins)
        {
            const FString CaseValue = FString::Format(TEXT("TEXT(\"{0}\")"), { CasePin->GetName().ReplaceCharWithEscapedChar() });
            FString Condition = FString::Format(TEXT("{0} == FName({1})"), { SelectionName, CaseValue });
            if (StringSwitch)
            {
                FString SearchCase = TEXT("ESearchCase::IgnoreCase");
                if (StringSwitch->bIsCaseSensitive)
                {
                    SearchCase = TEXT("ESearchCase::CaseSensitive");
                }
                Condition = FString::Format(TEXT("{0}.Equals({1}, {2})"), { SelectionName, CaseValue, SearchCase });
            }
            if (bUsesPredicate)
            {
                const FString CaseExpression = UBlueprintNativizationLibrary::GenerateOneLineDefaultConstructor(
                    CasePin->GetName(), SwitchNode->GetInnerCaseType(), SwitchNode->GetExportTextForPin(CasePin), nullptr);
                Condition = TEXT("!") + UBlueprintNativizationLibrary::GetUniqueFieldName(SwitchNode->FunctionClass) + TEXT("::") +
                    SwitchNode->FunctionName.ToString() + TEXT("(") + SelectionName + TEXT(", ") + CaseExpression + TEXT(")");
            }
            if (!bFirst)
            {
                Content += TEXT("else ");
            }
            Content += FString::Format(TEXT("if ({0})\n{\n{1}\n}\n"), { Condition, GenerateBranch(CasePin) });

            bFirst = false;
        }

        if (DefaultPin && DefaultPin->LinkedTo.Num() > 0 && DefaultPin->LinkedTo[0])
        {
            if (!bFirst)
            {
                Content += TEXT("else\n");
            }
            Content += FString::Format(TEXT("{\n{0}\n}\n"), { GenerateBranch(DefaultPin) });
        }
        Content += TEXT("}\n");
    }
    else
    {
        Content += FString::Format(TEXT("switch ({0})\n{\n"), { *SelectionExpiration });

        for (UEdGraphPin* CasePin : OptionPins)
        {
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
                    CaseValue = UBlueprintNativizationLibrary::GetUniqueFieldName(Enum) + TEXT("::") + UBlueprintNativizationLibrary::GetEnumValueName(Enum, Enum->GetValueByIndex(EnumIndex));
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

            FString CaseBody = GenerateBranch(CasePin);

            Content += FString::Format(TEXT("case {0}:\n{\n{1}\nbreak;\n}\n"), {
                *CaseValue,
                *CaseBody
                });
        }

        if (DefaultPin && DefaultPin->LinkedTo.Num() > 0 && DefaultPin->LinkedTo[0])
        {
            FString DefaultBody = GenerateBranch(DefaultPin);

            Content += FString::Format(TEXT("default:\n{\n{0}\nbreak;\n}\n"), { *DefaultBody });
        }

        Content += TEXT("}\n");
    }

    if (CommonTail)
    {
        Content += TEXT("if (") + ContinuationName + TEXT(")\n{\n") +
            NativizationV2Subsystem->GenerateCodeFromNode(CastChecked<UK2Node>(CommonTail->GetOwningNode()),
                CommonTail->GetName(), VisitedNodes, LocalPreparations, MacroStack) + TEXT("}\n}\n");
    }
    return Content;
}
