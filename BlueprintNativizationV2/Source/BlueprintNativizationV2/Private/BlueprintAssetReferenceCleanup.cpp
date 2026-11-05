/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright 2026 AO All Rights Reserved.
 */

#include "BlueprintAssetReferenceCleanup.h"

#include "EdGraph/EdGraphNode.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "KismetCompilerMisc.h"
#include "Misc/PackageName.h"
#include "Misc/OutputDeviceNull.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/Package.h"
#include "UObject/PropertyOptional.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StructOnScope.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

FBlueprintAssetReferenceCleanup::FBlueprintAssetReferenceCleanup(const TSet<FName>& InMissingPackages, TArray<FString>& InMessages)
    : MissingPackages(InMissingPackages), Messages(InMessages)
{
}

bool FBlueprintAssetReferenceCleanup::GetReplacement(const FSoftObjectPath& Original, FSoftObjectPath& Replacement) const
{
    if (Original.IsNull() || !MissingPackages.Contains(FName(*Original.GetLongPackageName())) ||
        FPackageName::DoesPackageExist(Original.GetLongPackageName()) || Original.ResolveObject())
    {
        return false;
    }
    Replacement = Original;
    if (Replacement.FixupCoreRedirects() &&
        (Replacement.ResolveObject() || FPackageName::DoesPackageExist(Replacement.GetLongPackageName())))
    {
        return false;
    }
    Replacement.Reset();
    return true;
}

bool FBlueprintAssetReferenceCleanup::VisitStructure(UStruct* Structure, void* Address, const FString& Location, bool bApply)
{
    const FString StructurePath = Structure->GetPathName();
    if (StructurePath == TEXT("/Script/CoreUObject.SoftObjectPath") || StructurePath == TEXT("/Script/CoreUObject.SoftClassPath"))
    {
        FSoftObjectPath& Original = *static_cast<FSoftObjectPath*>(Address);
        FSoftObjectPath Replacement;
        if (!GetReplacement(Original, Replacement))
        {
            return false;
        }
        if (bApply)
        {
            Messages.Add(FString::Format(TEXT("Cleaned asset path at {0}: {1} -> {2}"), {Location, Original.ToString(), Replacement.ToString()}));
            Original = Replacement;
        }
        return true;
    }
    if (Structure == FInstancedStruct::StaticStruct())
    {
        FInstancedStruct& Instance = *static_cast<FInstancedStruct*>(Address);
        if (!Instance.IsValid())
        {
            return false;
        }
        return VisitStructure(const_cast<UScriptStruct*>(Instance.GetScriptStruct()), Instance.GetMutableMemory(), Location, bApply);
    }
    bool bChanged = false;
    for (TFieldIterator<FProperty> PropertyIterator(Structure); PropertyIterator; ++PropertyIterator)
    {
        FProperty* Property = *PropertyIterator;
        if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_SkipSerialization))
        {
            continue;
        }
        for (int32 ElementIndex = 0; ElementIndex < Property->ArrayDim; ++ElementIndex)
        {
            const FString PropertyLocation = FString::Format(TEXT("{0}.{1}[{2}]"), {Location, Property->GetName(), ElementIndex});
            bChanged |= VisitValue(Property, Property->ContainerPtrToValuePtr<void>(Address, ElementIndex), PropertyLocation, bApply);
        }
    }
    return bChanged;
}

