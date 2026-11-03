#include "TranslatorObjects/Latens/AsyncTaskTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"
#include "RetainedBlueprintGeneration.h"
#include "NativeApiCatalog.h"
#include "EdGraphSchema_K2.h"
#include "Misc/PackageName.h"

bool UAsyncTaskTranslatorObject::CanApply(UK2Node* Node)
{
    if (Node->GetClass()->GetFName() == TEXT("K2Node_LatentGameplayTaskCall"))
    {
        return Node->FindPin(TEXT("Class"), EGPD_Input) == nullptr;
    }
    return Node->GetClass()->GetFName() == TEXT("K2Node_AsyncAction") || Node->GetClass()->GetFName() == TEXT("K2Node_PlayMontage");
}

static UClass* GetAsyncProxyClass(UK2Node* Node)
{
    return CastChecked<UClass>(FindFProperty<FObjectPropertyBase>(Node->GetClass(), TEXT("ProxyClass"))->GetObjectPropertyValue_InContainer(Node));
}

static FName GetAsyncActivationFunction(UK2Node* Node)
{
    return FindFProperty<FNameProperty>(Node->GetClass(), TEXT("ProxyActivateFunctionName"))->GetPropertyValue_InContainer(Node);
}

void UAsyncTaskTranslatorObject::GenerateNewFunction(UK2Node* Node, TArray<UK2Node*> Path,
    TArray<FGenerateFunctionStruct>& Entries, UNativizationV2Subsystem* Subsystem)
{
    for (TFieldIterator<FMulticastDelegateProperty> Delegate(GetAsyncProxyClass(Node)); Delegate; ++Delegate)
    {
        if (!Node->FindPin(Delegate->GetFName(), EGPD_Output))
        {
            continue;
        }
        FGenerateFunctionStruct Entry;
        Entry.Node = Node;
        Entry.MacroStack = Path;
        Entry.Name = *Subsystem->GetNodeStateName(Node, TEXT("Async") + Delegate->GetName());
        Entry.EntryPinName = Delegate->GetName();
        Entry.bIsLatentContinuation = true;
        Entry.bRequiresReflection = true;
        for (TFieldIterator<FProperty> Parameter(Delegate->SignatureFunction); Parameter && Parameter->HasAnyPropertyFlags(CPF_Parm); ++Parameter)
        {
            if (!Parameter->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                Entry.CustomParametersString.Add(UBlueprintNativizationLibrary::GetPropertyType(*Parameter) + TEXT(" ") +
                    Subsystem->GetNodeStateName(Node, TEXT("Async") + Delegate->GetName() + Parameter->GetName()));
            }
        }
        Entries.AddUnique(Entry);
    }
}

