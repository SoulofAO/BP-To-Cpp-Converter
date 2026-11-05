/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright 2026 AO All Rights Reserved.
 */

#include "Misc/PackageName.h"
#include "TranslatorObjects/CallFunctionTranslatorObject.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_Self.h"
#include "K2Node_Knot.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_PromotableOperator.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"
#include "NativeGenerationContext.h"
#include "NativeApiCatalog.h"
#include "TranslatorObjects/StructTranslatorObject.h"

FString UCallFunctionTranslatorObject::ConvertPromotableArgument(UK2Node_CallFunction* Call, UEdGraphPin* Input,
    FProperty* Parameter, const FString& Expression, UNativizationV2Subsystem* Subsystem)
{
    if (!Call->IsA<UK2Node_PromotableOperator>())
    {
        return Expression;
    }
    const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
    FEdGraphPinType DestinationType;
    Schema->ConvertPropertyToPinType(Parameter, DestinationType);
    FEdGraphPinType SourceType = Input->PinType;
    SourceType.bIsReference = false;
    SourceType.bIsConst = false;
    DestinationType.bIsReference = false;
    DestinationType.bIsConst = false;
    if (SourceType == DestinationType ||
        (SourceType.PinCategory == UEdGraphSchema_K2::PC_Real && DestinationType.PinCategory == UEdGraphSchema_K2::PC_Real))
    {
        return Expression;
    }
    UFunction* ConversionFunction = nullptr;
    if (const auto Conversion = Schema->SearchForAutocastFunction(SourceType, DestinationType))
    {
        ConversionFunction = Conversion->FunctionOwner->FindFunctionByName(Conversion->TargetFunction);
    }
    else if (!SourceType.IsContainer() && !DestinationType.IsContainer())
    {
        FName ConversionName;
        if (SourceType.PinCategory == UEdGraphSchema_K2::PC_SoftClass && DestinationType.PinCategory == UEdGraphSchema_K2::PC_Class)
        {
            ConversionName = GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, Conv_SoftClassReferenceToClass);
        }
        else if (SourceType.PinCategory == UEdGraphSchema_K2::PC_Class && DestinationType.PinCategory == UEdGraphSchema_K2::PC_SoftClass)
        {
            ConversionName = GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, Conv_ClassToSoftClassReference);
        }
        else if (SourceType.PinCategory == UEdGraphSchema_K2::PC_SoftObject && DestinationType.PinCategory == UEdGraphSchema_K2::PC_Object)
        {
            ConversionName = GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, Conv_SoftObjectReferenceToObject);
        }
        else if (SourceType.PinCategory == UEdGraphSchema_K2::PC_Object && DestinationType.PinCategory == UEdGraphSchema_K2::PC_SoftObject)
        {
            ConversionName = GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, Conv_ObjectToSoftObjectReference);
        }
        if (!ConversionName.IsNone())
        {
            ConversionFunction = UKismetSystemLibrary::StaticClass()->FindFunctionByName(ConversionName);
        }
    }
    if (ConversionFunction)
    {
        FString Include;
        if (Subsystem->GetIncludeFromField(ConversionFunction->GetOwnerClass(), Include))
        {
            FNativeGenerationContext::AddImplementationInclude(Include);
        }
        FStructTranslationResult Requirements;
        Requirements.ModuleDependencies.Add(FPackageName::GetShortName(ConversionFunction->GetOutermost()));
        FNativeGenerationContext::AddRequirements(Requirements);
        const TArray<FString> Arguments = {Expression};
        return GenerateStaticFunctionCall(ConversionFunction, Expression, &Arguments);
    }
    if (Schema->ArePinTypesCompatible(SourceType, DestinationType))
    {
        return Expression;
    }
    FNativeApiCatalog::AddError(TEXT("No read-only argument conversion for promotable operator ") +
        Call->GetPathName() + TEXT(".") + Input->GetName());
    return Expression;
}

static bool IsArithmeticFunction(const UK2Node_CallFunction* Call)
{
    const UFunction* Function = Call->GetTargetFunction();
    if (!Function || Function->GetOwnerClass() != UKismetMathLibrary::StaticClass())
    {
        return false;
    }
    const FString Name = Function->GetName();
    for (const TCHAR* Prefix : { TEXT("Add_"), TEXT("Subtract_"), TEXT("Multiply_"),
        TEXT("EqualEqual_"), TEXT("EqualExactly_"), TEXT("NotEqual_"), TEXT("NotEqualExactly_"),
        TEXT("Less_"), TEXT("LessEqual_"), TEXT("Greater_"), TEXT("GreaterEqual_"),
        TEXT("BooleanAND"), TEXT("BooleanOR"), TEXT("BooleanXOR"), TEXT("Not_PreBool") })
    {
        if (Name.StartsWith(Prefix))
        {
            return true;
        }
    }
    return false;
}

bool UCallFunctionTranslatorObject::CanInlineArithmetic(UK2Node_CallFunction* Call, UK2Node* Consumer) const
{
    if (!Consumer || !IsArithmeticFunction(Call) || !Call->GetReturnValuePin() || Call->GetReturnValuePin()->LinkedTo.Num() != 1)
    {
        return false;
    }
    if (!Consumer->IsA<UK2Node_CallFunction>() && !Consumer->IsA<UK2Node_IfThenElse>() &&
        !Consumer->IsA<UK2Node_VariableSet>() && !Consumer->IsA<UK2Node_FunctionResult>())
    {
        return false;
    }
    TArray<UK2Node*> Pending = { Consumer };
    TSet<UK2Node*> Visited;
    while (!Pending.IsEmpty())
    {
        UK2Node* Current = Pending.Pop();
        if (Visited.Contains(Current))
        {
            continue;
        }
        Visited.Add(Current);
        for (UEdGraphPin* Input : Current->Pins)
        {
            if (Input->Direction != EGPD_Input || Input->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
            {
                continue;
            }
            for (UEdGraphPin* Source : Input->LinkedTo)
            {
                UK2Node* Producer = CastChecked<UK2Node>(Source->GetOwningNode());
                if (!Producer->IsNodePure())
                {
                    if (Source->PinType.bIsReference)
                    {
                        return false;
                    }
                    continue;
                }
                if (UK2Node_CallFunction* FunctionCall = Cast<UK2Node_CallFunction>(Producer))
                {
                    if (!IsArithmeticFunction(FunctionCall))
                    {
                        return false;
                    }
                }
                else if (!Producer->IsA<UK2Node_VariableGet>() && !Producer->IsA<UK2Node_Self>() && !Producer->IsA<UK2Node_Knot>())
                {
                    return false;
                }
                Pending.Add(Producer);
            }
        }
    }
    return true;
}
