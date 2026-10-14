/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/CallParentTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSettings.h"
#include "ConstructionScriptTranslation.h"
#include "Engine/Blueprint.h"

FString UCallParentTranslatorObject::GetParentMethodName(UK2Node_CallParentFunction* Call)
{
    UFunction* Function = Call->GetTargetFunction();
    const FString NetworkEventBody = UBlueprintNativizationLibrary::GetNetworkEventBodyName(Function);
    if (!NetworkEventBody.IsEmpty())
    {
        return NetworkEventBody + TEXT("_Implementation");
    }
    FString MethodName = UBlueprintNativizationLibrary::GetUniqueFunctionName(Function, TEXT(""));
    if (MethodName.IsEmpty() || MethodName == TEXT("None"))
    {
        UFunction* RootFunction = UBlueprintNativizationLibrary::GetRootFunctionDeclaration(Function);
        MethodName = UBlueprintNativizationLibrary::GetUniqueFunctionName(RootFunction, RootFunction->GetName());
    }
    if (Function->HasAllFunctionFlags(FUNC_Native | FUNC_Event))
    {
        return MethodName + TEXT("_Implementation");
    }
    FFunctionDescriptor NativeMethod;
    if (UBlueprintNativizationSettingsLibrary::FindNativeByClassAndName(Call->GetBlueprint()->GeneratedClass,
        Function->GetName(), NativeMethod))
    {
        return NativeMethod.FunctionName;
    }
    UFunction* RootFunction = UBlueprintNativizationLibrary::GetRootFunctionDeclaration(Function);
    if (RootFunction->GetOwnerClass()->HasAnyClassFlags(CLASS_Native) &&
        RootFunction->HasAnyFunctionFlags(FUNC_BlueprintEvent) && !RootFunction->HasAnyFunctionFlags(FUNC_Native))
    {
        for (UClass* Parent = Call->GetBlueprint()->GeneratedClass->GetSuperClass(); Parent && !Parent->HasAnyClassFlags(CLASS_Native); Parent = Parent->GetSuperClass())
        {
            UBlueprint* ParentBlueprint = Cast<UBlueprint>(Parent->ClassGeneratedBy);
            if (ParentBlueprint && UBlueprintNativizationLibrary::FindEntryNodeByFunctionName(ParentBlueprint, RootFunction->GetFName()))
            {
                return RootFunction->GetName() + TEXT("_Implementation");
            }
        }
        return FString();
    }
    if (Function->GetOwnerClass()->ClassGeneratedBy && !Function->HasAnyFunctionFlags(FUNC_Private | FUNC_Net | FUNC_Static))
    {
        MethodName += TEXT("_Implementation");
    }
    return MethodName;
}

FString UCallParentTranslatorObject::GenerateInstanceFunctionCall(UK2Node_CallFunction* Call,
    const FString& Target, const FString& Arguments) const
{
    const FString ParentMethod = GetParentMethodName(CastChecked<UK2Node_CallParentFunction>(Call));
    if (ParentMethod.IsEmpty())
    {
        if (FProperty* ReturnProperty = Call->GetTargetFunction()->GetReturnProperty())
        {
            return UBlueprintNativizationLibrary::GetPropertyType(ReturnProperty) + TEXT("{}");
        }
        return FString();
    }
    FString ParentArguments = Arguments;
    FFunctionDescriptor ConstructionMethod;
    if (FConstructionScriptTranslation::TranslateNativeEvent(Call->GetBlueprint()->GeneratedClass,
        Call->GetTargetFunction()->GetName(), ConstructionMethod))
    {
        ParentArguments = TEXT("Transform");
    }
    return TEXT("Super::") + ParentMethod +
        TEXT("(") + ParentArguments + TEXT(")");
}
