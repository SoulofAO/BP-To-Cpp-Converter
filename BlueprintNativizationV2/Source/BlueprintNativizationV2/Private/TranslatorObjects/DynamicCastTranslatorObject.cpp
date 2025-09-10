/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/DynamicCastTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"


FString UDynamicCastTranslatorObject::GenerateCodeFromNode(UK2Node* Node,
    FString EntryPinName,
    TArray<FVisitedNodeStack> VisitedNodes,
    TArray<UK2Node*> MacroStack, TSet<FString>& Preparations, UNativizationV2Subsystem* NativizationV2Subsystem)
{
    if (UK2Node_DynamicCast* DynamicCast = Cast<UK2Node_DynamicCast>(Node))
    {
        if (!DynamicCast->TargetType)
        {
            UE_LOG(LogTemp, Warning, TEXT("DynamicCast has no TargetType set!"));
            return FString();
        }

        FString Content;

        FString ValidPinCodeGenerate;
        UEdGraphPin* ValidGraphPin = DynamicCast->GetValidCastPin();
        FString InvalidPinCodeGenerate;
        UEdGraphPin* InvalidGraphPin = DynamicCast->GetInvalidCastPin();

        FString TargetTypeName = UBlueprintNativizationLibrary::GetUniqueFieldName(DynamicCast->TargetType);
        FGenerateResultStruct SourceCode = NativizationV2Subsystem->GenerateInputParameterCodeForNode(
            Node,
            DynamicCast->GetCastSourcePin(),
            0,
            TArray<UK2Node*>());

        TSet<FString> LocalPreparations = Preparations;
        Content += GenerateNewPreparations(LocalPreparations, SourceCode.Preparations);
        LocalPreparations.Append(SourceCode.Preparations);

        if (ValidGraphPin->LinkedTo.Num() > 0)
        {
            UK2Node* ConnectedNode = Cast<UK2Node>(ValidGraphPin->LinkedTo[0]->GetOwningNode());
            ValidPinCodeGenerate = NativizationV2Subsystem->GenerateCodeFromNode(
                ConnectedNode,
                ValidGraphPin->LinkedTo[0]->GetName(),
                VisitedNodes, LocalPreparations, MacroStack);
        }

        if (InvalidGraphPin->LinkedTo.Num() > 0)
        {
            UK2Node* ConnectedNode = Cast<UK2Node>(InvalidGraphPin->LinkedTo[0]->GetOwningNode());
            InvalidPinCodeGenerate = NativizationV2Subsystem->GenerateCodeFromNode(
                ConnectedNode,
                InvalidGraphPin->LinkedTo[0]->GetName(),
                VisitedNodes, LocalPreparations, MacroStack);
        }

        bool bHasValidBlock = !ValidPinCodeGenerate.IsEmpty();
        bool bHasInvalidBlock = !InvalidPinCodeGenerate.IsEmpty();


        if (DynamicCast->GetCastResultPin()->LinkedTo.Num() == 0)
        {
            if (bHasValidBlock || bHasInvalidBlock)
            {
                if (bHasValidBlock && bHasInvalidBlock)
                {
                    Content += FString::Format(TEXT("if(Cast<{0}>({1}))\n{\n{2}\n}\nelse\n{\n{3}\n}"),
                        { TargetTypeName, SourceCode.Code, ValidPinCodeGenerate, InvalidPinCodeGenerate });
                }
                else if (bHasValidBlock)
                {
                    Content += FString::Format(TEXT("if(Cast<{0}>({1}))\n{\n{2}\n}"),
                        { TargetTypeName, SourceCode.Code, ValidPinCodeGenerate });
                }
                else // only invalid block
                {
                    Content += FString::Format(TEXT("if(!Cast<{0}>({1}))\n{\n{2}\n}"),
                        { TargetTypeName, SourceCode.Code, InvalidPinCodeGenerate });
                }
            }
        }
        else
        {
            FString CastResultVar = UBlueprintNativizationLibrary::GetUniquePinName(
                DynamicCast->GetCastResultPin(),
                NativizationV2Subsystem->EntryNodes
            );

            if (bHasValidBlock || bHasInvalidBlock)
            {
                if (bHasValidBlock && bHasInvalidBlock)
                {
                    Content += FString::Format(TEXT("{0} = Cast<{1}>({2});\nif({0})\n{\n{3}\n}\nelse\n{\n{4}\n}"),
                        { CastResultVar, TargetTypeName, SourceCode.Code, ValidPinCodeGenerate, InvalidPinCodeGenerate });
                }
                else if (bHasValidBlock)
                {
                    Content += FString::Format(TEXT("{0} = Cast<{1}>({2});\nif({0})\n{\n{3}\n}"),
                        { CastResultVar, TargetTypeName, SourceCode.Code, ValidPinCodeGenerate });
                }
                else 
                {
                    Content += FString::Format(TEXT("{0} = Cast<{1}>({2});\nif(!{0})\n{\n{3}\n}"),
                        { CastResultVar, TargetTypeName, SourceCode.Code, InvalidPinCodeGenerate });
                }
            }
            else
            {
                Content += FString::Format(TEXT("{0} = Cast<{1}>({2});"),
                    { CastResultVar, TargetTypeName, SourceCode.Code });
            }
        }
        return Content;
    }

    return FString();
}

FGenerateResultStruct UDynamicCastTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
    if (UK2Node_DynamicCast* DynamicCast = Cast<UK2Node_DynamicCast>(Node))
    {
		if (!DynamicCast->TargetType)
		{
			UE_LOG(LogTemp, Warning, TEXT("DynamicCast has no TargetType set!"));
			return FString();
		}
        if (DynamicCast->IsNodePure())
        {
            FGenerateResultStruct InputResultStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, DynamicCast->GetCastSourcePin(), 0, TArray<UK2Node*>());

            return FGenerateResultStruct(FString::Format(TEXT("Cast<{0}>({1})"), { UBlueprintNativizationLibrary::GetUniqueFieldName(DynamicCast->TargetType), InputResultStruct.Code}), InputResultStruct.Preparations);
        }
        else
        {
            return UBlueprintNativizationLibrary::GetUniquePinName(DynamicCast->GetCastResultPin(), NativizationV2Subsystem->EntryNodes);
        }
    }

    return FString();
}
