#include "TranslatorObjects/ConvertAssetTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_ConvertAsset.h"
#include "Kismet/KismetSystemLibrary.h"
#include "NativeApiCatalog.h"
#include "NativeGenerationContext.h"
#include "RetainedBlueprintGeneration.h"
#include "TranslatorObjects/CallFunctionTranslatorObject.h"
#include "TranslatorObjects/StructTranslatorObject.h"

UConvertAssetTranslatorObject::UConvertAssetTranslatorObject()
{
    ClassNodeForCheck = {UK2Node_ConvertAsset::StaticClass()};
}

FGenerateResultStruct UConvertAssetTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node,
    UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem)
{
    UK2Node_ConvertAsset* Conversion = CastChecked<UK2Node_ConvertAsset>(Node);
    UEdGraphPin* Input = Conversion->FindPin(TEXT("Input"), EGPD_Input);
    UClass* TargetClass = Cast<UClass>(Pin->PinType.PinSubCategoryObject.Get());
    if (!Input || !TargetClass || Input->PinType.IsContainer() || Pin->PinType.IsContainer())
    {
        FNativeApiCatalog::AddError(TEXT("Asset conversion has no concrete scalar contract: ") + Node->GetPathName());
        return {};
    }

    FName FunctionName;
    bool bConvertsToSoft = false;
    bool bConvertsClasses = false;
    if (Input->PinType.PinCategory == UEdGraphSchema_K2::PC_Object &&
        Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject)
    {
        FunctionName = GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, Conv_ObjectToSoftObjectReference);
        bConvertsToSoft = true;
    }
    else if (Input->PinType.PinCategory == UEdGraphSchema_K2::PC_Class &&
        Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass)
    {
        FunctionName = GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, Conv_ClassToSoftClassReference);
        bConvertsToSoft = true;
        bConvertsClasses = true;
    }
    else if (Input->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject &&
        Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object)
    {
        FunctionName = GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, Conv_SoftObjectReferenceToObject);
    }
    else if (Input->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass &&
        Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class)
    {
        FunctionName = GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, Conv_SoftClassReferenceToClass);
        bConvertsClasses = true;
    }
    else
    {
        FNativeApiCatalog::AddError(TEXT("Unsupported asset conversion contract: ") + Node->GetPathName());
        return {};
    }

    FGenerateResultStruct Result = Subsystem->GenerateInputParameterCodeForNode(Node, Input, 0, MacroStack);
    FNativeGenerationContext::AddImplementationInclude(TEXT("#include \"Kismet/KismetSystemLibrary.h\""));
    FNativeGenerationContext::AddImplementationInclude(TEXT("#include \"UObject/SoftObjectPtr.h\""));
    FString Include;
    if (Subsystem->GetIncludeFromField(TargetClass, Include))
    {
        FNativeGenerationContext::AddImplementationInclude(Include);
    }
    FStructTranslationResult Requirements;
    Requirements.ModuleDependencies.Add(TEXT("Engine"));
    FNativeGenerationContext::AddRequirements(Requirements);
    UFunction* Function = UKismetSystemLibrary::StaticClass()->FindFunctionByName(FunctionName);
    const TArray<FString> Arguments = {Result.Code};
    const FString Invocation = GetMutableDefault<UCallFunctionTranslatorObject>()->GenerateStaticFunctionCall(
        Function, Result.Code, &Arguments);
    if (bConvertsToSoft)
    {
        FEdGraphPinType OutputType = Pin->PinType;
        Result.Code = FString::Format(TEXT("{0}(({1}).ToSoftObjectPath())"),
            {UBlueprintNativizationLibrary::GetPinType(OutputType, false), Invocation});
    }
    else if (TargetClass == UObject::StaticClass())
    {
        Result.Code = Invocation;
    }
    else if (bConvertsClasses)
    {
        TGuardValue<bool> ClassFilterScope(FNativeGenerationContext::bGeneratingClassFilter, true);
        Result.Code = FString::Format(TEXT("[](UClass* SourceClass, UClass* TargetClass) -> UClass*\n{\nif (SourceClass && TargetClass && SourceClass->IsChildOf(TargetClass))\n{\nreturn SourceClass;\n}\nreturn nullptr;\n}({0}, {1})"),
            {Invocation, FRetainedBlueprintGeneration::GetClassValue(TargetClass)});
    }
    else if (FRetainedBlueprintGeneration::IsRetained(TargetClass))
    {
        Result.Code = FString::Format(TEXT("[](UObject* SourceObject, UClass* TargetClass) -> {0}*\n{\nif (SourceObject && TargetClass && SourceObject->IsA(TargetClass))\n{\nreturn Cast<{0}>(SourceObject);\n}\nreturn nullptr;\n}({1}, {2})"),
            {FRetainedBlueprintGeneration::GetClassName(TargetClass), Invocation,
                FRetainedBlueprintGeneration::GetClassValue(TargetClass)});
    }
    else
    {
        Result.Code = FString::Format(TEXT("Cast<{0}>({1})"),
            {FRetainedBlueprintGeneration::GetClassName(TargetClass), Invocation});
    }
    return Result;
}
