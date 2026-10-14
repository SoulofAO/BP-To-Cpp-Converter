#include "TranslatorObjects/CallFunctionTranslatorObject.h"
#include "Kismet/KismetMathLibrary.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_Self.h"
#include "K2Node_Knot.h"
#include "EdGraphSchema_K2.h"

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
