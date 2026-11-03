#include "BlueprintNativizationLibrary.h"
#include "RetainedBlueprintGeneration.h"
#include "BlueprintNativizationSubsystem.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Variable.h"
#include "K2Node_FunctionEntry.h"
#include "BlueprintNativizationSettings.h"
#include "NativeApiCatalog.h"
#include "KismetCompilerMisc.h"
#include "NativeEventGeneration.h"

FProperty* UBlueprintNativizationLibrary::FindFunctionProperty(UFunction* Function, FName PropertyName, bool bParameter)
{
    if (Function)
    {
        for (TFieldIterator<FProperty> Property(Function, EFieldIterationFlags::None); Property; ++Property)
        {
            if (Property->GetFName() == PropertyName && Property->HasAnyPropertyFlags(CPF_Parm) == bParameter)
            {
                return *Property;
            }
        }
    }
    if (Function && !bParameter)
    {
        UBlueprint* Blueprint = GetBlueprintFromFunction(Function);
        if (Blueprint && Blueprint->GeneratedClass)
        {
            UFunction* CompiledFunction = Blueprint->GeneratedClass->FindFunctionByName(Function->GetFName());
            if (CompiledFunction && CompiledFunction != Function)
            {
                return FindFunctionProperty(CompiledFunction, PropertyName, false);
            }
        }
    }
    return nullptr;
}

FProperty* UBlueprintNativizationLibrary::ResolveVariableProperty(UK2Node_Variable* Variable)
{
    FProperty* Property = Variable->GetPropertyForVariable();
    if (!Variable->VariableReference.IsLocalScope())
    {
        return Property;
    }
    UBlueprint* Blueprint = Variable->GetBlueprint();
    const FName FunctionName(*Variable->VariableReference.GetMemberScopeName());
    UFunction* Function = FindFunctionByFunctionName(Blueprint, FunctionName);
    if (Function)
    {
        bool bIsSparseProperty = false;
        if (FProperty* ScopedProperty = FKismetCompilerUtilities::FindNamedPropertyInScope(
            Function, Variable->GetVarName(), bIsSparseProperty, true))
        {
            return ScopedProperty;
        }
    }
    if (!Property)
    {
        Property = FindFunctionProperty(Function, Variable->GetVarName(), false);
        if (!Property)
        {
            Property = FindFunctionProperty(Function, Variable->GetVarName(), true);
        }
    }
    return Property;
}

