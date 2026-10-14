#include "StructInitialization.h"
#include "BlueprintNativizationLibrary.h"
#include "UObject/UnrealType.h"
#include <charconv>

static void ClearInitializationAssetValues(FProperty* Property, void* Value)
{
    if (auto* Object = CastField<FObjectPropertyBase>(Property))
    {
        UObject* Asset = Object->GetObjectPropertyValue(Value);
        if (Asset && Asset->IsAsset() && !Asset->IsA<UField>())
        {
            Object->SetObjectPropertyValue(Value, nullptr);
        }
    }
    else if (auto* Structure = CastField<FStructProperty>(Property))
    {
        for (TFieldIterator<FProperty> Member(Structure->Struct); Member; ++Member)
        {
            ClearInitializationAssetValues(*Member, Member->ContainerPtrToValuePtr<void>(Value));
        }
    }
    else if (auto* Array = CastField<FArrayProperty>(Property))
    {
        FScriptArrayHelper Values(Array, Value);
        for (int32 Index = 0; Index < Values.Num(); ++Index)
        {
            ClearInitializationAssetValues(Array->Inner, Values.GetRawPtr(Index));
        }
    }
    else if (auto* Map = CastField<FMapProperty>(Property))
    {
        FScriptMapHelper Values(Map, Value);
        for (int32 Index = 0; Index < Values.GetMaxIndex(); ++Index)
        {
            if (Values.IsValidIndex(Index))
            {
                ClearInitializationAssetValues(Map->KeyProp, Values.GetKeyPtr(Index));
                ClearInitializationAssetValues(Map->ValueProp, Values.GetValuePtr(Index));
            }
        }
        Values.Rehash();
    }
    else if (auto* Set = CastField<FSetProperty>(Property))
    {
        FScriptSetHelper Values(Set, Value);
        for (int32 Index = 0; Index < Values.GetMaxIndex(); ++Index)
        {
            if (Values.IsValidIndex(Index))
            {
                ClearInitializationAssetValues(Set->ElementProp, Values.GetElementPtr(Index));
            }
        }
        Values.Rehash();
    }
}

bool FStructInitialization::IsValueOperation(EStructTranslationOperation Operation)
{
    return Operation == EStructTranslationOperation::DefaultValue ||
        Operation == EStructTranslationOperation::PropertyValue ||
        Operation == EStructTranslationOperation::SerializedValue;
}

const void* FStructInitialization::ReadValue(const FStructTranslationRequest& Request, FStructOnScope& Storage, FString& Error)
{
    if (Request.PropertyValue && !Request.bKeepAssetReferencesInBlueprint)
    {
        return Request.PropertyValue;
    }
    if (Request.PropertyValue)
    {
        Request.Structure->CopyScriptStruct(Storage.GetStructMemory(), Request.PropertyValue);
    }
    if (!Request.SerializedValue.IsEmpty() &&
        !Request.Structure->ImportText(*Request.SerializedValue, Storage.GetStructMemory(),
            const_cast<UObject*>(Request.OwnerObject), PPF_None, nullptr, Request.Structure->GetName()))
    {
        Error = TEXT("Invalid serialized structure value.");
        return nullptr;
    }
    if (Request.bKeepAssetReferencesInBlueprint)
    {
        for (TFieldIterator<FProperty> Member(Request.Structure); Member; ++Member)
        {
            ClearInitializationAssetValues(*Member, Member->ContainerPtrToValuePtr<void>(Storage.GetStructMemory()));
        }
    }
    return Storage.GetStructMemory();
}

FString FStructInitialization::VariableName(const FStructTranslationRequest& Request, const FString& Purpose)
{
    return UBlueprintNativizationLibrary::GenerateLambdaConstructorUniqueVariableName(Request.OwnerClass, Purpose);
}

FString FStructInitialization::Number(double Value)
{
    ANSICHAR Buffer[64]{};
    const auto Conversion = std::to_chars(Buffer, Buffer + UE_ARRAY_COUNT(Buffer), Value);
    FString Result(static_cast<int32>(Conversion.ptr - Buffer), ANSI_TO_TCHAR(Buffer));
    if (!Result.Contains(TEXT(".")) && !Result.Contains(TEXT("e")))
    {
        Result += TEXT(".0");
    }
    return Result;
}

FString FStructInitialization::Number(float Value)
{
    ANSICHAR Buffer[64]{};
    const auto Conversion = std::to_chars(Buffer, Buffer + UE_ARRAY_COUNT(Buffer), Value);
    FString Result(static_cast<int32>(Conversion.ptr - Buffer), ANSI_TO_TCHAR(Buffer));
    if (!Result.Contains(TEXT(".")) && !Result.Contains(TEXT("e")))
    {
        Result += TEXT(".0");
    }
    return Result + TEXT("f");
}

FString FStructInitialization::Quote(const FString& Value)
{
    return TEXT("TEXT(\"") + Value.ReplaceCharWithEscapedChar() + TEXT("\")");
}

