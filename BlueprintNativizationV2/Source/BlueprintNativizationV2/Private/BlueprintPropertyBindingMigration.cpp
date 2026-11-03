#include "BlueprintPropertyBindingMigration.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectHash.h"

static void VisitBindingStructure(UStruct* Structure, void* Data, TFunctionRef<void(UStruct*, void*)> Visitor);

static void VisitBindingProperty(FProperty* Property, void* Address, TFunctionRef<void(UStruct*, void*)> Visitor)
{
    if (FStructProperty* Structure = CastField<FStructProperty>(Property))
    {
        VisitBindingStructure(Structure->Struct, Address, Visitor);
    }
    else if (FArrayProperty* Array = CastField<FArrayProperty>(Property))
    {
        FScriptArrayHelper Elements(Array, Address);
        for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ++ElementIndex)
        {
            VisitBindingProperty(Array->Inner, Elements.GetRawPtr(ElementIndex), Visitor);
        }
    }
}

static void VisitBindingStructure(UStruct* Structure, void* Data, TFunctionRef<void(UStruct*, void*)> Visitor)
{
    Visitor(Structure, Data);
    for (TFieldIterator<FProperty> Property(Structure); Property; ++Property)
    {
        for (int32 ArrayIndex = 0; ArrayIndex < Property->ArrayDim; ++ArrayIndex)
        {
            VisitBindingProperty(*Property, Property->ContainerPtrToValuePtr<void>(Data, ArrayIndex), Visitor);
        }
    }
}

static TArray<UObject*> GetBindingObjects(UObject* Asset)
{
    TArray<UObject*> Objects;
    GetObjectsWithOuter(Asset, Objects, EGetObjectsFlags::IncludeNestedObjects);
    Objects.Insert(Asset, 0);
    return Objects;
}

void FBlueprintPropertyBindingMigration::Capture(UObject* Asset, const TMap<FString, FString>& MemberNames)
{
    const TArray<UObject*> Objects = GetBindingObjects(Asset);
    TMap<FGuid, UStruct*> BindingTypes;
    for (UObject* Object : Objects)
    {
        VisitBindingStructure(Object->GetClass(), Object, [&BindingTypes](UStruct* Structure, void* Data)
        {
            FStructProperty* Identifier = FindFProperty<FStructProperty>(Structure, TEXT("ID"));
            FObjectPropertyBase* Instance = FindFProperty<FObjectPropertyBase>(Structure, TEXT("InstanceObject"));
            if (Identifier && Identifier->Struct == TBaseStructure<FGuid>::Get() && Instance)
            {
                if (UObject* InstanceObject = Instance->GetObjectPropertyValue_InContainer(Data))
                {
                    BindingTypes.Add(*Identifier->ContainerPtrToValuePtr<FGuid>(Data), InstanceObject->GetClass());
                }
            }
        });
    }
    for (UObject* Object : Objects)
    {
        VisitBindingStructure(Object->GetClass(), Object, [this, Asset, &BindingTypes, &MemberNames](UStruct* Structure, void* Data)
        {
            if (Structure->GetFName() != TEXT("PropertyBindingPath"))
            {
                return;
            }
            FStructProperty* Identifier = FindFProperty<FStructProperty>(Structure, TEXT("StructID"));
            FArrayProperty* Segments = FindFProperty<FArrayProperty>(Structure, TEXT("Segments"));
            FBindingPath Path;
            Path.StructIdentifier = *Identifier->ContainerPtrToValuePtr<FGuid>(Data);
            UStruct* CurrentType = BindingTypes.FindRef(Path.StructIdentifier);
            if (!CurrentType)
            {
                return;
            }
            FScriptArrayHelper Elements(Segments, Segments->ContainerPtrToValuePtr<void>(Data));
            FStructProperty* Segment = CastFieldChecked<FStructProperty>(Segments->Inner);
            FNameProperty* Name = FindFProperty<FNameProperty>(Segment->Struct, TEXT("Name"));
            FObjectPropertyBase* InstanceType = FindFProperty<FObjectPropertyBase>(Segment->Struct, TEXT("InstanceStruct"));
            bool bChanged = false;
            for (int32 SegmentIndex = 0; SegmentIndex < Elements.Num(); ++SegmentIndex)
            {
                if (!CurrentType)
                {
                    return;
                }
                void* SegmentData = Elements.GetRawPtr(SegmentIndex);
                const FName OriginalName = Name->GetPropertyValue_InContainer(SegmentData);
                FName NativeName = OriginalName;
                FProperty* Property = FindFProperty<FProperty>(CurrentType, OriginalName);
                if (!Property)
                {
                    return;
                }
                if (const FString* MappedName = MemberNames.Find(Property->GetPathName()))
                {
                    NativeName = FName(**MappedName);
                }
                Path.OriginalNames.Add(OriginalName);
                Path.NativeNames.Add(NativeName);
                bChanged |= NativeName != OriginalName;
                if (FArrayProperty* Array = CastField<FArrayProperty>(Property))
                {
                    Property = Array->Inner;
                }
                CurrentType = Cast<UStruct>(InstanceType->GetObjectPropertyValue_InContainer(SegmentData));
                if (!CurrentType)
                {
                    if (FStructProperty* ChildStructure = CastField<FStructProperty>(Property))
                    {
                        CurrentType = ChildStructure->Struct;
                    }
                    else if (FObjectPropertyBase* ChildObject = CastField<FObjectPropertyBase>(Property))
                    {
                        CurrentType = ChildObject->PropertyClass;
                    }
                }
            }
            if (bChanged)
            {
                AssetPaths.FindOrAdd(Asset).Add(MoveTemp(Path));
            }
        });
    }
}