bool FBlueprintAssetReferenceCleanup::VisitValue(FProperty* Property, void* Address, const FString& Location, bool bApply)
{
    if (FOptionalProperty* OptionalProperty = CastField<FOptionalProperty>(Property))
    {
        if (!OptionalProperty->IsSet(Address))
        {
            return false;
        }
        return VisitValue(OptionalProperty->GetValueProperty(), Address, Location + TEXT(".Value"), bApply);
    }
    if (FSoftObjectProperty* SoftProperty = CastField<FSoftObjectProperty>(Property))
    {
        const FSoftObjectPath Original = SoftProperty->GetPropertyValue(Address).ToSoftObjectPath();
        FSoftObjectPath Replacement;
        if (!GetReplacement(Original, Replacement))
        {
            return false;
        }
        if (bApply)
        {
            SoftProperty->SetPropertyValue(Address, FSoftObjectPtr(Replacement));
            Messages.Add(FString::Format(TEXT("Cleaned asset reference at {0}: {1} -> {2}"), {Location, Original.ToString(), Replacement.ToString()}));
        }
        return true;
    }
    if (FStructProperty* StructureProperty = CastField<FStructProperty>(Property))
    {
        UScriptStruct* Structure = StructureProperty->Struct;
        return VisitStructure(Structure, Address, Location, bApply);
    }
    if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
    {
        FScriptArrayHelper Values(ArrayProperty, Address);
        bool bChanged = false;
        for (int32 ElementIndex = 0; ElementIndex < Values.Num(); ++ElementIndex)
        {
            bChanged |= VisitValue(ArrayProperty->Inner, Values.GetRawPtr(ElementIndex),
                FString::Format(TEXT("{0}[{1}]"), {Location, ElementIndex}), bApply);
        }
        return bChanged;
    }
    if (FSetProperty* SetProperty = CastField<FSetProperty>(Property))
    {
        FScriptSetHelper Values(SetProperty, Address);
        bool bChanged = false;
        for (int32 ElementIndex = Values.GetMaxIndex() - 1; ElementIndex >= 0; --ElementIndex)
        {
            if (Values.IsValidIndex(ElementIndex) && VisitValue(SetProperty->ElementProp, Values.GetElementPtr(ElementIndex), Location, false))
            {
                bChanged = true;
                if (bApply)
                {
                    Values.RemoveAt(ElementIndex);
                    Messages.Add(FString::Format(TEXT("Removed set element containing a missing asset at {0}[{1}]."), {Location, ElementIndex}));
                }
            }
        }
        if (bChanged && bApply)
        {
            Values.Rehash();
        }
        return bChanged;
    }
    if (FMapProperty* MapProperty = CastField<FMapProperty>(Property))
    {
        FScriptMapHelper Values(MapProperty, Address);
        bool bChanged = false;
        for (int32 ElementIndex = Values.GetMaxIndex() - 1; ElementIndex >= 0; --ElementIndex)
        {
            if (!Values.IsValidIndex(ElementIndex))
            {
                continue;
            }
            const FString ElementLocation = FString::Format(TEXT("{0}[{1}]"), {Location, ElementIndex});
            if (VisitValue(MapProperty->KeyProp, Values.GetKeyPtr(ElementIndex), ElementLocation + TEXT(".Key"), false))
            {
                bChanged = true;
                if (bApply)
                {
                    Values.RemoveAt(ElementIndex);
                    Messages.Add(TEXT("Removed map entry containing a missing asset in its key at ") + ElementLocation);
                }
            }
            else
            {
                bChanged |= VisitValue(MapProperty->ValueProp, Values.GetValuePtr(ElementIndex), ElementLocation + TEXT(".Value"), bApply);
            }
        }
        if (bChanged && bApply)
        {
            Values.Rehash();
        }
        return bChanged;
    }
    // Hard references that failed loading are already null. Their original identity is in the linker import map.
    return false;
}