FString FStructInitialization::PropertyExpression(FProperty* Property, const void* Value, const FStructTranslationRequest& Request)
{
    return UBlueprintNativizationLibrary::GenerateOneLineDefaultConstructorForProperty(Property,
        Request.bKeepAssetReferencesInBlueprint, const_cast<void*>(Value), Request.OwnerExpression);
}

bool FStructInitialization::Describe(const FStructTranslationRequest& Request, FStructTranslationResult& Result,
    const FString& NativeType, const FString& Include, const FString& Module)
{
    Result.NativeType = NativeType;
    Result.HeaderIncludes.Add(Include);
    Result.ModuleDependencies.Add(Module);
    if (Request.Operation == EStructTranslationOperation::DescribeType)
    {
        Result.Expression = NativeType;
        return true;
    }
    if (Request.Operation == EStructTranslationOperation::Conversion && Request.DestinationType == NativeType)
    {
        Result.Expression = Request.SourceExpression;
        return true;
    }
    return false;
}

bool UStructTranslatorObject::TranslateSerializedFallback(const FStructTranslationRequest& Request, FStructTranslationResult& Result) const
{
    Result.Error = TEXT("The selected structure translator does not support serialized fallback.");
    return false;
}

static FString ExportInitializationProperty(FProperty* Property, const void* Value, UObject* Owner);

static FString ExportInitializationFields(const UScriptStruct* Structure, const void* Value, UObject* Owner)
{
    TArray<FString> Fields;
    for (TFieldIterator<FProperty> Field(Structure); Field; ++Field)
    {
        if (!Field->ShouldPort(PPF_None))
        {
            continue;
        }
        for (int32 Index = 0; Index < Field->ArrayDim; ++Index)
        {
            FString Name = Field->GetName();
            if (Field->ArrayDim > 1)
            {
                Name += TEXT("[") + LexToString(Index) + TEXT("]");
            }
            Fields.Add(Name + TEXT("=") + ExportInitializationProperty(*Field,
                Field->ContainerPtrToValuePtr<void>(Value, Index), Owner));
        }
    }
    return TEXT("(") + FString::Join(Fields, TEXT(",")) + TEXT(")");
}

static FString ExportInitializationProperty(FProperty* Property, const void* Value, UObject* Owner)
{
    if (auto* Numeric = CastField<FNumericProperty>(Property))
    {
        if (Numeric->IsFloatingPoint())
        {
            return FStructInitialization::Number(Numeric->GetFloatingPointPropertyValue(Value));
        }
    }
    if (auto* Structure = CastField<FStructProperty>(Property))
    {
        if (!(Structure->Struct->StructFlags & STRUCT_ExportTextItemNative))
        {
            return ExportInitializationFields(Structure->Struct, Value, Owner);
        }
    }
    TArray<FString> Elements;
    bool bContainer = false;
    if (auto* Array = CastField<FArrayProperty>(Property))
    {
        bContainer = true;
        FScriptArrayHelper Values(Array, Value);
        for (int32 Index = 0; Index < Values.Num(); ++Index)
        {
            Elements.Add(ExportInitializationProperty(Array->Inner, Values.GetRawPtr(Index), Owner));
        }
    }
    else if (auto* Map = CastField<FMapProperty>(Property))
    {
        bContainer = true;
        FScriptMapHelper Values(Map, Value);
        for (int32 Index = 0; Index < Values.GetMaxIndex(); ++Index)
        {
            if (Values.IsValidIndex(Index))
            {
                Elements.Add(TEXT("(") + ExportInitializationProperty(Map->KeyProp, Values.GetKeyPtr(Index), Owner) +
                    TEXT(",") + ExportInitializationProperty(Map->ValueProp, Values.GetValuePtr(Index), Owner) + TEXT(")"));
            }
        }
    }
    else if (auto* Set = CastField<FSetProperty>(Property))
    {
        bContainer = true;
        FScriptSetHelper Values(Set, Value);
        for (int32 Index = 0; Index < Values.GetMaxIndex(); ++Index)
        {
            if (Values.IsValidIndex(Index))
            {
                Elements.Add(ExportInitializationProperty(Set->ElementProp, Values.GetElementPtr(Index), Owner));
            }
        }
    }
    if (bContainer)
    {
        return TEXT("(") + FString::Join(Elements, TEXT(",")) + TEXT(")");
    }
    FString Text;
    Property->ExportTextItem_Direct(Text, Value, nullptr, Owner, PPF_Delimited);
    return Text;
}

