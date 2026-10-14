#include "DelegateExpressions.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"
#include "RetainedBlueprintGeneration.h"
#include "NativeGenerationContext.h"
#include "TranslatorObjects/StructTranslatorObject.h"
#include "UObject/UnrealType.h"
#include "K2Node_AddDelegate.h"
#include "K2Node_CallDelegate.h"
#include "K2Node_ClearDelegate.h"
#include "K2Node_RemoveDelegate.h"

FGenerateResultStruct GenerateMulticastDelegateStatement(UK2Node* Node, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem)
{
    UK2Node_BaseMCDelegate* Operation = CastChecked<UK2Node_BaseMCDelegate>(Node);
    FMulticastDelegateProperty* Property = CastFieldChecked<FMulticastDelegateProperty>(Operation->GetProperty());
    FGenerateResultStruct Result;
    FString Arguments;
    FString Method;
    if (Node->IsA<UK2Node_CallDelegate>())
    {
        Method = TEXT("Broadcast");
        TArray<FString> Values;
        for (TFieldIterator<FProperty> Parameter(Property->SignatureFunction); Parameter && Parameter->HasAnyPropertyFlags(CPF_Parm); ++Parameter)
        {
            if (!Parameter->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                const FGenerateResultStruct Value = Subsystem->GenerateInputParameterCodeForNode(Node, Node->FindPinChecked(Parameter->GetFName()), 0, MacroStack);
                Result.Preparations.Append(Value.Preparations);
                Values.Add(Value.Code);
            }
        }
        Arguments = FString::Join(Values, TEXT(", "));
    }
    else if (Node->IsA<UK2Node_ClearDelegate>())
    {
        Method = TEXT("Clear");
    }
    else
    {
        Method = TEXT("AddUnique");
        if (Node->IsA<UK2Node_RemoveDelegate>())
        {
            Method = TEXT("Remove");
        }
        const FGenerateResultStruct Callback = Subsystem->GenerateInputParameterCodeForNode(Node, Operation->GetDelegatePin(), 0, MacroStack);
        Result.Preparations.Append(Callback.Preparations);
        Arguments = Callback.Code;
    }
    UEdGraphPin* TargetPin = Node->FindPinChecked(UEdGraphSchema_K2::PN_Self);
    for (int32 TargetIndex = 0; TargetIndex < FMath::Max(1, TargetPin->LinkedTo.Num()); ++TargetIndex)
    {
        FGenerateResultStruct Target = Subsystem->GenerateInputParameterCodeForNode(Node, TargetPin, TargetIndex, MacroStack);
        Result.Preparations.Append(Target.Preparations);
        if (Target.Code.IsEmpty())
        {
            Target.Code = TEXT("this");
        }
        const FString Receiver = Subsystem->GetNodeStateName(Node, TEXT("DelegateOwner"));
        if (FRetainedBlueprintGeneration::IsRetained(Property->GetOwnerClass()))
        {
            FStructTranslationResult Requirements;
            Requirements.HeaderIncludes = { TEXT("UObject/UnrealType.h"), TEXT("UObject/StructOnScope.h") };
            FNativeGenerationContext::AddRequirements(Requirements);
            const FString Descriptor = Receiver + TEXT("Property");
            const FString Address = Receiver + TEXT("Address");
            Result.Code += TEXT("{\nauto ") + Receiver + TEXT(" = ") + Target.Code + TEXT(";\nif (IsValid(") + Receiver +
                TEXT("))\n{\nFMulticastDelegateProperty* ") + Descriptor + TEXT(" = FindFProperty<FMulticastDelegateProperty>(") +
                Receiver + TEXT("->GetClass(), ") + FRetainedBlueprintGeneration::Quote(Property->GetName()) + TEXT(");\ncheck(") +
                Descriptor + TEXT(");\nvoid* ") + Address + TEXT(" = ") + Descriptor + TEXT("->ContainerPtrToValuePtr<void>(") + Receiver + TEXT(");\n");
            if (Method == TEXT("Broadcast"))
            {
                const FString Parameters = Receiver + TEXT("Parameters");
                Result.Code += TEXT("FStructOnScope ") + Parameters + TEXT("(") + Descriptor + TEXT("->SignatureFunction);\n");
                for (TFieldIterator<FProperty> Parameter(Property->SignatureFunction); Parameter && Parameter->HasAnyPropertyFlags(CPF_Parm); ++Parameter)
                {
                    if (Parameter->HasAnyPropertyFlags(CPF_ReturnParm))
                    {
                        continue;
                    }
                    FGenerateResultStruct Value = Subsystem->GenerateInputParameterCodeForNode(Node, Node->FindPinChecked(Parameter->GetFName()), 0, MacroStack);
                    Result.Preparations.Append(Value.Preparations);
                    const FString ValueName = Parameters + Parameter->GetName();
                    const FString ParameterDescriptor = ValueName + TEXT("Property");
                    const FString ParameterAddress = ValueName + TEXT("Address");
                    Result.Code += TEXT("const auto ") + ValueName + TEXT(" = ") + Value.Code + TEXT(";\nFProperty* ") + ParameterDescriptor +
                        TEXT(" = FindFProperty<FProperty>(") + Descriptor + TEXT("->SignatureFunction, ") + FRetainedBlueprintGeneration::Quote(Parameter->GetName()) +
                        TEXT(");\ncheck(") + ParameterDescriptor + TEXT(");\nvoid* ") + ParameterAddress + TEXT(" = ") + ParameterDescriptor +
                        TEXT("->ContainerPtrToValuePtr<void>(") + Parameters + TEXT(".GetStructMemory());\n") +
                        FRetainedBlueprintGeneration::Transfer(*Parameter, ParameterDescriptor, ParameterAddress, ValueName, true);
                }
                Result.Code += TEXT("if (const FMulticastScriptDelegate* Callback = ") + Descriptor + TEXT("->GetMulticastDelegate(") + Address +
                    TEXT("))\n{\nCallback->ProcessMulticastDelegate<UObject>(") + Parameters + TEXT(".GetStructMemory());\n}\n");
            }
            else
            {
                FString ReflectedMethod = Method;
                if (Method == TEXT("AddUnique"))
                {
                    ReflectedMethod = TEXT("AddDelegate");
                }
                else if (Method == TEXT("Remove"))
                {
                    ReflectedMethod = TEXT("RemoveDelegate");
                }
                else
                {
                    ReflectedMethod = TEXT("ClearDelegate");
                }
                Result.Code += Descriptor + TEXT("->") + ReflectedMethod + TEXT("(");
                if (!Arguments.IsEmpty())
                {
                    Result.Code += Arguments + TEXT(", ");
                }
                Result.Code += Receiver + TEXT(", ") + Address + TEXT(");\n");
            }
            Result.Code += TEXT("}\n}\n");
            continue;
        }
        Result.Code += TEXT("{\nauto ") + Receiver + TEXT(" = ") + Target.Code + TEXT(";\nif (IsValid(") + Receiver + TEXT("))\n{\n") +
            Receiver + TEXT("->") + UBlueprintNativizationLibrary::GetUniquePropertyName(Property) + TEXT(".") + Method + TEXT("(") + Arguments + TEXT(");\n}\n}\n");
    }
    return Result;
}
