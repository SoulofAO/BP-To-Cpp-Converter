#include "TranslatorObjects/SpecializedStructTranslators.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSettings.h"
#include "NativeApiCatalog.h"
#include "StructInitialization.h"
#include "StructUtils/InstancedStruct.h"
#include "StructUtils/PropertyBag.h"
#include "EdGraphSchema_K2.h"
#include "UObject/UnrealType.h"
#include "RetainedBlueprintGeneration.h"

static FString GenerateInstancedContents(const UScriptStruct* Structure, const void* Memory,
    const FStructTranslationRequest& Request)
{
    FStructTranslationRequest Nested = Request;
    Nested.Structure = Structure;
    Nested.Property = nullptr;
    Nested.PropertyValue = Memory;
    Nested.BaseValue = nullptr;
    Nested.DestinationExpression.Reset();
    Nested.Operation = EStructTranslationOperation::PropertyValue;
    if (UBlueprintNativizationSettingsLibrary::FindStructTranslator(Structure) || Structure->IsNative())
    {
        return UBlueprintNativizationSettingsLibrary::TranslateStructure(Nested);
    }
    FEdGraphPinType Type;
    Type.PinCategory = UEdGraphSchema_K2::PC_Struct;
    Type.PinSubCategoryObject = const_cast<UScriptStruct*>(Structure);
    FString Text;
    Structure->ExportText(Text, Memory, nullptr, const_cast<UObject*>(Request.OwnerObject), PPF_None, nullptr);
    return UBlueprintNativizationLibrary::GenerateOneLineDefaultConstructor(Structure->GetName(), Type, Text, nullptr,
        Request.bKeepAssetReferencesInBlueprint);
}

static FString SchemaObjectExpression(const UObject* Object)
{
    if (!Object)
    {
        return TEXT("nullptr");
    }
    return TEXT("LoadObject<UObject>(nullptr, ") + FStructInitialization::Quote(Object->GetPathName()) + TEXT(")");
}

bool UInstancedValueStructTranslator::CanTranslate(const UScriptStruct* Structure) const
{
    return Structure == FInstancedStruct::StaticStruct() || Structure == FInstancedPropertyBag::StaticStruct();
}

bool UInstancedValueStructTranslator::TranslateSerializedFallback(const FStructTranslationRequest& Request, FStructTranslationResult& Result) const
{
    FStructOnScope Storage(Request.Structure);
    const void* Source = FStructInitialization::ReadValue(Request, Storage, Result.Error);
    if (!Source)
    {
        return false;
    }
    if (Request.Structure == FInstancedPropertyBag::StaticStruct())
    {
        if (static_cast<const FInstancedPropertyBag*>(Source)->GetPropertyBagStruct())
        {
            Result.Error = TEXT("ImportText cannot persist a generated property bag schema across editor restarts.");
            return false;
        }
    }
    else
    {
        const UScriptStruct* Nested = static_cast<const FInstancedStruct*>(Source)->GetScriptStruct();
        if (Nested && !Nested->IsNative() && Nested->HasAnyFlags(RF_Transient))
        {
            Result.Error = TEXT("ImportText cannot persist a transient instanced structure type.");
            return false;
        }
    }
    return FStructInitialization::Fallback(Request, Result);
}