bool FStructInitialization::Fallback(const FStructTranslationRequest& Request, FStructTranslationResult& Result,
    TFunction<void(void*)> FinalizeValue, const FString& FinalizeInstruction)
{
    FStructOnScope Source(Request.Structure);
    FString Error;
    const void* Value = ReadValue(Request, Source, Error);
    if (!Value)
    {
        Result.Error = Error;
        return false;
    }
    FString Serialized;
    Request.Structure->ExportText(Serialized, Value, nullptr, const_cast<UObject*>(Request.OwnerObject), PPF_None, nullptr);
    FStructOnScope Restored(Request.Structure);
    if (!Request.Structure->ImportText(*Serialized, Restored.GetStructMemory(), const_cast<UObject*>(Request.OwnerObject),
        PPF_None, nullptr, Request.Structure->GetName()))
    {
        Result.Error = TEXT("ImportText fallback cannot parse the exported structure value.");
        return false;
    }
    if (FinalizeValue)
    {
        FinalizeValue(Restored.GetStructMemory());
    }
    if (!Request.Structure->CompareScriptStruct(Value, Restored.GetStructMemory(), PPF_None))
    {
        const FString PreciseText = ExportInitializationFields(Request.Structure, Value, const_cast<UObject*>(Request.OwnerObject));
        FStructOnScope PreciseValue(Request.Structure);
        if (Request.Structure->ImportText(*PreciseText, PreciseValue.GetStructMemory(), const_cast<UObject*>(Request.OwnerObject),
            PPF_None, nullptr, Request.Structure->GetName()))
        {
            if (FinalizeValue)
            {
                FinalizeValue(PreciseValue.GetStructMemory());
            }
            if (Request.Structure->CompareScriptStruct(Value, PreciseValue.GetStructMemory(), PPF_None))
            {
                Serialized = PreciseText;
                Request.Structure->CopyScriptStruct(Restored.GetStructMemory(), PreciseValue.GetStructMemory());
            }
        }
    }
    if (!Request.Structure->CompareScriptStruct(Value, Restored.GetStructMemory(), PPF_None))
    {
        Result.Error = TEXT("ImportText fallback does not preserve this structure value.");
        for (TFieldIterator<FProperty> Field(Request.Structure); Field; ++Field)
        {
            const void* OriginalValue = Field->ContainerPtrToValuePtr<void>(Value);
            const void* RestoredValue = Field->ContainerPtrToValuePtr<void>(Restored.GetStructMemory());
            if (!Field->Identical(OriginalValue, RestoredValue, PPF_None))
            {
                FString OriginalText;
                FString RestoredText;
                Field->ExportTextItem_Direct(OriginalText, OriginalValue, nullptr, nullptr, PPF_None);
                Field->ExportTextItem_Direct(RestoredText, RestoredValue, nullptr, nullptr, PPF_None);
                Result.Error += FString::Format(TEXT(" {0}: original={1}; restored={2}."), {Field->GetName(), OriginalText, RestoredText});
            }
        }
        return false;
    }
    Result = {};
    Result.NativeType = UBlueprintNativizationLibrary::GetUniqueFieldName(const_cast<UScriptStruct*>(Request.Structure));
    Result.HeaderIncludes.Add(TEXT("UObject/UnrealType.h"));
    Result.HeaderIncludes.Add(TEXT("UObject/StructOnScope.h"));
    const FString Variable = VariableName(Request, Request.Structure->GetName() + TEXT("Value"));
    const FString Structure = VariableName(Request, TEXT("DefaultStructure"));
    const FString StorageName = VariableName(Request, Request.Structure->GetName() + TEXT("Storage"));
    Result.Preparations.Add(TEXT("UScriptStruct* ") + Structure + TEXT(" = FindObjectChecked<UScriptStruct>(nullptr, ") +
        Quote(Request.Structure->GetPathName()) + TEXT(");"));
    Result.Preparations.Add(TEXT("FStructOnScope ") + StorageName + TEXT("(") + Structure + TEXT(");"));
    Result.Preparations.Add(Result.NativeType + TEXT("& ") + Variable + TEXT(" = *reinterpret_cast<") + Result.NativeType +
        TEXT("*>(") + StorageName + TEXT(".GetStructMemory());"));
    Result.Preparations.Add(Structure + TEXT("->ImportText(") + Quote(Serialized) + TEXT(", &") + Variable +
        TEXT(", nullptr, PPF_None, nullptr, ") + Structure + TEXT("->GetName());"));
    Result.Expression = Variable;
    if (!FinalizeInstruction.IsEmpty())
    {
        Result.Preparations.Add(Variable + TEXT(".") + FinalizeInstruction + TEXT(";"));
    }
    if (!Request.DestinationExpression.IsEmpty())
    {
        FString Owner = TEXT("nullptr");
        if (!Request.OwnerExpression.IsEmpty())
        {
            Owner = Request.OwnerExpression;
        }
        Result.InitializationInstructions.Add(TEXT("{\nUScriptStruct* ") + Structure +
            TEXT(" = FindObjectChecked<UScriptStruct>(nullptr, ") + Quote(Request.Structure->GetPathName()) + TEXT(");"));
        Result.InitializationInstructions.Add(Structure + TEXT("->ImportText(") + Quote(Serialized) + TEXT(", &(") +
            Request.DestinationExpression + TEXT("), ") + Owner + TEXT(", PPF_None, nullptr, ") + Structure + TEXT("->GetName());"));
        if (!FinalizeInstruction.IsEmpty())
        {
            Result.InitializationInstructions.Add(Request.DestinationExpression + TEXT(".") + FinalizeInstruction + TEXT(";"));
        }
        Result.InitializationInstructions.Add(TEXT("}"));
    }
    return true;
}