int32 FBlueprintPropertyBindingMigration::Apply(UObject* Asset) const
{
    const TArray<FBindingPath>* Paths = AssetPaths.Find(Asset);
    if (!Paths)
    {
        return 0;
    }
    int32 ChangedPaths = 0;
    for (UObject* Object : GetBindingObjects(Asset))
    {
        VisitBindingStructure(Object->GetClass(), Object, [Paths, &ChangedPaths](UStruct* Structure, void* Data)
        {
            if (Structure->GetFName() != TEXT("PropertyBindingPath"))
            {
                return;
            }
            FStructProperty* Identifier = FindFProperty<FStructProperty>(Structure, TEXT("StructID"));
            FArrayProperty* Segments = FindFProperty<FArrayProperty>(Structure, TEXT("Segments"));
            const FGuid StructIdentifier = *Identifier->ContainerPtrToValuePtr<FGuid>(Data);
            FScriptArrayHelper Elements(Segments, Segments->ContainerPtrToValuePtr<void>(Data));
            FNameProperty* Name = FindFProperty<FNameProperty>(CastFieldChecked<FStructProperty>(Segments->Inner)->Struct, TEXT("Name"));
            for (const FBindingPath& Path : *Paths)
            {
                if (StructIdentifier != Path.StructIdentifier || Elements.Num() != Path.OriginalNames.Num())
                {
                    continue;
                }
                bool bMatches = true;
                for (int32 SegmentIndex = 0; SegmentIndex < Elements.Num(); ++SegmentIndex)
                {
                    const FName CurrentName = Name->GetPropertyValue_InContainer(Elements.GetRawPtr(SegmentIndex));
                    bMatches &= CurrentName == Path.OriginalNames[SegmentIndex] || CurrentName == Path.NativeNames[SegmentIndex];
                }
                if (!bMatches)
                {
                    continue;
                }
                bool bChanged = false;
                for (int32 SegmentIndex = 0; SegmentIndex < Elements.Num(); ++SegmentIndex)
                {
                    void* SegmentData = Elements.GetRawPtr(SegmentIndex);
                    bChanged |= Name->GetPropertyValue_InContainer(SegmentData) != Path.NativeNames[SegmentIndex];
                    Name->SetPropertyValue_InContainer(SegmentData, Path.NativeNames[SegmentIndex]);
                }
                if (bChanged)
                {
                    ++ChangedPaths;
                }
                break;
            }
        });
    }
    return ChangedPaths;
}