bool UInstancedValueStructTranslator::Translate(const FStructTranslationRequest& Request, FStructTranslationResult& Result) const
{
    const bool bBag = Request.Structure == FInstancedPropertyBag::StaticStruct();
    FString Type = TEXT("FInstancedStruct");
    FString Header = TEXT("StructUtils/InstancedStruct.h");
    if (bBag)
    {
        Type = TEXT("FInstancedPropertyBag");
        Header = TEXT("StructUtils/PropertyBag.h");
    }
    if (FStructInitialization::Describe(Request, Result, Type, Header, TEXT("CoreUObject")))
    {
        return true;
    }
    if (!FStructInitialization::IsValueOperation(Request.Operation))
    {
        Result.Error = TEXT("Unsupported instanced value operation.");
        return false;
    }
    FStructOnScope Storage(Request.Structure);
    const void* Source = FStructInitialization::ReadValue(Request, Storage, Result.Error);
    if (!Source)
    {
        return false;
    }
    const int32 OriginalErrorCount = FNativeApiCatalog::GetErrors().Num();
    if (!bBag)
    {
        const auto& Value = *static_cast<const FInstancedStruct*>(Source);
        if (!Value.IsValid())
        {
            Result.Expression = TEXT("FInstancedStruct()");
            return true;
        }
        const FString Name = FStructInitialization::VariableName(Request, TEXT("InstancedValue"));
        const FString ValueType = UBlueprintNativizationLibrary::GetUniqueFieldName(const_cast<UScriptStruct*>(Value.GetScriptStruct()));
        const FString Contents = GenerateInstancedContents(Value.GetScriptStruct(), Value.GetMemory(), Request);
        if (FNativeApiCatalog::GetErrors().Num() != OriginalErrorCount)
        {
            Result.Error = TEXT("Instanced structure contents cannot be initialized completely.");
            return false;
        }
        Result.Preparations.Add(TEXT("FInstancedStruct ") + Name + TEXT(";"));
        if (FRetainedBlueprintGeneration::IsExternalStructure(Value.GetScriptStruct()))
        {
            Result.Preparations.Add(Name + TEXT(" = ") + Contents + TEXT(";"));
        }
        else
        {
            Result.Preparations.Add(Name + TEXT(".InitializeAs<") + ValueType + TEXT(">() = ") + Contents + TEXT(";"));
        }
        Result.Expression = Name;
        return true;
    }
    const auto& Value = *static_cast<const FInstancedPropertyBag*>(Source);
    const UPropertyBag* Schema = Value.GetPropertyBagStruct();
    if (!Schema)
    {
        Result.Expression = TEXT("FInstancedPropertyBag()");
        return true;
    }
    const FString Name = FStructInitialization::VariableName(Request, TEXT("PropertyBagValue"));
    const FString Descriptions = FStructInitialization::VariableName(Request, TEXT("PropertyDescriptions"));
    Result.Preparations.Add(TEXT("FInstancedPropertyBag ") + Name + TEXT(";"));
    Result.Preparations.Add(TEXT("TArray<FPropertyBagPropertyDesc> ") + Descriptions + TEXT(";"));
    for (const FPropertyBagPropertyDesc& Description : Schema->GetPropertyDescs())
    {
        const FString DescriptionName = FStructInitialization::VariableName(Request, Description.Name.ToString() + TEXT("Description"));
        FString Constructor = TEXT("FPropertyBagPropertyDesc ") + DescriptionName + TEXT("(FName(") +
            FStructInitialization::Quote(Description.Name.ToString()) + TEXT("), EPropertyBagPropertyType::") +
            StaticEnum<EPropertyBagPropertyType>()->GetNameStringByValue(static_cast<int64>(Description.ValueType));
        if (Description.ValueTypeObject)
        {
            Constructor += TEXT(", ") + SchemaObjectExpression(Description.ValueTypeObject.Get());
        }
        Result.Preparations.Add(Constructor + TEXT(");"));
        TArray<FString> Containers;
        for (uint32 ContainerIndex = 0; ContainerIndex < Description.ContainerTypes.Num(); ++ContainerIndex)
        {
            Containers.Add(TEXT("EPropertyBagContainerType::") + StaticEnum<EPropertyBagContainerType>()->GetNameStringByValue(
                static_cast<int64>(Description.ContainerTypes[ContainerIndex])));
        }
        if (!Containers.IsEmpty())
        {
            Result.Preparations.Add(DescriptionName + TEXT(".ContainerTypes = FPropertyBagContainerTypes{") + FString::Join(Containers, TEXT(", ")) + TEXT("};"));
        }
        Result.Preparations.Add(FString::Format(TEXT("{0}.ID = FGuid({1}u, {2}u, {3}u, {4}u);"),
            {DescriptionName, LexToString(Description.ID.A), LexToString(Description.ID.B),
                LexToString(Description.ID.C), LexToString(Description.ID.D)}));
        if (Description.PropertyFlags != FPropertyBagPropertyDesc().PropertyFlags)
        {
            Result.Preparations.Add(DescriptionName + TEXT(".PropertyFlags = ") + LexToString(Description.PropertyFlags) + TEXT("ull;"));
        }
        if (Description.KeyType != EPropertyBagPropertyType::None)
        {
            Result.Preparations.Add(DescriptionName + TEXT(".KeyType = EPropertyBagPropertyType::") +
                StaticEnum<EPropertyBagPropertyType>()->GetNameStringByValue(static_cast<int64>(Description.KeyType)) + TEXT(";"));
        }
        if (Description.KeyTypeObject)
        {
            Result.Preparations.Add(DescriptionName + TEXT(".KeyTypeObject = ") + SchemaObjectExpression(Description.KeyTypeObject.Get()) + TEXT(";"));
        }
#if WITH_EDITORONLY_DATA
        for (const FPropertyBagPropertyDescMetaData& Metadata : Description.MetaData)
        {
            Result.Preparations.Add(TEXT("#if WITH_EDITORONLY_DATA\n") + DescriptionName + TEXT(".MetaData.Emplace(FName(") +
                FStructInitialization::Quote(Metadata.Key.ToString()) + TEXT("), ") + FStructInitialization::Quote(Metadata.Value) + TEXT(");\n#endif"));
        }
        if (Description.MetaClass)
        {
            Result.Preparations.Add(TEXT("#if WITH_EDITORONLY_DATA\n") + DescriptionName + TEXT(".MetaClass = LoadObject<UClass>(nullptr, ") +
                FStructInitialization::Quote(Description.MetaClass->GetPathName()) + TEXT(");\n#endif"));
        }
#endif
        Result.Preparations.Add(Descriptions + TEXT(".Add(") + DescriptionName + TEXT(");"));
    }
    Result.Preparations.Add(Name + TEXT(".AddProperties(") + Descriptions + TEXT(");"));
    Result.HeaderIncludes.Add(TEXT("UObject/UnrealType.h"));
    for (const FPropertyBagPropertyDesc& Description : Schema->GetPropertyDescs())
    {
        const FProperty* Property = Description.CachedProperty;
        if (!Property)
        {
            Result.Error = TEXT("Property bag has no resolved property for ") + Description.Name.ToString();
            return false;
        }
        const FString Item = FStructInitialization::VariableName(Request, Description.Name.ToString() + TEXT("Value"));
        const FString PropertyName = FStructInitialization::VariableName(Request, Description.Name.ToString() + TEXT("Property"));
        const FString Contents = FStructInitialization::PropertyExpression(const_cast<FProperty*>(Property),
            Property->ContainerPtrToValuePtr<void>(Value.GetValue().GetMemory()), Request);
        const FString PropertyType = UBlueprintNativizationLibrary::GetPropertyType(const_cast<FProperty*>(Property));
        FString Setter;
        if (Description.ContainerTypes.Num() == 0)
        {
            switch (Description.ValueType)
            {
            case EPropertyBagPropertyType::Bool: Setter = TEXT("SetValueBool"); break;
            case EPropertyBagPropertyType::Int32: Setter = TEXT("SetValueInt32"); break;
            case EPropertyBagPropertyType::Int64: Setter = TEXT("SetValueInt64"); break;
            case EPropertyBagPropertyType::UInt64: Setter = TEXT("SetValueUInt64"); break;
            case EPropertyBagPropertyType::Float: Setter = TEXT("SetValueFloat"); break;
            case EPropertyBagPropertyType::Double: Setter = TEXT("SetValueDouble"); break;
            case EPropertyBagPropertyType::Name: Setter = TEXT("SetValueName"); break;
            case EPropertyBagPropertyType::String: Setter = TEXT("SetValueString"); break;
            case EPropertyBagPropertyType::Text: Setter = TEXT("SetValueText"); break;
            case EPropertyBagPropertyType::Struct: Setter = TEXT("SetValueStruct"); break;
            case EPropertyBagPropertyType::Object: Setter = TEXT("SetValueObject"); break;
            default: break;
            }
        }
        if (!Setter.IsEmpty())
        {
            Result.Preparations.Add(Name + TEXT(".") + Setter + TEXT("(FName(") +
                FStructInitialization::Quote(Description.Name.ToString()) + TEXT("), ") + Contents + TEXT(");"));
            continue;
        }
        Result.Preparations.Add(PropertyType + TEXT(" ") + Item + TEXT(" = ") + Contents + TEXT(";"));
        Result.Preparations.Add(TEXT("FProperty* ") + PropertyName + TEXT(" = FindFProperty<FProperty>(") +
            Name + TEXT(".GetPropertyBagStruct(), ") + FStructInitialization::Quote(Property->GetName()) + TEXT(");"));
        Result.Preparations.Add(PropertyName + TEXT("->CopyCompleteValue(") + PropertyName + TEXT("->ContainerPtrToValuePtr<void>(") +
            Name + TEXT(".GetMutableValue().GetMemory()), &") + Item + TEXT(");"));
    }
    if (FNativeApiCatalog::GetErrors().Num() != OriginalErrorCount)
    {
        Result.Error = TEXT("Property bag contents cannot be initialized completely.");
        return false;
    }
    Result.Expression = Name;
    return true;
}