bool FBlueprintAssetReferenceCleanup::VisitDefault(const FEdGraphPinType& PinType, FString& DefaultValue, UObject* Owner, const FString& Location)
{
    if (DefaultValue.IsEmpty())
    {
        return false;
    }
    if (!PinType.IsContainer() && (PinType.PinCategory == UEdGraphSchema_K2::PC_Object || PinType.PinCategory == UEdGraphSchema_K2::PC_Class ||
        PinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject || PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass))
    {
        const FString ObjectPath = FPackageName::ExportTextPathToObjectPath(DefaultValue);
        if (!ObjectPath.StartsWith(TEXT("/")))
        {
            return false;
        }
        FSoftObjectPath Replacement;
        if (!GetReplacement(FSoftObjectPath(ObjectPath), Replacement))
        {
            return false;
        }
        Messages.Add(FString::Format(TEXT("Cleaned graph default at {0}: {1} -> {2}"), {Location, DefaultValue, Replacement.ToString()}));
        DefaultValue = Replacement.ToString();
        return true;
    }
    if (PinType.IsContainer())
    {
        TStrongObjectPtr<UScriptStruct> Scope(NewObject<UScriptStruct>(GetTransientPackage(), NAME_None, RF_Transient));
        FCompilerResultsLog Diagnostics;
        FProperty* Property = FKismetCompilerUtilities::CreatePropertyOnScope(Scope.Get(), TEXT("DefaultValue"), PinType,
            nullptr, CPF_None, GetDefault<UEdGraphSchema_K2>(), Diagnostics);
        if (!Property || Diagnostics.NumErrors > 0)
        {
            delete Property;
            Messages.Add(TEXT("Cannot inspect container graph default at ") + Location);
            return false;
        }
        Scope->AddCppProperty(Property);
        Scope->StaticLink(true);
        FStructOnScope Value(Scope.Get());
        void* Address = Property->ContainerPtrToValuePtr<void>(Value.GetStructMemory());
        FOutputDeviceNull Errors;
        const TCHAR* Remaining = Property->ImportText_Direct(*DefaultValue, Address, Owner, PPF_None, &Errors);
        if (Remaining && FString(Remaining).TrimStartAndEnd().IsEmpty() && VisitValue(Property, Address, Location, false))
        {
            VisitValue(Property, Address, Location, true);
            DefaultValue.Reset();
            Property->ExportTextItem_Direct(DefaultValue, Address, nullptr, Owner, PPF_None);
            return true;
        }
    }
    else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
    {
        UScriptStruct* Structure = Cast<UScriptStruct>(PinType.PinSubCategoryObject.Get());
        if (Structure)
        {
            FStructOnScope Value(Structure);
            FOutputDeviceNull Errors;
            const TCHAR* Remaining = Structure->ImportText(*DefaultValue, Value.GetStructMemory(), Owner, PPF_None, &Errors, Structure->GetName());
            if (Remaining && FString(Remaining).TrimStartAndEnd().IsEmpty() &&
                VisitStructure(Structure, Value.GetStructMemory(), Location, false))
            {
                VisitStructure(Structure, Value.GetStructMemory(), Location, true);
                DefaultValue.Reset();
                Structure->ExportText(DefaultValue, Value.GetStructMemory(), nullptr, Owner, PPF_None, nullptr);
                return true;
            }
        }
    }
    return false;
}

void FBlueprintAssetReferenceCleanup::Apply(UPackage* Package)
{
    TArray<TStrongObjectPtr<UObject>> Objects;
    ForEachObjectWithPackage(Package, [&Objects](UObject* Object)
    {
        Objects.Emplace(Object);
        return true;
    }, EGetObjectsFlags::IncludeNestedObjects, RF_Transient, EInternalObjectFlags::Garbage);
    TSet<UBlueprint*> ChangedBlueprints;
    for (const TStrongObjectPtr<UObject>& Object : Objects)
    {
        bool bChanged = false;
        if (VisitStructure(Object->GetClass(), Object.Get(), Object->GetPathName(), false))
        {
            Object->Modify();
            bChanged = VisitStructure(Object->GetClass(), Object.Get(), Object->GetPathName(), true);
        }
        if (UEdGraphNode* Node = Cast<UEdGraphNode>(Object.Get()))
        {
            for (UEdGraphPin* Pin : Node->Pins)
            {
                FString DefaultValue = Pin->DefaultValue;
                if (VisitDefault(Pin->PinType, DefaultValue, Node, Node->GetPathName() + TEXT(".") + Pin->PinName.ToString()))
                {
                    Node->Modify();
                    Pin->Modify();
                    Pin->DefaultValue = DefaultValue;
                    bChanged = true;
                }
                FString AutogeneratedDefault = Pin->AutogeneratedDefaultValue;
                if (VisitDefault(Pin->PinType, AutogeneratedDefault, Node, Node->GetPathName() + TEXT(".Autogenerated.") + Pin->PinName.ToString()))
                {
                    Node->Modify();
                    Pin->Modify();
                    Pin->AutogeneratedDefaultValue = AutogeneratedDefault;
                    bChanged = true;
                }
            }
        }
        if (UBlueprint* Blueprint = Cast<UBlueprint>(Object.Get()))
        {
            for (FBPVariableDescription& Variable : Blueprint->NewVariables)
            {
                FString DefaultValue = Variable.DefaultValue;
                if (VisitDefault(Variable.VarType, DefaultValue, Blueprint, Blueprint->GetPathName() + TEXT(".Variable.") + Variable.VarName.ToString()))
                {
                    Blueprint->Modify();
                    Variable.DefaultValue = DefaultValue;
                    bChanged = true;
                }
            }
            if (bChanged)
            {
                ChangedBlueprints.Add(Blueprint);
            }
        }
        if (bChanged)
        {
            if (UBlueprint* Blueprint = Object->GetTypedOuter<UBlueprint>())
            {
                ChangedBlueprints.Add(Blueprint);
            }
        }
    }
    for (UBlueprint* Blueprint : ChangedBlueprints)
    {
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    }
}
