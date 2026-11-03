/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#include "BlueprintNativizationLibrary.h"
#include "Engine/Blueprint.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/UnrealType.h"

static bool DoesValueContainAsset(FProperty* Property, const void* Address)
{
    if (FSoftObjectProperty* SoftObject = CastField<FSoftObjectProperty>(Property))
    {
        const FSoftObjectPath Path = SoftObject->GetPropertyValue(Address).ToSoftObjectPath();
        return !Path.IsNull() && Path.GetSubPathString().IsEmpty() && !Path.GetLongPackageName().StartsWith(TEXT("/Script/"));
    }
    if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
    {
        UObject* Object = ObjectProperty->GetObjectPropertyValue(Address);
        if (UClass* Class = Cast<UClass>(Object))
        {
            return Class->ClassGeneratedBy && Class->ClassGeneratedBy->IsAsset();
        }
        return Object && Object->IsAsset();
    }
    if (FStructProperty* Structure = CastField<FStructProperty>(Property))
    {
        UScriptStruct* Type = Structure->Struct;
        if (Type == FInstancedStruct::StaticStruct())
        {
            const FInstancedStruct& Instance = *static_cast<const FInstancedStruct*>(Address);
            if (!Instance.IsValid())
            {
                return false;
            }
            Type = const_cast<UScriptStruct*>(Instance.GetScriptStruct());
            Address = Instance.GetMemory();
        }
        for (TFieldIterator<FProperty> Member(Type); Member; ++Member)
        {
            for (int32 ArrayIndex = 0; ArrayIndex < Member->ArrayDim; ++ArrayIndex)
            {
                if (DoesValueContainAsset(*Member, Member->ContainerPtrToValuePtr<void>(Address, ArrayIndex)))
                {
                    return true;
                }
            }
        }
    }
    else if (FArrayProperty* Array = CastField<FArrayProperty>(Property))
    {
        FScriptArrayHelper Values(Array, Address);
        for (int32 Index = 0; Index < Values.Num(); ++Index)
        {
            if (DoesValueContainAsset(Array->Inner, Values.GetRawPtr(Index)))
            {
                return true;
            }
        }
    }
    else if (FSetProperty* Set = CastField<FSetProperty>(Property))
    {
        FScriptSetHelper Values(Set, Address);
        for (int32 Index = 0; Index < Values.GetMaxIndex(); ++Index)
        {
            if (Values.IsValidIndex(Index) && DoesValueContainAsset(Set->ElementProp, Values.GetElementPtr(Index)))
            {
                return true;
            }
        }
    }
    else if (FMapProperty* Map = CastField<FMapProperty>(Property))
    {
        FScriptMapHelper Values(Map, Address);
        for (int32 Index = 0; Index < Values.GetMaxIndex(); ++Index)
        {
            if (Values.IsValidIndex(Index) && (DoesValueContainAsset(Map->KeyProp, Values.GetKeyPtr(Index)) ||
                DoesValueContainAsset(Map->ValueProp, Values.GetValuePtr(Index))))
            {
                return true;
            }
        }
    }
    return false;
}

bool UBlueprintNativizationLibrary::DoesPropertyContainAssetReference(FProperty* Property, const void* Container)
{
    for (int32 ArrayIndex = 0; ArrayIndex < Property->ArrayDim; ++ArrayIndex)
    {
        if (DoesValueContainAsset(Property, Property->ContainerPtrToValuePtr<void>(Container, ArrayIndex)))
        {
            return true;
        }
    }
    return false;
}