FGenerateResultStruct UBlueprintNativizationLibrary::GenerateVariableAccess(UK2Node_Variable* Variable,
    TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem)
{
    FProperty* Property = ResolveVariableProperty(Variable);
    if (!Property)
    {
        FNativeApiCatalog::AddError(FString::Format(TEXT("Cannot resolve variable {0} while generating {1}, scope {2}."),
            {Variable->GetVarName().ToString(), Variable->GetPathName(), Variable->VariableReference.GetMemberScopeName()}));
        return FGenerateResultStruct();
    }
    if (Property->HasAnyPropertyFlags(CPF_Parm))
    {
        UFunction* OwnerFunction = Cast<UFunction>(Property->GetOwnerUObject());
        UFunction* CurrentFunction = GetFunctionByNodeAndEntryNodes(Variable, Subsystem->EntryNodes);
        if (OwnerFunction && CurrentFunction && OwnerFunction->GetFName() == CurrentFunction->GetFName())
        {
            if (FProperty* Parameter = FindFunctionProperty(CurrentFunction, Property->GetFName(), true))
            {
                Property = Parameter;
            }
        }
    }
    FGenerateResultStruct Result(GetUniquePropertyName(Property));
    if (Property->HasMetaData(FBlueprintMetadata::MD_PropertyGetFunction))
    {
        UFunction* Getter = Property->GetOwnerClass()->FindFunctionByName(FName(*Property->GetMetaData(FBlueprintMetadata::MD_PropertyGetFunction)));
        FString Receiver = TEXT("this");
        if (UEdGraphPin* ReceiverPin = Variable->FindPin(UEdGraphSchema_K2::PN_Self);
            ReceiverPin && (!ReceiverPin->LinkedTo.IsEmpty() || ReceiverPin->DefaultObject))
        {
            const FGenerateResultStruct Target = Subsystem->GenerateInputParameterCodeForNode(Variable, ReceiverPin, 0, MacroStack);
            Result.Preparations.Append(Target.Preparations);
            Receiver = Target.Code;
        }
        Result.Code = FRetainedBlueprintGeneration::CallPropertyAccessor(Property, Getter, Receiver, FString(), false);
        return Result;
    }
    if (FRetainedBlueprintGeneration::IsRetained(Property->GetOwnerClass()))
    {
        FString Receiver = TEXT("this");
        UEdGraphPin* ReceiverPin = Variable->FindPin(UEdGraphSchema_K2::PN_Self);
        if (ReceiverPin && (!ReceiverPin->LinkedTo.IsEmpty() || ReceiverPin->DefaultObject))
        {
            FGenerateResultStruct Target = Subsystem->GenerateInputParameterCodeForNode(Variable, ReceiverPin, 0, MacroStack);
            Result.Preparations = Target.Preparations;
            Receiver = Target.Code;
        }
        Result.Code = FRetainedBlueprintGeneration::PreparePropertyRead(Property, Receiver, Variable, Subsystem, Result.Preparations);
        return Result;
    }
    FGetterAndSetterPropertyDescriptor Descriptor;
    const bool bUsesGetter = UBlueprintNativizationSettingsLibrary::FindGetterAndSetterDescriptorDescriptorByProperty(Property, Descriptor) &&
        !Descriptor.GetterPropertyFunctionName.IsEmpty();
    if (!bUsesGetter && Property->GetOwnerStruct()->IsNative() &&
        Property->HasAnyPropertyFlags(CPF_NativeAccessSpecifierPrivate | CPF_NativeAccessSpecifierProtected))
    {
        FString Receiver = TEXT("this");
        UEdGraphPin* ReceiverPin = Variable->FindPin(UEdGraphSchema_K2::PN_Self);
        if (ReceiverPin && (!ReceiverPin->LinkedTo.IsEmpty() || ReceiverPin->DefaultObject))
        {
            FGenerateResultStruct Target = Subsystem->GenerateInputParameterCodeForNode(Variable, ReceiverPin, 0, MacroStack);
            Result.Preparations = Target.Preparations;
            Receiver = Target.Code;
        }
        Result.Code = FRetainedBlueprintGeneration::PreparePropertyRead(Property, Receiver, Variable, Subsystem, Result.Preparations);
        return Result;
    }
    if (bUsesGetter)
    {
        Result.Code = Descriptor.GetterPropertyFunctionName + TEXT("()");
    }
    UEdGraphPin* Target = Variable->FindPin(UEdGraphSchema_K2::PN_Self);
    if (Target && (Target->LinkedTo.Num() > 0 || Target->DefaultObject))
    {
        FGenerateResultStruct Receiver = Subsystem->GenerateInputParameterCodeForNode(Variable, Target, 0, MacroStack);
        Result.Preparations = Receiver.Preparations;
        FString Type = GetPropertyType(Property);
        if (!Property->IsA<FBoolProperty>() && !bUsesGetter && Property->GetOwnerClass()->HasAnyClassFlags(CLASS_Native))
        {
            Type = FString::Format(TEXT("decltype({0}::{1})"),
                { GetUniqueFieldName(Property->GetOwnerClass()), GetUniquePropertyName(Property) });
        }
        bool bAddressable = false;
        FString MissingValue = TEXT("{}");
        if (Subsystem->bRequiresInputStorage && !Property->IsA<FBoolProperty>() && !bUsesGetter &&
            !Property->GetOwnerClass()->HasAnyClassFlags(CLASS_Const))
        {
            bAddressable = true;
            MissingValue = Subsystem->GetNodeStateName(Variable, TEXT("Missing") + GetUniquePropertyName(Property), true);
            if (Subsystem->DataConsumer)
            {
                MissingValue = Subsystem->GetNodeStateName(Subsystem->DataConsumer, MissingValue, true);
            }
            Result.Preparations.Add(Type + TEXT(" ") + MissingValue + TEXT("{};\n"));
        }
        FString Access = TEXT("VariableTarget->") + Result.Code;
        if (bUsesGetter && Property->IsA<FObjectPropertyBase>() && !Property->IsA<FClassProperty>())
        {
            FObjectPropertyBase* ObjectProperty = CastFieldChecked<FObjectPropertyBase>(Property);
            Access = TEXT("Cast<") + FRetainedBlueprintGeneration::GetClassName(ObjectProperty->PropertyClass) + TEXT(">(") + Access + TEXT(")");
        }
        FString ResultName = Subsystem->GetNodeStateName(Variable, TEXT("Read") + GetUniquePropertyName(Property), true);
        if (bAddressable)
        {
            ResultName = Subsystem->GetNodeStateName(Variable, TEXT("Storage") + GetUniquePropertyName(Property), true);
        }
        if (Subsystem->DataConsumer)
        {
            ResultName = Subsystem->GetNodeStateName(Subsystem->DataConsumer, Subsystem->InputEvaluationPurpose + ResultName, true);
        }
        FString ReturnType = Type;
        FString Parameters = TEXT("const auto& VariableTarget");
        FString Arguments = Receiver.Code;
        FString Fallback = TEXT("{}");
        if (bAddressable)
        {
            ReturnType += TEXT("*");
            Access = TEXT("&(") + Access + TEXT(")");
            Parameters += TEXT(", ") + Type + TEXT("* MissingValue");
            Arguments += TEXT(", &") + MissingValue;
            Fallback = TEXT("MissingValue");
        }
        const FString Definition = TEXT("[](") + Parameters + TEXT(") -> ") + ReturnType +
            TEXT("\n{\nif (IsValid(VariableTarget))\n{\nreturn ") + Access + TEXT(";\n}\nreturn ") + Fallback + TEXT(";\n}");
        FString ExpandedFallback = TEXT("{}");
        if (bAddressable)
        {
            ExpandedFallback = TEXT("&") + MissingValue;
        }
        const FString TargetName = Subsystem->GetNodeStateName(Variable, TEXT("ReadTarget") + GetUniquePropertyName(Property), true);
        const FString ExpandedAccess = Access.Replace(TEXT("VariableTarget->"), *(TargetName + TEXT("->")), ESearchCase::CaseSensitive);
        FString Statement = TEXT("{\nconst auto ") + TargetName + TEXT(" = ") + Receiver.Code +
            TEXT(";\nif (IsValid(") + TargetName + TEXT("))\n{\n") + ResultName + TEXT(" = ") + ExpandedAccess +
            TEXT(";\n}\n");
        if (bAddressable)
        {
            Statement += TEXT("else\n{\n") + ResultName + TEXT(" = ") + ExpandedFallback + TEXT(";\n}\n");
        }
        Statement += TEXT("}\n");
        Result.Preparations.Add(ReturnType + TEXT(" ") + ResultName + TEXT("{};\n") +
            Subsystem->RegisterRepeatedCall(TEXT("VariableAccess|") + Property->GetPathName() + TEXT("|") + Definition,
                Subsystem->GetNodeStateName(Variable, TEXT("EvaluateRead") + GetUniquePropertyName(Property), true),
                Definition, Statement, Arguments, ResultName));
        Result.Code = ResultName;
        if (bAddressable)
        {
            Result.Code = TEXT("(*") + ResultName + TEXT(")");
        }
    }
    else if (bUsesGetter && Property->IsA<FObjectPropertyBase>() && !Property->IsA<FClassProperty>())
    {
        FObjectPropertyBase* ObjectProperty = CastFieldChecked<FObjectPropertyBase>(Property);
        Result.Code = TEXT("Cast<") + FRetainedBlueprintGeneration::GetClassName(ObjectProperty->PropertyClass) + TEXT(">(") + Result.Code + TEXT(")");
    }
    else if (Subsystem->bRequiresInputStorage && !bUsesGetter && Property->GetOwnerClass() &&
        !Property->GetOwnerClass()->HasAnyClassFlags(CLASS_Native) &&
        (Property->IsA<FArrayProperty>() || Property->IsA<FMapProperty>() || Property->IsA<FSetProperty>() || Property->IsA<FStructProperty>()))
    {
        UClass* GeneratedClass = Variable->GetBlueprint()->GeneratedClass;
        UFunction* CurrentFunction = GetFunctionByNodeAndEntryNodes(Variable, Subsystem->EntryNodes);
        if (CurrentFunction && FNativeEventGeneration::IsImplementationConst(CurrentFunction, GeneratedClass))
        {
            Result.Code = TEXT("const_cast<") + FRetainedBlueprintGeneration::GetClassName(GeneratedClass) + TEXT("*>(this)->") + Result.Code;
        }
    }
    return Result;
}