FString UAsyncTaskTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName,
    TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations,
    UNativizationV2Subsystem* Subsystem)
{
    FString Content;
    if (FMulticastDelegateProperty* Delegate = FindFProperty<FMulticastDelegateProperty>(GetAsyncProxyClass(Node), FName(*EntryPinName)))
    {
        for (TFieldIterator<FProperty> Parameter(Delegate->SignatureFunction); Parameter && Parameter->HasAnyPropertyFlags(CPF_Parm); ++Parameter)
        {
            if (UEdGraphPin* Output = Node->FindPin(Parameter->GetFName(), EGPD_Output))
            {
                Content += TEXT("this->") + UBlueprintNativizationLibrary::GetUniquePinName(Output, Subsystem->EntryNodes) + TEXT(" = ") +
                    Subsystem->GetNodeStateName(Node, TEXT("Async") + Delegate->GetName() + Parameter->GetName()) + TEXT(";\n");
            }
        }
        if (UEdGraphPin* Target = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Node->FindPin(FName(*EntryPinName)), 0, false, MacroStack, Subsystem))
        {
            Content += Subsystem->GenerateCodeFromNode(CastChecked<UK2Node>(Target->GetOwningNode()), Target->GetName(), VisitedNodes, Preparations, MacroStack);
        }
        return Content;
    }
    UFunction* Factory = CastChecked<UK2Node_BaseAsyncTask>(Node)->GetFactoryFunction();
    const FString Proxy = Subsystem->GetNodeStateName(Node, TEXT("AsyncProxy"), true);
    const FString FactoryName = Subsystem->GetNodeStateName(Node, TEXT("AsyncFactory"), true);
    const FString Parameters = Subsystem->GetNodeStateName(Node, TEXT("AsyncParameters"), true);
    FString NativeFactoryInvocation;
    TArray<FString> FactoryArguments;
    const bool bNativeFactory = FNativeApiCatalog::CanCallFunctionDirectly(Factory);
    if (bNativeFactory)
    {
        Content += TEXT("{\n");
    }
    else
    {
        Content += TEXT("{\nUFunction* ") + FactoryName + TEXT(" = FindObjectChecked<UFunction>(nullptr, ") +
            FRetainedBlueprintGeneration::Quote(Factory->GetPathName()) + TEXT(");\nFStructOnScope ") + Parameters + TEXT("(") + FactoryName + TEXT(");\n");
    }
    for (TFieldIterator<FProperty> Parameter(Factory); Parameter && Parameter->HasAnyPropertyFlags(CPF_Parm); ++Parameter)
    {
        if (Parameter->HasAnyPropertyFlags(CPF_ReturnParm))
        {
            continue;
        }
        UEdGraphPin* Input = Node->FindPin(Parameter->GetFName(), EGPD_Input);
        FGenerateResultStruct Value;
        const FString ParameterName = Parameter->GetName();
        const bool bImplicitSelf = (Factory->GetMetaData(TEXT("WorldContext")) == ParameterName || Factory->GetMetaData(TEXT("DefaultToSelf")) == ParameterName) &&
            (!Input || (Input->LinkedTo.IsEmpty() && !Input->DefaultObject && Input->DefaultValue.IsEmpty()));
        if (bImplicitSelf)
        {
            Value.Code = TEXT("this");
        }
        else if (Input)
        {
            Value = Subsystem->GenerateInputParameterCodeForNode(Node, Input, 0, MacroStack);
        }
        else
        {
            FNativeApiCatalog::AddError(TEXT("Cannot resolve an asynchronous factory argument: ") + Factory->GetPathName() + TEXT(".") + ParameterName);
            return {};
        }
        Content += GenerateNewPreparations(Preparations, Value.Preparations);
        Preparations.Append(Value.Preparations);
        if (bNativeFactory)
        {
            const FString ArgumentName = Subsystem->GetNodeStateName(Node, TEXT("FactoryArgument") + ParameterName, true);
            FString ArgumentDeclaration = TEXT("const auto ");
            if (Parameter->HasAnyPropertyFlags(CPF_ReferenceParm) && !Parameter->HasAnyPropertyFlags(CPF_ConstParm))
            {
                ArgumentDeclaration = TEXT("auto& ");
            }
            Content += ArgumentDeclaration + ArgumentName + TEXT(" = ") + Value.Code + TEXT(";\n");
            FactoryArguments.Add(ArgumentName);
        }
        else
        {
            Content += TEXT("{\nFProperty* ParameterProperty = FindFProperty<FProperty>(") + FactoryName + TEXT(", ") +
                FRetainedBlueprintGeneration::Quote(ParameterName) + TEXT(");\nvoid* ParameterAddress = ParameterProperty->ContainerPtrToValuePtr<void>(") + Parameters + TEXT(".GetStructMemory());\nconst auto ArgumentValue = ") + Value.Code + TEXT(";\n") +
                FRetainedBlueprintGeneration::Transfer(*Parameter, TEXT("ParameterProperty"), TEXT("ParameterAddress"), TEXT("ArgumentValue"), true) + TEXT("}\n");
        }
    }
    if (bNativeFactory)
    {
        FRetainedBlueprintGeneration::TryGenerateNativeCall(Factory, FString(), FactoryArguments, NativeFactoryInvocation);
        Content += TEXT("auto* ") + Proxy + TEXT(" = ") + NativeFactoryInvocation + TEXT(";\n");
    }
    else
    {
        Content += FactoryName + TEXT("->GetOwnerClass()->GetDefaultObject()->ProcessEvent(") + FactoryName + TEXT(", ") + Parameters + TEXT(".GetStructMemory());\n") +
            TEXT("UObject* ") + Proxy + TEXT(" = FindFProperty<FObjectPropertyBase>(") + FactoryName + TEXT(", ") +
            FRetainedBlueprintGeneration::Quote(Factory->GetReturnProperty()->GetName()) + TEXT(")->GetObjectPropertyValue_InContainer(") + Parameters + TEXT(".GetStructMemory());\n");
    }
    Content += TEXT("if (IsValid(") + Proxy + TEXT("))\n{\n");
    for (TFieldIterator<FMulticastDelegateProperty> Delegate(GetAsyncProxyClass(Node)); Delegate; ++Delegate)
    {
        if (!Node->FindPin(Delegate->GetFName(), EGPD_Output))
        {
            continue;
        }
        const FString Callback = Subsystem->GetNodeStateName(Node, TEXT("Async") + Delegate->GetName());
        Content += TEXT("{\nFScriptDelegate Callback;\nCallback.BindUFunction(this, ") + FRetainedBlueprintGeneration::Quote(Callback) +
            TEXT(");\nFMulticastDelegateProperty* DelegateProperty = FindFProperty<FMulticastDelegateProperty>(") + Proxy + TEXT("->GetClass(), ") +
            FRetainedBlueprintGeneration::Quote(Delegate->GetName()) + TEXT(");\nDelegateProperty->AddDelegate(Callback, ") + Proxy + TEXT(", DelegateProperty->ContainerPtrToValuePtr<void>(") + Proxy + TEXT("));\n}\n");
    }
    for (UEdGraphPin* Output : Node->Pins)
    {
        if (Output->Direction == EGPD_Output && Output->PinType.PinCategory == UEdGraphSchema_K2::PC_Object &&
            Output->PinType.PinSubCategoryObject == GetAsyncProxyClass(Node))
        {
            Content += TEXT("this->") + UBlueprintNativizationLibrary::GetUniquePinName(Output, Subsystem->EntryNodes) + TEXT(" = Cast<") +
                UBlueprintNativizationLibrary::GetUniqueFieldName(GetAsyncProxyClass(Node)) + TEXT(">(") + Proxy + TEXT(");\n");
        }
    }
    const FName Activation = GetAsyncActivationFunction(Node);
    if (!Activation.IsNone())
    {
        UFunction* ActivationFunction = GetAsyncProxyClass(Node)->FindFunctionByName(Activation);
        FString NativeActivation;
        const FString ActivationTarget = TEXT("Cast<") + FRetainedBlueprintGeneration::GetClassName(ActivationFunction->GetOwnerClass()) + TEXT(">(") + Proxy + TEXT(")");
        if (FRetainedBlueprintGeneration::TryGenerateNativeCall(ActivationFunction, ActivationTarget, {}, NativeActivation))
        {
            Content += NativeActivation + TEXT(";\n");
        }
        else
        {
            Content += Proxy + TEXT("->ProcessEvent(") + Proxy + TEXT("->FindFunctionChecked(") + FRetainedBlueprintGeneration::Quote(Activation.ToString()) + TEXT("), nullptr);\n");
        }
    }
    Content += TEXT("}\n}\n");
    if (UEdGraphPin* Target = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Node->FindPin(UEdGraphSchema_K2::PN_Then), 0, false, MacroStack, Subsystem))
    {
        Content += Subsystem->GenerateCodeFromNode(CastChecked<UK2Node>(Target->GetOwningNode()), Target->GetName(), VisitedNodes, Preparations, MacroStack);
    }
    return Content;
}

