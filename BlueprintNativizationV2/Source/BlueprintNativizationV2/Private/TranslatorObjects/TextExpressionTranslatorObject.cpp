/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/TextExpressionTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"
#include "K2Node_EnumInequality.h"
#include "EdGraphSchema_K2.h"

FGenerateResultStruct UTextExpressionTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin,
    int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem)
{
    if (UK2Node_EnumEquality* Comparison = Cast<UK2Node_EnumEquality>(Node))
    {
        FGenerateResultStruct First = Subsystem->GenerateInputParameterCodeForNode(Node, Comparison->GetInput1Pin(), 0, MacroStack);
        const FGenerateResultStruct Second = Subsystem->GenerateInputParameterCodeForNode(Node, Comparison->GetInput2Pin(), 0, MacroStack);
        First.Preparations.Append(Second.Preparations);
        FString Operator = TEXT(" == ");
        if (Node->IsA<UK2Node_EnumInequality>())
        {
            Operator = TEXT(" != ");
        }
        First.Code = TEXT("(") + First.Code + Operator + Second.Code + TEXT(")");
        return First;
    }
    UK2Node_FormatText* Format = CastChecked<UK2Node_FormatText>(Node);
    FGenerateResultStruct Result = Subsystem->GenerateInputParameterCodeForNode(Node, Format->GetFormatPin(), 0, MacroStack);
    const FString Arguments = Subsystem->GetNodeStateName(Node, TEXT("FormatArguments"), true);
    const FString Name = Subsystem->GetNodeStateName(Node, TEXT("FormattedText"), true);
    FString Content = TEXT("FFormatNamedArguments ") + Arguments + TEXT(";\n");
    for (int32 ArgumentIndex = 0; ArgumentIndex < Format->GetArgumentCount(); ++ArgumentIndex)
    {
        const FString ArgumentName = Format->GetArgumentName(ArgumentIndex).ToString();
        UEdGraphPin* ArgumentPin = Format->FindArgumentPin(FName(*ArgumentName));
        FGenerateResultStruct Value;
        if (ArgumentPin->LinkedTo.IsEmpty())
        {
            Value.Code = UBlueprintNativizationLibrary::GenerateTextLiteral(ArgumentPin->DefaultTextValue,
                Node->GetBlueprint()->GetPathName(), Node->NodeGuid.ToString() + ArgumentName);
        }
        else
        {
            Value = Subsystem->GenerateInputParameterCodeForNode(Node, ArgumentPin, 0, MacroStack);
            const FName Category = ArgumentPin->PinType.PinCategory;
            if (Category == UEdGraphSchema_K2::PC_Int ||
                (Category == UEdGraphSchema_K2::PC_Byte && !ArgumentPin->PinType.PinSubCategoryObject.IsValid()))
            {
                Value.Code = TEXT("static_cast<int64>(") + Value.Code + TEXT(")");
            }
            else if (Category == UEdGraphSchema_K2::PC_Byte || Category == UEdGraphSchema_K2::PC_Enum)
            {
                Value.Code = TEXT("static_cast<ETextGender>(") + Value.Code + TEXT(")");
            }
            else
            {
                FString Conversion;
                if (Category == UEdGraphSchema_K2::PC_Boolean)
                {
                    Conversion = TEXT("Conv_BoolToText");
                }
                else if (Category == UEdGraphSchema_K2::PC_Name)
                {
                    Conversion = TEXT("Conv_NameToText");
                }
                else if (Category == UEdGraphSchema_K2::PC_String)
                {
                    Conversion = TEXT("Conv_StringToText");
                }
                else if (Category == UEdGraphSchema_K2::PC_Object)
                {
                    Conversion = TEXT("Conv_ObjectToText");
                }
                if (!Conversion.IsEmpty())
                {
                    Value.Code = TEXT("UKismetTextLibrary::") + Conversion + TEXT("(") + Value.Code + TEXT(")");
                }
            }
        }
        Result.Preparations.Append(Value.Preparations);
        Content += Arguments + TEXT(".Add(TEXT(\"") + ArgumentName + TEXT("\"), FFormatArgumentValue(") + Value.Code + TEXT("));\n");
    }
    Content += TEXT("const FText ") + Name + TEXT(" = FText::Format(") + Result.Code + TEXT(", ") + Arguments + TEXT(");\n");
    Result.Preparations.Add(Content);
    Result.Code = Name;
    return Result;
}

TSet<FString> UTextExpressionTranslatorObject::GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem)
{
    TSet<FString> Includes = Super::GenerateCppIncludeInstructions(Node, Subsystem);
    Includes.Add(TEXT("#include \"Kismet/KismetTextLibrary.h\""));
    return Includes;
}
