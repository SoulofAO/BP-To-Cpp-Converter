#include "TranslatorObjects/LiteralTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"

FObjectPropertyBase* ULiteralTranslatorObject::FindStorageProperty(UK2Node_Literal* Literal) const
{
    UClass* OwnerClass = Literal->GetBlueprint()->GeneratedClass;
    for (TFieldIterator<FObjectPropertyBase> Property(OwnerClass); Property; ++Property)
    {
        if (!Property->HasAnyPropertyFlags(CPF_BlueprintVisible) &&
            Property->GetName().EndsWith(TEXT("_RefProperty")) &&
            Property->GetObjectPropertyValue_InContainer(OwnerClass->GetDefaultObject()) == Literal->GetObjectRef())
        {
            return *Property;
        }
    }
    return nullptr;
}

FGenerateResultStruct ULiteralTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node,
    UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem)
{
    UK2Node_Literal* Literal = CastChecked<UK2Node_Literal>(Node);
    if (Literal->GetObjectRef() && Literal->GetObjectRef()->IsAsset())
    {
        if (FObjectPropertyBase* Property = FindStorageProperty(Literal))
        {
            return UBlueprintNativizationLibrary::GetUniquePropertyName(Property);
        }
        return Subsystem->GetNodeStateName(Node, Literal->GetObjectRef()->GetName());
    }
    FEdGraphPinType Type = Literal->GetValuePin()->PinType;
    if (Literal->GetObjectRef() && Literal->GetObjectRef()->IsA<UClass>())
    {
        Type.PinCategory = UEdGraphSchema_K2::PC_Class;
    }
    return UBlueprintNativizationLibrary::GenerateOneLineDefaultConstructor(Pin->GetName(), Type,
        FString(), Literal->GetObjectRef(), Subsystem->LeftAllAssetRefInBlueprint);
}

TSet<FString> ULiteralTranslatorObject::GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem)
{
    TSet<FString> Includes;
    UObject* Value = CastChecked<UK2Node_Literal>(Node)->GetObjectRef();
    if (Value)
    {
        UClass* ValueClass = Cast<UClass>(Value);
        if (!ValueClass)
        {
            ValueClass = Value->GetClass();
        }
        FString Include;
        if (Subsystem->GetIncludeFromField(ValueClass, Include))
        {
            Includes.Add(Include);
        }
    }
    return Includes;
}

TSet<FString> ULiteralTranslatorObject::GenerateCSPrivateDependencyModuleNames(UK2Node* Node, UNativizationV2Subsystem* Subsystem)
{
    TSet<FString> Modules;
    UObject* Value = CastChecked<UK2Node_Literal>(Node)->GetObjectRef();
    if (Value)
    {
        UClass* ValueClass = Cast<UClass>(Value);
        if (!ValueClass)
        {
            ValueClass = Value->GetClass();
        }
        FString Package = ValueClass->GetOutermost()->GetName();
        if (Package.RemoveFromStart(TEXT("/Script/")))
        {
            Modules.Add(FString::Format(TEXT("\"{0}\""), { Package }));
        }
    }
    return Modules;
}

FString ULiteralTranslatorObject::GenerateGlobalVariables(UK2Node* Node, FString Prefix, UNativizationV2Subsystem* Subsystem)
{
    UK2Node_Literal* Literal = CastChecked<UK2Node_Literal>(Node);
    UObject* Value = Literal->GetObjectRef();
    if (!Value || !Value->IsAsset())
    {
        return FString();
    }
    if (FindStorageProperty(Literal))
    {
        return FString();
    }
    FString Declaration = TEXT("UPROPERTY()\n");
    if (Subsystem->LeftAllAssetRefInBlueprint)
    {
        Declaration = TEXT("UPROPERTY(EditDefaultsOnly, Category=\"Assets\")\n");
    }
    return Declaration + UBlueprintNativizationLibrary::GetPinType(Literal->GetValuePin()->PinType, false) +
        TEXT(" ") + Subsystem->GetNodeStateName(Node, Value->GetName()) + TEXT(" = nullptr;\n");
}

FString ULiteralTranslatorObject::GenerateConstruction(UK2Node* Node, FString Prefix, UNativizationV2Subsystem* Subsystem)
{
    UK2Node_Literal* Literal = CastChecked<UK2Node_Literal>(Node);
    UObject* Value = Literal->GetObjectRef();
    if (!Value || !Value->IsAsset() || Subsystem->LeftAllAssetRefInBlueprint)
    {
        return FString();
    }
    FString Storage;
    if (FObjectPropertyBase* Property = FindStorageProperty(Literal))
    {
        Storage = UBlueprintNativizationLibrary::GetUniquePropertyName(Property);
    }
    else
    {
        Storage = Subsystem->GetNodeStateName(Node, Value->GetName());
    }
    return Prefix + Storage + TEXT(" = ") +
        UBlueprintNativizationLibrary::GenerateOneLineDefaultConstructor(Literal->GetValuePin()->GetName(),
            Literal->GetValuePin()->PinType, FString(), Value, false) + TEXT(";\n");
}

TSet<FString> ULiteralTranslatorObject::GenerateHeaderIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem)
{
    return GenerateCppIncludeInstructions(Node, Subsystem);
}