FString UAsyncTaskTranslatorObject::GenerateGlobalVariables(UK2Node* Node, FString Prefix, UNativizationV2Subsystem* Subsystem)
{
    FString Content;
    for (UEdGraphPin* Output : Node->Pins)
    {
        if (Output->Direction == EGPD_Output && !Output->ParentPin && Output->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
        {
            Content += TEXT("UPROPERTY(Transient)\n") + UBlueprintNativizationLibrary::GetPinType(Output->PinType, false) + TEXT(" ") +
                UBlueprintNativizationLibrary::GetUniquePinName(Output, Subsystem->EntryNodes) + TEXT("{};\n");
        }
    }
    return Content;
}

TSet<FString> UAsyncTaskTranslatorObject::GenerateLocalVariables(UK2Node* Node, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem)
{
    return {};
}

FGenerateResultStruct UAsyncTaskTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex,
    TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem)
{
    return TEXT("this->") + UBlueprintNativizationLibrary::GetUniquePinName(Pin, Subsystem->EntryNodes);
}

TSet<FString> UAsyncTaskTranslatorObject::GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem)
{
    TSet<FString> Includes = Super::GenerateCppIncludeInstructions(Node, Subsystem);
    Includes.Add(TEXT("#include \"UObject/StructOnScope.h\""));
    Includes.Add(TEXT("#include \"UObject/UnrealType.h\""));
    return Includes;
}

TSet<FString> UAsyncTaskTranslatorObject::GenerateHeaderIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem)
{
    return Super::GenerateCppIncludeInstructions(Node, Subsystem);
}

TSet<FString> UAsyncTaskTranslatorObject::GenerateCSPrivateDependencyModuleNames(UK2Node* Node, UNativizationV2Subsystem* Subsystem)
{
    UFunction* Factory = CastChecked<UK2Node_BaseAsyncTask>(Node)->GetFactoryFunction();
    if (Factory->GetOutermost()->GetName().StartsWith(TEXT("/Script/")))
    {
        return {TEXT("\"") + FPackageName::GetShortName(Factory->GetOutermost()) + TEXT("\"")};
    }
    return {};
}
