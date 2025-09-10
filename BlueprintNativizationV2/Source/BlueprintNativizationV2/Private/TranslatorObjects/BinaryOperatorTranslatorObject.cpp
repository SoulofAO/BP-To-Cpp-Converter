// Fill out your copyright notice in the Description page of Project Settings.


#include "TranslatorObjects/BinaryOperatorTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"


FGenerateResultStruct UBinaryOperatorTranslatorObject::GenerateInputParameterCodeForNode(
    UK2Node* Node,
    UEdGraphPin* Pin,
    int PinIndex,
    TArray<UK2Node*> MacroStack,
    UNativizationV2Subsystem* NativizationV2Subsystem)
{
    if (UK2Node_CommutativeAssociativeBinaryOperator* CommutativeAssociativeBinaryOperatorNode =
        Cast<UK2Node_CommutativeAssociativeBinaryOperator>(Node))
    {
        TArray<UEdGraphPin*> Pins = UBlueprintNativizationLibrary::GetFilteredPins(
            CommutativeAssociativeBinaryOperatorNode,
            EPinOutputOrInputFilter::Input,
            EPinExcludeFilter::None,
            EPinIncludeOnlyFilter::None
        );

        FString MemberName = CommutativeAssociativeBinaryOperatorNode->FunctionReference.GetMemberName().ToString();

        TMap<FString, FString> OperationByFunctionName =
        {
            { TEXT("BooleanOR"),  TEXT(" || ") },
            { TEXT("BooleanAND"), TEXT(" && ") },
        };

        TArray<FString> Content;
        TSet<FString> Preparations;
        for (UEdGraphPin* InputPin : Pins)
        {
            FGenerateResultStruct ResultStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(
                CommutativeAssociativeBinaryOperatorNode,
                InputPin,
                0,
                MacroStack
            );
            if (!ResultStruct.Code.IsEmpty())
            {
                Content.Add(ResultStruct.Code);
            }
            Preparations.Append(ResultStruct.Preparations);
        }

        if (Content.Num() == 0)
        {
            return FGenerateResultStruct();
        }

        if (MemberName == TEXT("BooleanNAND"))
        {
            if (Content.Num() == 1)
            {
                return FGenerateResultStruct(FString::Printf(TEXT("!(%s)"), *Content[0]), Preparations);
            }

            FString Joined = FString::Join(Content, TEXT(" && "));
            return FGenerateResultStruct(FString::Printf(TEXT("!(%s)"), *Joined), Preparations);
        }
        if (OperationByFunctionName.Contains(MemberName))
        {
            FString Operator = *OperationByFunctionName.Find(MemberName);
            if (Content.Num() == 1)
            {
                return FGenerateResultStruct(Content[0], Preparations);
            }
            return FGenerateResultStruct(FString::Join(Content, *Operator), Preparations);
        }
    }

    return FGenerateResultStruct();
}
