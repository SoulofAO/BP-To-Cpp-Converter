#include "Dom/JsonObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintSubsystemReinstanceScope.h"
#include "RetainedBlueprintGeneration.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationUniqueNameSubsystem.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "Engine/Blueprint.h"
#include "Components/Widget.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/TimelineTemplate.h"
#include "Components/TimelineComponent.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/InheritableComponentHandler.h"
#include "ComponentInstanceDataCache.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "Components/BillboardComponent.h"
#include "Engine/Texture2D.h"
#include "Components/StaticMeshComponent.h"
#include "Components/ChildActorComponent.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CallArrayFunction.h"
#include "K2Node_CallParentFunction.h"
#include "K2Node_BaseMCDelegate.h"
#include "K2Node_CreateDelegate.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_ClassDynamicCast.h"
#include "K2Node_ConstructObjectFromClass.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_GetClassDefaults.h"
#include "StructPrivateMemberAccess.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionTerminator.h"
#include "K2Node_Variable.h"
#include "K2Node_StructOperation.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_Select.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/StructureEditorUtils.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "KismetCompiler.h"
#include "BlueprintCompilationManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "StructUtils/InstancedStruct.h"
#include "StructUtils/PropertyBag.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/StructOnScope.h"
#include "UObject/Script.h"
#include "UObject/UnrealType.h"
#include "UObject/EnumProperty.h"
#include "UObject/UObjectHash.h"
#include "K2Node_EnhancedInputAction.h"
#include "InputAction.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Base64.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Serialization/CustomVersion.h"
#include "NiagaraComponent.h"
#include "Components/SceneCaptureComponent.h"
#include "Animation/AnimNodeBase.h"

static TAutoConsoleVariable<int32> MigrationValueDiagnostics(
    TEXT("BlueprintNativization.Debug.MigrationValues"), 0,
    TEXT("Log object property identities before migration replay to diagnose reflection lifetime failures."));

struct FClassDefaultsPinOptionsAccess
{
    using Member = TArray<FOptionalPinFromProperty> UK2Node_GetClassDefaults::*;
    friend Member GetStructPrivateMember(FClassDefaultsPinOptionsAccess);
};
template struct TStructPrivateMemberAccess<FClassDefaultsPinOptionsAccess, &UK2Node_GetClassDefaults::ShowPinForProperties>;

static void FlushMigrationComponentUpdates()
{
    TArray<UWorld*> Worlds;
    ForEachObjectOfClass(UWorld::StaticClass(), [&Worlds](UObject* Object)
    {
        UWorld* World = CastChecked<UWorld>(Object);
        if (World->HasEndOfFrameUpdates())
        {
            Worlds.Add(World);
        }
    });
    for (UWorld* World : Worlds)
    {
        World->FinishAsyncSendAllEndOfFrameUpdates();
        World->SendAllEndOfFrameUpdates();
    }
}

TMap<FString, FString> UNativizationV2Subsystem::CaptureMigrationMemberNames() const
{
    TMap<FString, FString> Names;
    UBlueprintNativizationUniqueNameSubsystem* NameSubsystem = GEngine->GetEngineSubsystem<UBlueprintNativizationUniqueNameSubsystem>();
    for (const FRegisterVariable& Registered : NameSubsystem->UniqueVarNamesArray)
    {
        for (const FPropertyNamePair& Member : Registered.PropertyNamePairs)
        {
            if (Member.Property.Get())
            {
                Names.Add(Member.Property->GetPathName(), Member.Name.ToString());
            }
        }
    }
    for (const TPair<UField*, FName>& Generated : UBlueprintNativizationLibrary::GetAllRegisterUniqueFieldsInNameSubsistem())
    {
        if (UStruct* Structure = Cast<UStruct>(Generated.Key))
        {
            for (TFieldIterator<FProperty> Property(Structure, EFieldIteratorFlags::ExcludeSuper); Property; ++Property)
            {
                Names.Add(Property->GetPathName(), UBlueprintNativizationLibrary::GetUniquePropertyName(*Property));
                Names.Add(Structure->GetPathName() + TEXT(":Property:") + Property->GetName(),
                    UBlueprintNativizationLibrary::GetUniquePropertyName(*Property));
            }
        }
        if (UClass* Class = Cast<UClass>(Generated.Key))
        {
            for (TFieldIterator<UFunction> Function(Class, EFieldIteratorFlags::ExcludeSuper); Function; ++Function)
            {
                Names.Add(Function->GetPathName(), UBlueprintNativizationLibrary::GetUniqueFunctionName(*Function, TEXT("")));
                Names.Add(Class->GetPathName() + TEXT(":Function:") + Function->GetName(),
                    UBlueprintNativizationLibrary::GetUniqueFunctionName(*Function, TEXT("")));
                for (TFieldIterator<FProperty> Parameter(*Function); Parameter; ++Parameter)
                {
                    if (Parameter->HasAnyPropertyFlags(CPF_Parm))
                    {
                        Names.Add(Parameter->GetPathName(), UBlueprintNativizationLibrary::GetUniquePropertyName(*Parameter));
                    }
                }
            }
        }
    }
    for (const FRegisterVariable& Registered : NameSubsystem->UniqueVarNamesArray)
    {
        for (const FPropertyNamePair& Member : Registered.PropertyNamePairs)
        {
            UK2Node_EnhancedInputAction* Input = Cast<UK2Node_EnhancedInputAction>(Member.LambdaNode);
            if (Input && Input->InputAction && Member.LambdaMainName == Input->InputAction->GetName())
            {
                Names.Add(Registered.Class->GetPathName() + TEXT(":InputAction:") + Input->NodeGuid.ToString(), Member.Name.ToString());
            }
        }
    }
    Names.Append(FRetainedBlueprintGeneration::GetMigrationClassMembers());
    return Names;
}

enum class EMigrationValueKind : uint8
{
    Text,
    Object,
    SoftObject,
    Number,
    Structure,
    InstancedStructure,
    Array,
    Set,
    Map,
    ConstructorComponent,
    NativeStructure
};

static bool IsMigrationRecoveryRoot(const UActorComponent* Component)
{
    if (!Component || Component->GetClass() != UBillboardComponent::StaticClass() ||
        Component->CreationMethod != EComponentCreationMethod::SimpleConstructionScript ||
        Component->GetArchetype() != UBillboardComponent::StaticClass()->GetDefaultObject())
    {
        return false;
    }
    const AActor* Actor = Component->GetOwner();
    if (!Actor || Actor->IsTemplate() || Actor->GetRootComponent() != Component)
    {
        return false;
    }
    const UBillboardComponent* Billboard = CastChecked<UBillboardComponent>(Component);
    if (!Billboard->Sprite || Billboard->Sprite->GetPathName() != TEXT("/Engine/EditorResources/BadBlueprintSprite.BadBlueprintSprite"))
    {
        return false;
    }
    TArray<const UBlueprintGeneratedClass*> BlueprintHierarchy;
    return !UBlueprintGeneratedClass::GetGeneratedClassesHierarchy(Actor->GetClass(), BlueprintHierarchy);
}

struct FMigrationRecoveryRoot
{
    TStrongObjectPtr<AActor> Owner;
    FTransform Transform;
};

struct FMigrationValue
{
    EMigrationValueKind Kind = EMigrationValueKind::Text;
    FString Name;
    FString Text;
    FString SourcePath;
    FString ObjectClassPath;
    TStrongObjectPtr<UObject> Object;
    TStrongObjectPtr<UBlueprint> BlueprintClass;
    TStrongObjectPtr<UScriptStruct> Structure;
    TSharedPtr<FStructOnScope> NativeValues;
    TArray<TStrongObjectPtr<UObject>> NativeObjects;
    TArray<FMigrationValue> Children;
};

class FMigrationStructureObjectCollector : public FReferenceCollector
{
public:
    TFunction<void(UObject*&)> VisitObject;

    explicit FMigrationStructureObjectCollector(TFunction<void(UObject*&)> InVisitObject)
        : VisitObject(MoveTemp(InVisitObject))
    {
    }

    virtual bool IsIgnoringArchetypeRef() const override
    {
        return false;
    }

    virtual bool IsIgnoringTransient() const override
    {
        return false;
    }

    virtual void HandleObjectReference(UObject*& Object, const UObject* ReferencingObject, const FProperty* ReferencingProperty) override
    {
        VisitObject(Object);
    }
};

static TArray<uint8> SerializeMigrationStructure(UScriptStruct* Structure, const void* Memory)
{
    TArray<uint8> Bytes;
    FMemoryWriter Writer(Bytes, true);
    FObjectAndNameAsStringProxyArchive Archive(Writer, false);
    Archive.SetCustomVersions(FCurrentCustomVersions::GetAll());
    Structure->SerializeItem(Archive, const_cast<void*>(Memory), nullptr);
    return Bytes;
}

static bool DeserializeMigrationStructure(UScriptStruct* Structure, const TArray<uint8>& Bytes, void* Memory)
{
    FMemoryReader Reader(Bytes, true);
    FObjectAndNameAsStringProxyArchive Archive(Reader, true);
    Archive.SetCustomVersions(FCurrentCustomVersions::GetAll());
    Structure->SerializeItem(Archive, Memory, nullptr);
    return !Archive.IsError() && Reader.Tell() == Bytes.Num();
}

struct FMigrationObject
{
    TStrongObjectPtr<UObject> Source;
    TStrongObjectPtr<UClass> DestinationClass;
    TArray<FMigrationValue> Properties;
};

struct FMigrationWidget
{
    FString Path;
    TStrongObjectPtr<UWidget> Source;
    TArray<FMigrationValue> Properties;
};

struct FMigrationBlueprint
{
    TStrongObjectPtr<UBlueprint> Blueprint;
    TStrongObjectPtr<UClass> NativeClass;
    FString SourceClassPath;
    UObject* DefaultObject = nullptr;
    TMap<FName, UObject*> Components;
    TMap<FName, UObject*> InheritedComponents;
    TMap<FName, UObject*> InstanceTemplates;
    TSet<FName> RuntimeComponentMembers;
    TSet<FName> ConstructionTemplateMembers;
    TMap<FName, FName> ConstructionTemplateNames;
};

struct FMigrationTable
{
    TStrongObjectPtr<UDataTable> Table;
    TStrongObjectPtr<UScriptStruct> NativeStructure;
    TMap<FName, TArray<FMigrationValue>> Rows;
};

struct FMigrationChildActorOwner
{
    FString ActorPath;
    TStrongObjectPtr<AActor> Actor;
    TStrongObjectPtr<UChildActorComponent> Component;
    FName ComponentMember;
    FName ComponentName;
};

struct FMigrationInstance
{
    FString Path;
    TStrongObjectPtr<UObject> Source;
    TStrongObjectPtr<UObject> Owner;
    FString OwnerPath;
    FName ComponentMember;
    FName ComponentName;
    FName ComponentTemplateName;
    FName ComponentTemplateMember;
    int32 ComponentSerializationIndex = INDEX_NONE;
    int32 ComponentTemplateOrdinal = 0;
    bool bConstructionComponent = false;
    bool bChildActorComponentOwner = false;
    TStructOnScope<FActorComponentInstanceData> ComponentData;
    TArray<FMigrationChildActorOwner> ChildActorOwners;
    TArray<TStrongObjectPtr<UActorComponent>> ComponentOuters;
    TArray<FName> ComponentOuterNames;
    FName ComponentOuterMember;
};

class FMigrationReferenceArchive : public FArchiveReplaceObjectRef<UObject>
{
public:
    const TMap<FSoftObjectPath, FSoftObjectPath>& SoftMappings;
    const TSet<UClass*>& DefaultOnlySourceClasses;
    bool bKeepAssetReferencesInBlueprint;
    bool bSoftReferencesChanged = false;

    FMigrationReferenceArchive(UObject* Object, const TMap<UObject*, UObject*>& ObjectMappings,
        const TMap<FSoftObjectPath, FSoftObjectPath>& InSoftMappings, const TSet<UClass*>& InDefaultOnlySourceClasses,
        bool bInKeepAssetReferencesInBlueprint)
        : FArchiveReplaceObjectRef<UObject>(Object, ObjectMappings,
            EArchiveReplaceObjectFlags::IgnoreOuterRef | EArchiveReplaceObjectFlags::IgnoreArchetypeRef |
            EArchiveReplaceObjectFlags::DelayStart), SoftMappings(InSoftMappings), DefaultOnlySourceClasses(InDefaultOnlySourceClasses),
            bKeepAssetReferencesInBlueprint(bInKeepAssetReferencesInBlueprint)
    {
        SerializeSearchObject();
    }

    virtual bool ShouldSkipProperty(const FProperty* Property) const override
    {
        if (UBlueprint* Blueprint = Cast<UBlueprint>(SerializingObject))
        {
            if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UBlueprint, GeneratedClass) ||
                Property->GetFName() == GET_MEMBER_NAME_CHECKED(UBlueprint, SkeletonGeneratedClass))
            {
                return true;
            }
            if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UBlueprint, ParentClass) &&
                (bKeepAssetReferencesInBlueprint || DefaultOnlySourceClasses.Contains(Blueprint->ParentClass)))
            {
                return true;
            }
        }
        return FArchiveReplaceObjectRef<UObject>::ShouldSkipProperty(Property);
    }

    virtual FArchive& operator<<(UObject*& Value) override
    {
        if (const FProperty* Property = GetSerializedProperty())
        {
            if (ShouldSkipProperty(Property))
            {
                return *this;
            }
            if (bKeepAssetReferencesInBlueprint && Property->IsA<FClassProperty>() &&
                !SerializingObject->IsA<UBlueprint>() && !SerializingObject->IsA<UField>() &&
                !SerializingObject->IsA<UEdGraphNode>())
            {
                if (UClass* Class = Cast<UClass>(Value))
                {
                    if (UBlueprint* Blueprint = Cast<UBlueprint>(Class->ClassGeneratedBy))
                    {
                        if (Blueprint->GeneratedClass)
                        {
                            if (Value != Blueprint->GeneratedClass)
                            {
                                Value = Blueprint->GeneratedClass;
                                ++Count;
                            }
                            return *this;
                        }
                    }
                }
            }
        }
        if (bKeepAssetReferencesInBlueprint)
        {
            if (UBlueprint* Blueprint = Cast<UBlueprint>(SerializingObject))
            {
                if (UClass* Class = Cast<UClass>(Value))
                {
                    if (Class == Blueprint->ParentClass)
                    {
                        if (UBlueprint* ParentBlueprint = Cast<UBlueprint>(Class->ClassGeneratedBy))
                        {
                            Value = ParentBlueprint->GeneratedClass.Get();
                            return *this;
                        }
                    }
                }
            }
        }
        return FArchiveReplaceObjectRef<UObject>::operator<<(Value);
    }

    virtual FArchive& operator<<(FSoftObjectPath& Value) override
    {
        if (bKeepAssetReferencesInBlueprint)
        {
            if (const FProperty* Property = GetSerializedProperty())
            {
                if (Property->IsA<FSoftClassProperty>())
                {
                    return *this;
                }
            }
        }
        if (const FSoftObjectPath* Replacement = SoftMappings.Find(Value))
        {
            Value = *Replacement;
            bSoftReferencesChanged = true;
            return *this;
        }
        return FArchiveReplaceObjectRef<UObject>::operator<<(Value);
    }
};

class FMigrationValues
{
public:
    bool bKeepAssetReferencesInBlueprint;
    const TMap<UField*, UField*>& TypeMappings;
    const TMap<UClass*, UClass*>& DefaultOnlyClassReplacements;
    const TMap<FString, FString>& MemberNames;
    TMap<UObject*, FMigrationObject> Objects;
    TMap<UObject*, FMigrationRecoveryRoot> RecoveryRoots;
    TMap<UObject*, UObject*> ObjectMappings;
    TMap<UObject*, TStrongObjectPtr<UObject>> MappingSourceObjects;
    TMap<UObject*, TStrongObjectPtr<UObject>> DestinationObjects;
    TArray<TStrongObjectPtr<UObject>> ConfigurationOwners;
    TMap<FSoftObjectPath, FSoftObjectPath> SoftMappings;
    TMap<const UObject*, FString> CapturedObjectPaths;
    TMap<UObject*, FString> SceneObjectPaths;
    TMap<UObject*, TStrongObjectPtr<UClass>> SceneObjectClasses;
    TFunction<UObject*(UObject*, const FString&)> SceneObjectResolver;
    bool bCaptureObjectPaths = true;
    FString Error;

    FMigrationValues(const TMap<UField*, UField*>& InTypeMappings, const TMap<FString, FString>& InMemberNames,
        const TMap<UClass*, UClass*>& InDefaultOnlyClassReplacements, bool bInKeepAssetReferencesInBlueprint)
        : bKeepAssetReferencesInBlueprint(bInKeepAssetReferencesInBlueprint), TypeMappings(InTypeMappings),
        DefaultOnlyClassReplacements(InDefaultOnlyClassReplacements), MemberNames(InMemberNames)
    {
        for (const TPair<UField*, UField*>& Mapping : TypeMappings)
        {
            CaptureObjectPath(Mapping.Key);
            CaptureObjectPath(Mapping.Value);
            AddObjectMapping(Mapping.Key, Mapping.Value);
            SoftMappings.Add(FSoftObjectPath(Mapping.Key), FSoftObjectPath(Mapping.Value));
        }
        for (const TPair<UClass*, UClass*>& Replacement : DefaultOnlyClassReplacements)
        {
            CaptureObjectPath(Replacement.Key);
            CaptureObjectPath(Replacement.Value);
            AddObjectMapping(Replacement.Key, Replacement.Value);
            SoftMappings.Add(FSoftObjectPath(Replacement.Key), FSoftObjectPath(Replacement.Value));
        }
    }

    void CaptureObjectPath(const UObject* Object)
    {
        if (bCaptureObjectPaths && Object && !CapturedObjectPaths.Contains(Object))
        {
            CapturedObjectPaths.Add(Object, Object->GetPathName());
        }
    }

    void CaptureSceneObject(UObject* Object)
    {
        if (!bCaptureObjectPaths || !Object || SceneObjectPaths.Contains(Object))
        {
            return;
        }
        AActor* Actor = Cast<AActor>(Object);
        if (UActorComponent* Component = Cast<UActorComponent>(Object))
        {
            Actor = Component->GetOwner();
        }
        if (Actor && Actor->GetLevel())
        {
            SceneObjectPaths.Add(Object, Object->GetPathName());
            UClass* DestinationClass = Object->GetClass();
            if (UField* const* Replacement = TypeMappings.Find(DestinationClass))
            {
                DestinationClass = CastChecked<UClass>(*Replacement);
            }
            else if (UClass* const* DefaultOnlyReplacement = DefaultOnlyClassReplacements.Find(DestinationClass))
            {
                DestinationClass = *DefaultOnlyReplacement;
            }
            CaptureObjectPath(DestinationClass);
            SceneObjectClasses.Add(Object, TStrongObjectPtr<UClass>(DestinationClass));
        }
    }

    FString GetDiagnosticObjectPath(const UObject* Object) const
    {
        if (!Object)
        {
            return TEXT("None");
        }
        if (const FString* Path = CapturedObjectPaths.Find(Object))
        {
            return *Path;
        }
        return FString::Format(TEXT("Uncaptured object address {0}"), {LexToString(reinterpret_cast<UPTRINT>(Object))});
    }

    void MapObject(UObject* Source, UObject* Destination)
    {
        UObject* PreviousDestination = ObjectMappings.FindRef(Source);
        AddObjectMapping(Source, Destination);
        if (PreviousDestination && PreviousDestination != Source && PreviousDestination != Destination)
        {
            AddObjectMapping(PreviousDestination, Destination);
        }
    }

    void AddObjectMapping(UObject* Source, UObject* Destination)
    {
        MappingSourceObjects.FindOrAdd(Source).Reset(Source);
        if (Destination)
        {
            DestinationObjects.FindOrAdd(Destination).Reset(Destination);
        }
        ObjectMappings.Add(Source, Destination);
    }

    void MapDefaultSubobjects(UObject* Source, UObject* Destination)
    {
        TArray<UObject*> SourceSubobjects;
        Source->GetDefaultSubobjects(SourceSubobjects);
        for (UObject* SourceSubobject : SourceSubobjects)
        {
            if (SourceSubobject->GetOuter() != Source)
            {
                continue;
            }
            UObject* DestinationSubobject = Destination->GetDefaultSubobjectByName(SourceSubobject->GetFName());
            if (DestinationSubobject && DestinationSubobject->GetOuter() == Destination &&
                DestinationSubobject->IsA(SourceSubobject->GetClass()))
            {
                MapObject(SourceSubobject, DestinationSubobject);
                MapDefaultSubobjects(SourceSubobject, DestinationSubobject);
            }
        }
    }

    bool NormalizeObjectMappings()
    {
        TMap<UObject*, UObject*> NormalizedMappings;
        for (const TPair<UObject*, UObject*>& Mapping : ObjectMappings)
        {
            if (Mapping.Key == Mapping.Value)
            {
                continue;
            }
            TSet<UObject*> VisitedObjects;
            VisitedObjects.Add(Mapping.Key);
            UObject* Destination = Mapping.Value;
            while (Destination)
            {
                if (VisitedObjects.Contains(Destination))
                {
                    Error = TEXT("Migration object mapping contains a cycle starting at ") + GetDiagnosticObjectPath(Mapping.Key);
                    return false;
                }
                VisitedObjects.Add(Destination);
                UObject* const* NextDestination = ObjectMappings.Find(Destination);
                if (!NextDestination || *NextDestination == Destination)
                {
                    break;
                }
                Destination = *NextDestination;
            }
            if (Mapping.Key->IsA<UActorComponent>() && Destination && !Destination->IsA<UActorComponent>())
            {
                Error = FString::Format(TEXT("Migration component mapping has an incompatible destination: '{0}' -> '{1}' of class '{2}'."),
                    {GetDiagnosticObjectPath(Mapping.Key), Destination->GetPathName(), Destination->GetClass()->GetPathName()});
                return false;
            }
            NormalizedMappings.Add(Mapping.Key, Destination);
        }
        ObjectMappings = MoveTemp(NormalizedMappings);
        return true;
    }

    static bool IsProjectAsset(UObject* Object)
    {
        FString Filename;
        if (!Object || !FPackageName::TryConvertLongPackageNameToFilename(Object->GetOutermost()->GetName(), Filename))
        {
            return false;
        }
        return FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Filename),
            FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
    }

    UClass* FindSourceClass(UClass* Class) const
    {
        if (TypeMappings.Contains(Class))
        {
            return Class;
        }
        if (UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(Class))
        {
            if (TypeMappings.Contains(Blueprint->GeneratedClass.Get()))
            {
                return Blueprint->GeneratedClass.Get();
            }
        }
        return nullptr;
    }

    FString GetMemberName(FProperty* Property) const
    {
        if (TypeMappings.Contains(Property->GetOwnerStruct()) || TypeMappings.Contains(Property->GetOwnerClass()))
        {
            if (const FString* Name = MemberNames.Find(Property->GetPathName()))
            {
                return *Name;
            }
        }
        if (UClass* OwnerClass = Cast<UClass>(Property->GetOwnerStruct()))
        {
            if (UBlueprint* OwnerBlueprint = UBlueprint::GetBlueprintFromClass(OwnerClass))
            {
                for (const TPair<UField*, UField*>& Mapping : TypeMappings)
                {
                    UClass* SourceClass = Cast<UClass>(Mapping.Key);
                    UClass* NativeClass = Cast<UClass>(Mapping.Value);
                    if (!SourceClass || !NativeClass || UBlueprint::GetBlueprintFromClass(SourceClass) != OwnerBlueprint)
                    {
                        continue;
                    }
                    const FString* SourceClassPath = CapturedObjectPaths.Find(SourceClass);
                    if (!SourceClassPath)
                    {
                        continue;
                    }
                    if (const FString* Name = MemberNames.Find(*SourceClassPath + TEXT(":") + Property->GetName()))
                    {
                        if (FindFProperty<FProperty>(NativeClass, FName(**Name)))
                        {
                            return *Name;
                        }
                    }
                }
            }
        }
        return Property->GetName();
    }

    UFunction* FindSourceFunction(UFunction* Function) const
    {
        for (UFunction* Signature = Function; Signature; Signature = Signature->GetSuperFunction())
        {
            if (UClass* SourceClass = FindSourceClass(Signature->GetOwnerClass()))
            {
                return SourceClass->FindFunctionByName(Signature->GetFName());
            }
        }
        return nullptr;
    }

    FString RemapEnumValue(UEnum* Enumeration, const FString& Value) const
    {
        if (!Enumeration || Value.IsEmpty())
        {
            return Value;
        }
        UEnum* SourceEnumeration = Enumeration;
        UEnum* DestinationEnumeration = Cast<UEnum>(TypeMappings.FindRef(Enumeration));
        if (!DestinationEnumeration)
        {
            for (const TPair<UField*, UField*>& Mapping : TypeMappings)
            {
                if (Mapping.Value == Enumeration)
                {
                    SourceEnumeration = Cast<UEnum>(Mapping.Key);
                    DestinationEnumeration = Enumeration;
                    if (Enumeration->GetValueByNameString(Value) != INDEX_NONE)
                    {
                        return Value;
                    }
                    break;
                }
            }
        }
        if (SourceEnumeration && DestinationEnumeration)
        {
            const int64 NumericValue = SourceEnumeration->GetValueByNameString(Value);
            if (NumericValue != INDEX_NONE)
            {
                return DestinationEnumeration->GetNameStringByValue(NumericValue);
            }
        }
        return Value;
    }

    bool RemapPin(UEdGraphPin* Pin, const FString& DestinationName)
    {
        const FString OriginalName = Pin->PinName.ToString();
        UScriptStruct* OriginalStructure = Cast<UScriptStruct>(Pin->PinType.PinSubCategoryObject.Get());
        UEnum* OriginalEnumeration = Cast<UEnum>(Pin->PinType.PinSubCategoryObject.Get());
        Pin->DefaultValue = RemapEnumValue(OriginalEnumeration, Pin->DefaultValue);
        Pin->AutogeneratedDefaultValue = RemapEnumValue(OriginalEnumeration, Pin->AutogeneratedDefaultValue);
        Pin->PinName = FName(*DestinationName);
        if (UClass* OriginalClass = Cast<UClass>(Pin->PinType.PinSubCategoryObject.Get()))
        {
            if (UClass* const* Replacement = DefaultOnlyClassReplacements.Find(OriginalClass))
            {
                Pin->PinType.PinSubCategoryObject = *Replacement;
            }
        }
        if (UClass* OriginalClass = Cast<UClass>(Pin->PinType.PinValueType.TerminalSubCategoryObject.Get()))
        {
            if (UClass* const* Replacement = DefaultOnlyClassReplacements.Find(OriginalClass))
            {
                Pin->PinType.PinValueType.TerminalSubCategoryObject = *Replacement;
            }
        }
        if (UClass* OriginalClass = Cast<UClass>(Pin->DefaultObject))
        {
            if (UClass* const* Replacement = DefaultOnlyClassReplacements.Find(OriginalClass))
            {
                Pin->DefaultObject = *Replacement;
            }
        }
        if (OriginalStructure && TypeMappings.Contains(OriginalStructure))
        {
            UScriptStruct* NativeStructure = ResolveStructure(OriginalStructure);
            if (Pin->SubPins.IsEmpty() && Pin->PinType.ContainerType == EPinContainerType::None && !Pin->DefaultValue.IsEmpty())
            {
                FStructOnScope OriginalMemory(OriginalStructure);
                if (!OriginalStructure->ImportText(*Pin->DefaultValue, OriginalMemory.GetStructMemory(), Pin->GetOwningNode(),
                    PPF_None, GWarn, OriginalStructure->GetName()))
                {
                    Error = TEXT("Migration cannot import struct pin default: ") + Pin->GetOwningNode()->GetPathName() + TEXT(".") + OriginalName;
                    return false;
                }
                const TArray<FMigrationValue> Properties = CaptureStructure(OriginalStructure, OriginalMemory.GetStructMemory());
                FStructOnScope NativeMemory(NativeStructure);
                if (!ValidateStructure(NativeStructure, Properties) ||
                    !ApplyStructure(NativeStructure, NativeMemory.GetStructMemory(), Properties, Pin->GetOwningNode()))
                {
                    return false;
                }
                const bool bDefaultMatchesAutomatic = Pin->DefaultValue == Pin->AutogeneratedDefaultValue;
                Pin->DefaultValue.Reset();
                NativeStructure->ExportText(Pin->DefaultValue, NativeMemory.GetStructMemory(), nullptr,
                    Pin->GetOwningNode(), PPF_None, nullptr);
                if (bDefaultMatchesAutomatic)
                {
                    Pin->AutogeneratedDefaultValue = Pin->DefaultValue;
                }
            }
        }
        for (UEdGraphPin* Child : Pin->SubPins)
        {
            FString MemberName = Child->PinName.ToString().RightChop(OriginalName.Len() + 1);
            if (OriginalStructure)
            {
                if (FProperty* Property = FindFProperty<FProperty>(OriginalStructure, FName(*MemberName)))
                {
                    MemberName = GetMemberName(Property);
                }
            }
            if (!RemapPin(Child, DestinationName + TEXT("_") + MemberName))
            {
                return false;
            }
        }
        return true;
    }

    static bool IsSerializable(FProperty* Property)
    {
        if (Property->GetOwnerClass() == UNiagaraComponent::StaticClass() &&
            (Property->GetFName() == TEXT("TemplateParameterOverrides") || Property->GetFName() == TEXT("InstanceParameterOverrides")))
        {
            return true;
        }
        if (Property->GetOwnerClass() == UChildActorComponent::StaticClass() && Property->GetFName() == TEXT("ChildActorTemplate"))
        {
            return true;
        }
        if (Property->GetOwnerClass() == UActorComponent::StaticClass() &&
            Property->GetFName() == GET_MEMBER_NAME_CHECKED(UActorComponent, CreationMethod))
        {
            // The destination factory owns component lifetime policy, not the captured Blueprint defaults.
            return false;
        }
        return !Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_Deprecated | CPF_EditorOnly) &&
            !Property->HasMetaData(TEXT("BlueprintCompilerGeneratedDefaults")) &&
            !Property->IsA<FDelegateProperty>() && !Property->IsA<FMulticastDelegateProperty>();
    }

    TArray<FMigrationValue> CaptureStructure(UStruct* Structure, const void* Memory)
    {
        TArray<FMigrationValue> Values;
        for (TFieldIterator<FProperty> Property(Structure); Property; ++Property)
        {
            UClass* OwnerClass = Property->GetOwnerClass();
            if (OwnerClass && TypeMappings.Contains(OwnerClass))
            {
                UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(OwnerClass);
                if (Blueprint && UBlueprintNativizationLibrary::GetConversionPropertyFlagForProperty(*Property, Blueprint) == EConversionPropertyFlag::Ignore)
                {
                    continue;
                }
            }
            if (IsSerializable(*Property))
            {
                for (int32 ArrayIndex = 0; ArrayIndex < Property->ArrayDim; ++ArrayIndex)
                {
                    FMigrationValue Value = CaptureValue(*Property, Property->ContainerPtrToValuePtr<void>(Memory, ArrayIndex));
                    Value.Name = GetMemberName(*Property);
                    if (Value.Kind == EMigrationValueKind::Object && !Value.Object &&
                        Cast<UClass>(Structure) && static_cast<const UObject*>(Memory)->HasAnyFlags(RF_ClassDefaultObject))
                    {
                        UBlueprintGeneratedClass* BlueprintOwner = Cast<UBlueprintGeneratedClass>(OwnerClass);
                        UClass* NativeOwner = Cast<UClass>(TypeMappings.FindRef(OwnerClass));
                        if (BlueprintOwner && NativeOwner && NativeOwner->HasAnyClassFlags(CLASS_Native))
                        {
                            const bool bTimelineBinding = BlueprintOwner->Timelines.ContainsByPredicate([&Property](const UTimelineTemplate* Timeline)
                            {
                                return Timeline && Timeline->GetVariableName() == Property->GetFName();
                            });
                            if (bTimelineBinding)
                            {
                                UObject* NativeDefaults = NativeOwner->GetDefaultObject();
                                FObjectPropertyBase* NativeProperty = FindFProperty<FObjectPropertyBase>(NativeOwner, *Value.Name);
                                UTimelineComponent* NativeComponent = nullptr;
                                if (NativeProperty)
                                {
                                    NativeComponent = Cast<UTimelineComponent>(NativeProperty->GetObjectPropertyValue_InContainer(NativeDefaults));
                                }
                                if (!NativeComponent || NativeComponent->GetOuter() != NativeDefaults ||
                                    !NativeComponent->HasAnyFlags(RF_DefaultSubObject))
                                {
                                    Error = TEXT("Migration cannot resolve the constructor component for a Blueprint timeline binding: ") + Property->GetPathName();
                                }
                                else
                                {
                                    Value.Kind = EMigrationValueKind::ConstructorComponent;
                                    Value.Text = NativeComponent->GetName();
                                }
                            }
                        }
                    }
                    if (Property->ArrayDim > 1)
                    {
                        Value.Name += FString::Format(TEXT("[{0}]"), {ArrayIndex});
                    }
                    Values.Add(MoveTemp(Value));
                }
            }
        }
        return Values;
    }

    void CaptureObject(UObject* Object)
    {
        if (!Object || Objects.Contains(Object))
        {
            return;
        }
        if (AActor* Actor = Cast<AActor>(Object))
        {
            if (IsMigrationRecoveryRoot(Actor->GetRootComponent()))
            {
                FMigrationRecoveryRoot& Recovery = RecoveryRoots.FindOrAdd(Actor->GetRootComponent());
                Recovery.Owner.Reset(Actor);
                Recovery.Transform = Actor->GetActorTransform();
            }
        }
        FMigrationObject Snapshot;
        CaptureObjectPath(Object);
        CaptureObjectPath(Object->GetClass());
        CaptureSceneObject(Object);
        Snapshot.Source.Reset(Object);
        UClass* DestinationClass = Object->GetClass();
        if (UField* const* Replacement = TypeMappings.Find(DestinationClass))
        {
            DestinationClass = CastChecked<UClass>(*Replacement);
        }
        else if (UClass* const* DefaultOnlyReplacement = DefaultOnlyClassReplacements.Find(DestinationClass))
        {
            DestinationClass = *DefaultOnlyReplacement;
        }
        Snapshot.DestinationClass.Reset(DestinationClass);
        CaptureObjectPath(DestinationClass);
        Objects.Add(Object, MoveTemp(Snapshot));
        TArray<FMigrationValue> Properties = CaptureStructure(Object->GetClass(), Object);
        AActor* SourceActor = Cast<AActor>(Object);
        UBlueprintGeneratedClass* SourceActorClass = Cast<UBlueprintGeneratedClass>(Object->GetClass());
        if (SourceActor && SourceActor->IsTemplate() && !SourceActor->GetRootComponent() &&
            SourceActorClass && SourceActorClass->SimpleConstructionScript && DestinationClass->HasAnyClassFlags(CLASS_Native))
        {
            USCS_Node* RootNode = nullptr;
            SourceActorClass->SimpleConstructionScript->GetSceneRootComponentTemplate(true, &RootNode);
            if (RootNode && UBlueprintNativizationLibrary::IsRootComponentTemplate(RootNode->ComponentTemplate))
            {
                FName RootMemberName = RootNode->GetVariableName();
                if (FProperty* SourceProperty = FindFProperty<FProperty>(SourceActorClass, RootMemberName))
                {
                    RootMemberName = FName(*GetMemberName(SourceProperty));
                }
                AActor* NativeDefaults = Cast<AActor>(DestinationClass->GetDefaultObject());
                FObjectPropertyBase* RootProperty = FindFProperty<FObjectPropertyBase>(DestinationClass, RootMemberName);
                if (NativeDefaults && NativeDefaults->GetRootComponent() && RootProperty &&
                    RootProperty->GetObjectPropertyValue_InContainer(NativeDefaults) == NativeDefaults->GetRootComponent())
                {
                    Properties.RemoveAll([](const FMigrationValue& Property)
                    {
                        return Property.Name == TEXT("RootComponent") && Property.Kind == EMigrationValueKind::Object &&
                            !Property.Object.IsValid();
                    });
                }
            }
        }
        UActorComponent* SourceComponent = Cast<UActorComponent>(Object);
        USCS_Node* SourceComponentNode = UBlueprintNativizationLibrary::FindSCSNodeForComponent(SourceComponent);
        for (UObject* Archetype = Object->GetArchetype(); !SourceComponentNode && Archetype && Archetype != Object;)
        {
            SourceComponentNode = UBlueprintNativizationLibrary::FindSCSNodeForComponent(Cast<UActorComponent>(Archetype));
            UObject* ParentArchetype = Archetype->GetArchetype();
            if (ParentArchetype == Archetype)
            {
                break;
            }
            Archetype = ParentArchetype;
        }
        if (SourceComponentNode)
        {
            // SCS stores the hierarchy separately; its template fields must not erase constructor attachments.
            Properties.RemoveAll([](const FMigrationValue& Property)
            {
                return Property.SourcePath.StartsWith(USceneComponent::StaticClass()->GetPathName() + TEXT(":")) &&
                    (Property.Name == TEXT("AttachParent") || Property.Name == TEXT("AttachSocketName") || Property.Name == TEXT("AttachChildren"));
            });
        }
        Objects.FindChecked(Object).Properties = MoveTemp(Properties);
    }

    FMigrationValue CaptureValue(FProperty* Property, const void* Address)
    {
        FMigrationValue Value;
        Value.SourcePath = Property->GetPathName();
        if (FSoftObjectProperty* SoftObject = CastField<FSoftObjectProperty>(Property))
        {
            Value.Kind = EMigrationValueKind::SoftObject;
            Value.Text = SoftObject->GetPropertyValue(Address).ToSoftObjectPath().ToString();
        }
        else if (FObjectPropertyBase* Object = CastField<FObjectPropertyBase>(Property))
        {
            Value.Kind = EMigrationValueKind::Object;
            Value.Object.Reset(Object->GetObjectPropertyValue(Address));
            if (bKeepAssetReferencesInBlueprint && Property->IsA<FClassProperty>())
            {
                if (UClass* Class = Cast<UClass>(Value.Object.Get()))
                {
                    Value.BlueprintClass.Reset(Cast<UBlueprint>(Class->ClassGeneratedBy));
                }
            }
            CaptureObjectPath(Object->PropertyClass);
            if (Value.Object)
            {
                CaptureObjectPath(Value.Object.Get());
                CaptureObjectPath(Value.Object->GetClass());
                CaptureSceneObject(Value.Object.Get());
                Value.ObjectClassPath = GetDiagnosticObjectPath(Value.Object->GetClass());
            }
            if (Value.Object && (Property->HasAnyPropertyFlags(CPF_InstancedReference) ||
                (Property->GetOwnerClass() == UChildActorComponent::StaticClass() && Property->GetFName() == TEXT("ChildActorTemplate"))))
            {
                CaptureObject(Value.Object.Get());
            }
        }
        else if (FEnumProperty* Enumeration = CastField<FEnumProperty>(Property))
        {
            Value.Kind = EMigrationValueKind::Number;
            Value.Text = LexToString(Enumeration->GetUnderlyingProperty()->GetSignedIntPropertyValue(Address));
        }
        else if (FNumericProperty* Number = CastField<FNumericProperty>(Property))
        {
            Value.Kind = EMigrationValueKind::Number;
            Value.Text = Number->GetNumericPropertyValueToString(Address);
        }
        else if (FStructProperty* Structure = CastField<FStructProperty>(Property))
        {
            Value.Kind = EMigrationValueKind::Structure;
            Value.Structure.Reset(Structure->Struct);
            if (Structure->Struct == FInstancedStruct::StaticStruct())
            {
                Value.Kind = EMigrationValueKind::InstancedStructure;
                const FInstancedStruct& Instance = *static_cast<const FInstancedStruct*>(Address);
                Value.Structure.Reset(const_cast<UScriptStruct*>(Instance.GetScriptStruct()));
                if (Instance.IsValid())
                {
                    Value.Children = CaptureStructure(Value.Structure.Get(), Instance.GetMemory());
                }
            }
            else
            {
                if (Structure->Struct->IsNative() && Structure->Struct != FInstancedPropertyBag::StaticStruct() &&
                    !Structure->Struct->IsChildOf(FAnimNode_Base::StaticStruct()) &&
                    (Structure->Struct->StructFlags & (STRUCT_SerializeNative | STRUCT_CopyNative | STRUCT_AddStructReferencedObjects)))
                {
                    Value.Kind = EMigrationValueKind::NativeStructure;
                    Value.NativeValues = MakeShared<FStructOnScope>(Structure->Struct);
                    if (!DeserializeMigrationStructure(Structure->Struct, SerializeMigrationStructure(Structure->Struct, Address),
                        Value.NativeValues->GetStructMemory()))
                    {
                        Error = TEXT("Cannot capture the native configuration structure: ") + Value.SourcePath;
                    }
                    FMigrationStructureObjectCollector Collector([&](UObject*& Object)
                    {
                        if (Object)
                        {
                            Value.NativeObjects.Emplace(Object);
                            if (!Object->IsAsset() && !Object->IsA<UField>() && Object->GetOuter() && Object->GetOuter()->IsTemplate())
                            {
                                CaptureObject(Object);
                            }
                        }
                    });
                    Collector.AddPropertyReferencesWithStructARO(Structure->Struct, Value.NativeValues->GetStructMemory());
                }
                else
                {
                    Value.Children = CaptureStructure(Structure->Struct, Address);
                }
                Property->ExportText_Direct(Value.Text, Address, Address, nullptr, PPF_None);
            }
        }
        else if (FArrayProperty* Array = CastField<FArrayProperty>(Property))
        {
            Value.Kind = EMigrationValueKind::Array;
            FScriptArrayHelper Values(Array, Address);
            for (int32 Index = 0; Index < Values.Num(); ++Index)
            {
                Value.Children.Add(CaptureValue(Array->Inner, Values.GetRawPtr(Index)));
            }
        }
        else if (FSetProperty* Set = CastField<FSetProperty>(Property))
        {
            Value.Kind = EMigrationValueKind::Set;
            FScriptSetHelper Values(Set, Address);
            for (int32 Index = 0; Index < Values.GetMaxIndex(); ++Index)
            {
                if (Values.IsValidIndex(Index))
                {
                    Value.Children.Add(CaptureValue(Set->ElementProp, Values.GetElementPtr(Index)));
                }
            }
        }
        else if (FMapProperty* Map = CastField<FMapProperty>(Property))
        {
            Value.Kind = EMigrationValueKind::Map;
            FScriptMapHelper Values(Map, Address);
            for (int32 Index = 0; Index < Values.GetMaxIndex(); ++Index)
            {
                if (Values.IsValidIndex(Index))
                {
                    Value.Children.Add(CaptureValue(Map->KeyProp, Values.GetKeyPtr(Index)));
                    Value.Children.Add(CaptureValue(Map->ValueProp, Values.GetValuePtr(Index)));
                }
            }
        }
        else
        {
            Property->ExportText_Direct(Value.Text, Address, Address, nullptr, PPF_None);
        }
        return Value;
    }

    UScriptStruct* ResolveStructure(UScriptStruct* Original) const
    {
        if (UField* const* Replacement = TypeMappings.Find(Original))
        {
            return Cast<UScriptStruct>(*Replacement);
        }
        return Original;
    }

    FProperty* FindProperty(UStruct* Structure, const FMigrationValue& Value, int32& ArrayIndex)
    {
        FString Name = Value.Name;
        ArrayIndex = 0;
        int32 Bracket = INDEX_NONE;
        if (Name.FindLastChar(TEXT('['), Bracket))
        {
            ArrayIndex = FCString::Atoi(*Name.Mid(Bracket + 1));
            Name.LeftInline(Bracket);
        }
        FProperty* Property = FindFProperty<FProperty>(Structure, *Name);
        if (!Property || ArrayIndex >= Property->ArrayDim)
        {
            Error = FString::Format(TEXT("Migration cannot resolve property {0} as {1} in {2}."),
                {Value.SourcePath, Value.Name, Structure->GetPathName()});
            return nullptr;
        }
        return Property;
    }

    bool ValidateStructure(UStruct* Structure, const TArray<FMigrationValue>& Values)
    {
        for (const FMigrationValue& Value : Values)
        {
            int32 ArrayIndex = 0;
            FProperty* Property = FindProperty(Structure, Value, ArrayIndex);
            if (!Property || !ValidateValue(Property, Value))
            {
                return false;
            }
        }
        return true;
    }

    bool ValidateValue(FProperty* Property, const FMigrationValue& Value)
    {
        if (Value.Kind == EMigrationValueKind::NativeStructure)
        {
            FStructProperty* StructureProperty = CastField<FStructProperty>(Property);
            if (StructureProperty && StructureProperty->Struct == Value.Structure.Get() && Value.NativeValues.IsValid())
            {
                return true;
            }
            Error = TEXT("Native configuration structure identity changed: ") + Value.SourcePath;
            return false;
        }
        if (Value.Kind == EMigrationValueKind::Structure)
        {
            FStructProperty* Structure = CastField<FStructProperty>(Property);
            if (Structure && Structure->Struct == FInstancedStruct::StaticStruct() &&
                FRetainedBlueprintGeneration::IsExternalStructure(Value.Structure.Get()))
            {
                return ValidateStructure(Value.Structure.Get(), Value.Children);
            }
            if (Structure && Structure->Struct == ResolveStructure(Value.Structure.Get()))
            {
                return ValidateStructure(Structure->Struct, Value.Children);
            }
        }
        else if (Value.Kind == EMigrationValueKind::InstancedStructure)
        {
            FStructProperty* Structure = CastField<FStructProperty>(Property);
            if (Structure && Structure->Struct == FInstancedStruct::StaticStruct())
            {
                if (!Value.Structure)
                {
                    return true;
                }
                return ValidateStructure(ResolveStructure(Value.Structure.Get()), Value.Children);
            }
        }
        else if (Value.Kind == EMigrationValueKind::Array)
        {
            if (FArrayProperty* Array = CastField<FArrayProperty>(Property))
            {
                for (const FMigrationValue& Element : Value.Children)
                {
                    if (!ValidateValue(Array->Inner, Element))
                    {
                        return false;
                    }
                }
                return true;
            }
        }
        else if (Value.Kind == EMigrationValueKind::Set)
        {
            if (FSetProperty* Set = CastField<FSetProperty>(Property))
            {
                for (const FMigrationValue& Element : Value.Children)
                {
                    if (!ValidateValue(Set->ElementProp, Element))
                    {
                        return false;
                    }
                }
                return true;
            }
        }
        else if (Value.Kind == EMigrationValueKind::Map)
        {
            if (FMapProperty* Map = CastField<FMapProperty>(Property))
            {
                for (int32 Index = 0; Index < Value.Children.Num(); Index += 2)
                {
                    if (!ValidateValue(Map->KeyProp, Value.Children[Index]) ||
                        !ValidateValue(Map->ValueProp, Value.Children[Index + 1]))
                    {
                        return false;
                    }
                }
                return true;
            }
        }
        else if (((Value.Kind == EMigrationValueKind::Object || Value.Kind == EMigrationValueKind::ConstructorComponent) && Property->IsA<FObjectPropertyBase>()) ||
            (Value.Kind == EMigrationValueKind::SoftObject && Property->IsA<FSoftObjectProperty>()) ||
            (Value.Kind == EMigrationValueKind::Number && (Property->IsA<FNumericProperty>() || Property->IsA<FEnumProperty>())) ||
            Value.Kind == EMigrationValueKind::Text)
        {
            return true;
        }
        Error = TEXT("Incompatible migration value: ") + Value.SourcePath + TEXT(" -> ") + Property->GetPathName();
        return false;
    }

    UObject* ResolveObject(UObject* Source, UObject* Owner)
    {
        if (!Source)
        {
            return nullptr;
        }
        if (const FString* ScenePath = SceneObjectPaths.Find(Source))
        {
            if (bCaptureObjectPaths)
            {
                return Source;
            }
            if (!SceneObjectResolver)
            {
                Error = TEXT("Migration scene object resolver is not initialized for ") + *ScenePath;
                return nullptr;
            }
            UObject* Destination = SceneObjectResolver(Source, *ScenePath);
            if (!Destination)
            {
                if (Error.IsEmpty())
                {
                    Error = TEXT("Migration cannot resolve the current scene object for ") + *ScenePath;
                }
                return nullptr;
            }
            MapObject(Source, Destination);
            return Destination;
        }
        if (UObject* const* Replacement = ObjectMappings.Find(Source))
        {
            return *Replacement;
        }
        if (!Source->IsAsset() && Objects.Contains(Source))
        {
            UClass* Class = Objects.FindChecked(Source).DestinationClass.Get();
            if (bKeepAssetReferencesInBlueprint)
            {
                if (UBlueprint* SourceBlueprint = UBlueprint::GetBlueprintFromClass(Source->GetClass()))
                {
                    Class = SourceBlueprint->GeneratedClass;
                }
            }
            EObjectFlags ObjectFlags = Source->GetFlags() & RF_Transactional;
            if (Source->IsTemplate())
            {
                ObjectFlags |= RF_ArchetypeObject | RF_Public;
            }
            UObject* Destination = nullptr;
            if (Owner)
            {
                Destination = FindObjectFast<UObject>(Owner, Source->GetFName());
                if (Destination && Destination->GetClass() != Class)
                {
                    Destination = nullptr;
                }
            }
            if (!Destination)
            {
                Destination = NewObject<UObject>(Owner, Class, NAME_None, ObjectFlags);
            }
            MapObject(Source, Destination);
            if (!ApplyStructure(Destination->GetClass(), Destination, Objects.FindChecked(Source).Properties, Destination))
            {
                return nullptr;
            }
            return Destination;
        }
        return Source;
    }

    bool ApplyStructure(UStruct* Structure, void* Memory, const TArray<FMigrationValue>& Values, UObject* Owner)
    {
        for (const FMigrationValue& Value : Values)
        {
            int32 ArrayIndex = 0;
            FProperty* Property = FindProperty(Structure, Value, ArrayIndex);
            if (!Property || !ApplyValue(Property, Property->ContainerPtrToValuePtr<void>(Memory, ArrayIndex), Value, Owner))
            {
                return false;
            }
        }
        if (Memory == Owner)
        {
            if (UNiagaraComponent* NiagaraComponent = Cast<UNiagaraComponent>(Owner))
            {
                NiagaraComponent->GetOverrideParameters().SetOwner(NiagaraComponent);
                NiagaraComponent->GetOverrideParameters().Rebind();
            }
            if (USceneCaptureComponent* SceneCaptureComponent = Cast<USceneCaptureComponent>(Owner))
            {
                SceneCaptureComponent->SetShowFlagSettings(SceneCaptureComponent->GetShowFlagSettings());
            }
        }
        return true;
    }

    bool ApplyValue(FProperty* Property, void* Address, const FMigrationValue& Value, UObject* Owner)
    {
        if (MigrationValueDiagnostics.GetValueOnGameThread() != 0 && Property->GetFName() == TEXT("AttachSocketName"))
        {
            UE_LOG(LogTemp, Display, TEXT("Migration attachment socket: owner '%s'; source property '%s'; value '%s'."),
                *Owner->GetPathName(), *Value.SourcePath, *Value.Text);
        }
        if (Value.Kind == EMigrationValueKind::NativeStructure)
        {
            FStructProperty* StructureProperty = CastFieldChecked<FStructProperty>(Property);
            StructureProperty->Struct->CopyScriptStruct(Address, Value.NativeValues->GetStructMemory());
            FMigrationStructureObjectCollector Collector([&](UObject*& Object)
            {
                if (UClass* Class = Cast<UClass>(Object))
                {
                    if (UBlueprint* Blueprint = Cast<UBlueprint>(Class->ClassGeneratedBy))
                    {
                        Object = Blueprint->GeneratedClass.Get();
                        return;
                    }
                }
                Object = ResolveObject(Object, Owner);
            });
            Collector.AddPropertyReferencesWithStructARO(StructureProperty->Struct, Address, Owner);
            return Error.IsEmpty();
        }
        if (Value.Kind == EMigrationValueKind::ConstructorComponent)
        {
            FObjectPropertyBase* ObjectProperty = CastFieldChecked<FObjectPropertyBase>(Property);
            UObject* Component = Owner->GetDefaultSubobjectByName(FName(*Value.Text));
            if (!Component || Component->GetOuter() != Owner || !Component->IsA(ObjectProperty->PropertyClass))
            {
                Error = TEXT("Migration cannot restore the constructor component binding: ") + Owner->GetPathName() + TEXT(":") + Value.Name;
                return false;
            }
            ObjectProperty->SetObjectPropertyValue(Address, Component);
            return true;
        }
        if (Value.Kind == EMigrationValueKind::Object)
        {
            if (MigrationValueDiagnostics.GetValueOnGameThread() != 0)
            {
                UE_LOG(LogTemp, Display, TEXT("Migration object replay: source property '%s'; owner '%s'; property address %s; source object '%s'; source class at capture '%s'."),
                    *Value.SourcePath, *GetDiagnosticObjectPath(Owner), *LexToString(reinterpret_cast<UPTRINT>(Property)),
                    *GetDiagnosticObjectPath(Value.Object.Get()), *Value.ObjectClassPath);
            }
            FObjectPropertyBase* Object = CastFieldChecked<FObjectPropertyBase>(Property);
            TStrongObjectPtr<UClass> ExpectedClass(Object->PropertyClass.Get());
            if (!bCaptureObjectPaths && Property->GetOwnerClass() == AActor::StaticClass() && Property->GetFName() == TEXT("RootComponent"))
            {
                if (const FMigrationRecoveryRoot* Recovery = RecoveryRoots.Find(Value.Object.Get()))
                {
                    AActor* Actor = Cast<AActor>(Owner);
                    USceneComponent* Root = nullptr;
                    if (Actor)
                    {
                        Root = Actor->GetRootComponent();
                    }
                    if (!IsValid(Root) || Root->IsBeingDestroyed() || Root->GetOwner() != Actor ||
                        !Actor->GetComponents().Contains(Root) || !Root->IsA(ExpectedClass.Get()) ||
                        ResolveObject(Recovery->Owner.Get(), Owner) != Actor || IsMigrationRecoveryRoot(Root))
                    {
                        Error = TEXT("Migration cannot restore the actor transform after replacing its Blueprint error recovery root: ") + GetDiagnosticObjectPath(Recovery->Owner.Get());
                        return false;
                    }
                    Object->SetObjectPropertyValue(Address, Root);
                    Actor->SetActorTransform(Recovery->Transform, false, nullptr, ETeleportType::TeleportPhysics);
                    return true;
                }
            }
            UObject* Destination = nullptr;
            if (Value.BlueprintClass)
            {
                Destination = Value.BlueprintClass->GeneratedClass;
            }
            else
            {
                Destination = ResolveObject(Value.Object.Get(), Owner);
            }
            if (!Error.IsEmpty())
            {
                return false;
            }
            if (Destination && !Destination->IsA(ExpectedClass.Get()))
            {
                Error = FString::Format(TEXT("Incompatible migrated object for {0} on {1}: {2} (source class at capture: {3}) -> {4} (destination class: {5}), expected {6}."),
                    {Value.SourcePath + TEXT(" -> ") + Property->GetName(), GetDiagnosticObjectPath(Owner),
                        GetDiagnosticObjectPath(Value.Object.Get()), Value.ObjectClassPath,
                        GetDiagnosticObjectPath(Destination), GetDiagnosticObjectPath(Destination->GetClass()),
                        GetDiagnosticObjectPath(ExpectedClass.Get())});
                return false;
            }
            Object->SetObjectPropertyValue(Address, Destination);
            if (Property->GetOwnerClass() == UStaticMeshComponent::StaticClass() &&
                Property->GetFName() == UStaticMeshComponent::GetMemberNameChecked_StaticMesh())
            {
                CastChecked<UStaticMeshComponent>(Owner)->PostApplyToComponent();
            }
        }
        else if (Value.Kind == EMigrationValueKind::SoftObject)
        {
            FSoftObjectPath Path(Value.Text);
            if (!bKeepAssetReferencesInBlueprint || !Property->IsA<FSoftClassProperty>())
            {
                if (const FSoftObjectPath* Replacement = SoftMappings.Find(Path))
                {
                    Path = *Replacement;
                }
            }
            CastFieldChecked<FSoftObjectProperty>(Property)->SetPropertyValue(Address, FSoftObjectPtr(Path));
        }
        else if (Value.Kind == EMigrationValueKind::Number)
        {
            FNumericProperty* Number = CastField<FNumericProperty>(Property);
            if (FEnumProperty* Enumeration = CastField<FEnumProperty>(Property))
            {
                Number = Enumeration->GetUnderlyingProperty();
            }
            Number->SetNumericPropertyValueFromString(Address, *Value.Text);
        }
        else if (Value.Kind == EMigrationValueKind::Structure)
        {
            FStructProperty* Structure = CastFieldChecked<FStructProperty>(Property);
            if (Structure->Struct == FInstancedStruct::StaticStruct() &&
                FRetainedBlueprintGeneration::IsExternalStructure(Value.Structure.Get()))
            {
                FInstancedStruct& Instance = *static_cast<FInstancedStruct*>(Address);
                Instance.InitializeAs(Value.Structure.Get());
                return ApplyStructure(Value.Structure.Get(), Instance.GetMutableMemory(), Value.Children, Owner);
            }
            if (Value.Children.IsEmpty())
            {
                if (!Property->ImportText_Direct(*Value.Text, Address, Owner, PPF_None))
                {
                    Error = TEXT("Migration failed to import native value: ") + Value.SourcePath;
                    return false;
                }
            }
            else
            {
                return ApplyStructure(Structure->Struct, Address, Value.Children, Owner);
            }
        }
        else if (Value.Kind == EMigrationValueKind::InstancedStructure)
        {
            FInstancedStruct& Instance = *static_cast<FInstancedStruct*>(Address);
            Instance.InitializeAs(ResolveStructure(Value.Structure.Get()));
            if (Instance.IsValid())
            {
                return ApplyStructure(const_cast<UScriptStruct*>(Instance.GetScriptStruct()), Instance.GetMutableMemory(), Value.Children, Owner);
            }
        }
        else if (Value.Kind == EMigrationValueKind::Array)
        {
            FArrayProperty* Array = CastFieldChecked<FArrayProperty>(Property);
            FScriptArrayHelper Values(Array, Address);
            Values.EmptyAndAddValues(Value.Children.Num());
            for (int32 Index = 0; Index < Value.Children.Num(); ++Index)
            {
                if (!ApplyValue(Array->Inner, Values.GetRawPtr(Index), Value.Children[Index], Owner))
                {
                    return false;
                }
            }
        }
        else if (Value.Kind == EMigrationValueKind::Set)
        {
            FSetProperty* Set = CastFieldChecked<FSetProperty>(Property);
            FScriptSetHelper Values(Set, Address);
            Values.EmptyElements();
            for (const FMigrationValue& Element : Value.Children)
            {
                const int32 Index = Values.AddDefaultValue_Invalid_NeedsRehash();
                if (!ApplyValue(Set->ElementProp, Values.GetElementPtr(Index), Element, Owner))
                {
                    return false;
                }
            }
            Values.Rehash();
        }
        else if (Value.Kind == EMigrationValueKind::Map)
        {
            FMapProperty* Map = CastFieldChecked<FMapProperty>(Property);
            FScriptMapHelper Values(Map, Address);
            Values.EmptyValues();
            for (int32 Index = 0; Index < Value.Children.Num(); Index += 2)
            {
                const int32 Destination = Values.AddDefaultValue_Invalid_NeedsRehash();
                if (!ApplyValue(Map->KeyProp, Values.GetKeyPtr(Destination), Value.Children[Index], Owner) ||
                    !ApplyValue(Map->ValueProp, Values.GetValuePtr(Destination), Value.Children[Index + 1], Owner))
                {
                    return false;
                }
            }
            Values.Rehash();
        }
        else if (!Property->ImportText_Direct(*Value.Text, Address, Owner, PPF_None))
        {
            Error = TEXT("Migration failed to import value: ") + Value.SourcePath;
            return false;
        }
        return true;
    }
};


static UActorComponent* ResolveConfigurationComponent(UClass* OwnerClass, UActorComponent* Component)
{
    if (!OwnerClass || !Component || !OwnerClass->IsChildOf(AActor::StaticClass()))
    {
        return Component;
    }
    TArray<FName> ComponentNames;
    const TArray<UActorComponent*> Components = UBlueprintNativizationLibrary::GetClassComponentTemplates(
        ComponentNames, OwnerClass, UActorComponent::StaticClass());
    for (UActorComponent* Candidate : Components)
    {
        if (Candidate == Component || Candidate->GetFName() == Component->GetFName())
        {
            return Candidate;
        }
    }
    return Component;
}

static TSharedPtr<FJsonObject> SerializeConfigurationValue(const FMigrationValue& Value, const TSet<UObject*>& CapturedObjects)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("Kind"), static_cast<int32>(Value.Kind));
    Result->SetStringField(TEXT("Name"), Value.Name);
    Result->SetStringField(TEXT("Text"), Value.Text);
    Result->SetStringField(TEXT("SourcePath"), Value.SourcePath);
    Result->SetStringField(TEXT("ObjectClassPath"), Value.ObjectClassPath);
    if (Value.Object)
    {
        FString Field = TEXT("ObjectPath");
        if (CapturedObjects.Contains(Value.Object.Get()))
        {
            Field = TEXT("ObjectId");
        }
        Result->SetStringField(Field, Value.Object->GetPathName());
    }
    if (Value.BlueprintClass)
    {
        Result->SetStringField(TEXT("BlueprintPath"), Value.BlueprintClass->GetPathName());
    }
    if (Value.Structure)
    {
        Result->SetStringField(TEXT("StructurePath"), Value.Structure->GetPathName());
    }
    if (Value.Kind == EMigrationValueKind::NativeStructure)
    {
        Result->SetStringField(TEXT("NativeData"), FBase64::Encode(SerializeMigrationStructure(
            Value.Structure.Get(), Value.NativeValues->GetStructMemory())));
    }
    TArray<TSharedPtr<FJsonValue>> Children;
    for (const FMigrationValue& Child : Value.Children)
    {
        Children.Add(MakeShared<FJsonValueObject>(SerializeConfigurationValue(Child, CapturedObjects)));
    }
    Result->SetArrayField(TEXT("Children"), Children);
    return Result;
}

bool UNativizationV2Subsystem::CaptureConfigurationTemplates(const TMap<UField*, FName>& TranslatedObjects,
    const TMap<FString, FString>& MemberNames, bool bKeepAssetReferencesInBlueprint,
    TSharedPtr<FJsonObject>& Result, FString& Error) const
{
    TMap<UField*, UField*> IdentityMappings;
    for (const TPair<UField*, FName>& Field : TranslatedObjects)
    {
        IdentityMappings.Add(Field.Key, Field.Key);
    }
    const TMap<UClass*, UClass*> NoReplacements;
    FMigrationValues Values(IdentityMappings, MemberNames, NoReplacements, bKeepAssetReferencesInBlueprint);
    Result = MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("Version"), 1);
    TSharedPtr<FJsonObject> Roots = MakeShared<FJsonObject>();
    TSet<UObject*> RootObjects;
    const TArray<FString> Categories = { TEXT(":RuntimeComponentTemplate:"), TEXT(":ComponentInstanceTemplate:"),
        TEXT(":InstancePropertyTemplate:"), TEXT(":ComponentTemplate:") };
    TMap<FString, FString> ContextMembers(MemberNames);
    for (const TPair<FString, FString>& Member : MemberNames)
    {
        for (const FString& Category : Categories)
        {
            const int32 Separator = Member.Key.Find(Category);
            if (Separator == INDEX_NONE)
            {
                continue;
            }
            UClass* DeclaringClass = LoadObject<UClass>(nullptr, *Member.Key.Left(Separator));
            if (DeclaringClass)
            {
                for (const TPair<UField*, FName>& Field : TranslatedObjects)
                {
                    UClass* DerivedClass = Cast<UClass>(Field.Key);
                    if (DerivedClass && DerivedClass->IsChildOf(DeclaringClass))
                    {
                        ContextMembers.FindOrAdd(DerivedClass->GetPathName() + Member.Key.Mid(Separator), Member.Value);
                    }
                }
            }
            break;
        }
    }
    for (const TPair<FString, FString>& Member : ContextMembers)
    {
        for (const FString& Category : Categories)
        {
            const int32 Separator = Member.Key.Find(Category);
            if (Separator == INDEX_NONE)
            {
                continue;
            }
            UClass* OwnerClass = LoadObject<UClass>(nullptr, *Member.Key.Left(Separator));
            const FString Identifier = Member.Key.Mid(Separator + Category.Len());
            UObject* Object = nullptr;
            FString PropertyName;
            bool bResolved = false;
            if (Category == TEXT(":RuntimeComponentTemplate:"))
            {
                Object = ResolveConfigurationComponent(OwnerClass, LoadObject<UActorComponent>(nullptr, *Identifier));
                bResolved = Object != nullptr;
                if (Object && OwnerClass && OwnerClass->IsChildOf(AActor::StaticClass()))
                {
                    TArray<FName> ComponentNames;
                    const TArray<UActorComponent*> Components = UBlueprintNativizationLibrary::GetClassComponentTemplates(
                        ComponentNames, OwnerClass, UActorComponent::StaticClass());
                    const int32 ComponentIndex = Components.IndexOfByKey(CastChecked<UActorComponent>(Object));
                    if (ComponentNames.IsValidIndex(ComponentIndex))
                    {
                        PropertyName = ComponentNames[ComponentIndex].ToString();
                        if (FProperty* ComponentProperty = FindFProperty<FProperty>(OwnerClass, ComponentNames[ComponentIndex]))
                        {
                            PropertyName = Values.GetMemberName(ComponentProperty);
                        }
                    }
                }
            }
            else if (Category == TEXT(":ComponentTemplate:"))
            {
                if (UBlueprintGeneratedClass* BlueprintClass = Cast<UBlueprintGeneratedClass>(OwnerClass))
                {
                    const FName TemplateName(*Identifier);
                    for (UClass* Candidate = BlueprintClass; Candidate && !Object; Candidate = Candidate->GetSuperClass())
                    {
                        if (UBlueprintGeneratedClass* CandidateBlueprint = Cast<UBlueprintGeneratedClass>(Candidate))
                        {
                            if (UInheritableComponentHandler* Overrides = CandidateBlueprint->GetInheritableComponentHandler())
                            {
                                Object = Overrides->GetOverridenComponentTemplate(Overrides->FindKey(TemplateName));
                            }
                            if (!Object)
                            {
                                Object = CandidateBlueprint->FindComponentTemplateByName(TemplateName);
                            }
                        }
                    }
                }
                bResolved = Object != nullptr;
            }
            else if (Category == TEXT(":ComponentInstanceTemplate:"))
            {
                const int32 PropertySeparator = Identifier.Find(TEXT(":"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
                if (PropertySeparator != INDEX_NONE)
                {
                    if (UActorComponent* Component = LoadObject<UActorComponent>(nullptr, *Identifier.Left(PropertySeparator)))
                    {
                        Component = ResolveConfigurationComponent(OwnerClass, Component);
                        FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Component->GetClass(), FName(*Identifier.Mid(PropertySeparator + 1)));
                        if (Property)
                        {
                            Object = Property->GetObjectPropertyValue_InContainer(Component);
                            PropertyName = Values.GetMemberName(Property);
                            bResolved = true;
                        }
                    }
                }
            }
            else if (OwnerClass)
            {
                for (TFieldIterator<FProperty> Property(OwnerClass); Property; ++Property)
                {
                    if (Property->GetPathName() == Identifier)
                    {
                        if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(*Property))
                        {
                            Object = ObjectProperty->GetObjectPropertyValue_InContainer(OwnerClass->GetDefaultObject());
                            PropertyName = Values.GetMemberName(*Property);
                            bResolved = true;
                        }
                        break;
                    }
                }
            }
            if (!bResolved)
            {
                Error = TEXT("Cannot capture the required configuration template: ") + Member.Key;
                return false;
            }
            TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
            Root->SetStringField(TEXT("ObjectId"), FString());
            Root->SetStringField(TEXT("PropertyName"), PropertyName);
            Root->SetStringField(TEXT("DestinationMemberName"), Member.Value);
            if (Object)
            {
                RootObjects.Add(Object);
                Values.CaptureObject(Object);
                Root->SetStringField(TEXT("ObjectId"), Object->GetPathName());
            }
            Roots->SetObjectField(Member.Key, Root);
            break;
        }
    }
    auto IsOwnedConfigurationObject = [&RootObjects](UObject* Object)
    {
        if (!Object || Object->IsAsset() || Object->IsA<UField>())
        {
            return false;
        }
        for (UObject* Root : RootObjects)
        {
            if (Object == Root || Object->IsIn(Root))
            {
                return true;
            }
            UClass* TemplateOwner = Root->GetTypedOuter<UClass>();
            if (TemplateOwner && Object->IsTemplate() && Object->IsIn(TemplateOwner))
            {
                return true;
            }
        }
        return false;
    };
    TSet<UObject*> CapturedObjects;
    TArray<UObject*> PendingObjects = RootObjects.Array();
    for (int32 ObjectIndex = 0; ObjectIndex < PendingObjects.Num(); ++ObjectIndex)
    {
        UObject* Object = PendingObjects[ObjectIndex];
        if (CapturedObjects.Contains(Object))
        {
            continue;
        }
        CapturedObjects.Add(Object);
        Values.CaptureObject(Object);
        TArray<FMigrationValue> PendingValues = Values.Objects.FindChecked(Object).Properties;
        for (int32 ValueIndex = 0; ValueIndex < PendingValues.Num(); ++ValueIndex)
        {
            FMigrationValue Value = PendingValues[ValueIndex];
            if (IsOwnedConfigurationObject(Value.Object.Get()))
            {
                PendingObjects.AddUnique(Value.Object.Get());
            }
            PendingValues.Append(Value.Children);
        }
    }
    TSharedPtr<FJsonObject> Objects = MakeShared<FJsonObject>();
    for (UObject* Object : CapturedObjects)
    {
        TSharedPtr<FJsonObject> Record = MakeShared<FJsonObject>();
        UClass* SourceClass = Object->GetClass();
        if (UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(SourceClass))
        {
            SourceClass = Blueprint->GeneratedClass;
        }
        Record->SetStringField(TEXT("ClassPath"), SourceClass->GetPathName());
        Record->SetStringField(TEXT("Name"), Object->GetName());
        Record->SetBoolField(TEXT("DefaultSubobject"), Object->HasAnyFlags(RF_DefaultSubObject));
        TArray<TSharedPtr<FJsonValue>> Properties;
        for (const FMigrationValue& Value : Values.Objects.FindChecked(Object).Properties)
        {
            Properties.Add(MakeShared<FJsonValueObject>(SerializeConfigurationValue(Value, CapturedObjects)));
        }
        Record->SetArrayField(TEXT("Properties"), Properties);
        Objects->SetObjectField(Object->GetPathName(), Record);
    }
    Result->SetObjectField(TEXT("Roots"), Roots);
    Result->SetObjectField(TEXT("Objects"), Objects);
    return true;
}

static bool DeserializeConfigurationValue(const TSharedPtr<FJsonObject>& Record,
    const TMap<FString, UObject*>& Objects, FMigrationValue& Value, FString& Error)
{
    double Kind = 0;
    const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
    if (!Record.IsValid() || !Record->TryGetNumberField(TEXT("Kind"), Kind) ||
        Kind < 0 || Kind > static_cast<int32>(EMigrationValueKind::NativeStructure) ||
        !Record->TryGetArrayField(TEXT("Children"), Children))
    {
        Error = TEXT("Invalid configuration value record.");
        return false;
    }
    Value.Kind = static_cast<EMigrationValueKind>(static_cast<int32>(Kind));
    Record->TryGetStringField(TEXT("Name"), Value.Name);
    Record->TryGetStringField(TEXT("Text"), Value.Text);
    Record->TryGetStringField(TEXT("SourcePath"), Value.SourcePath);
    Record->TryGetStringField(TEXT("ObjectClassPath"), Value.ObjectClassPath);
    FString Identifier;
    if (Record->TryGetStringField(TEXT("ObjectId"), Identifier))
    {
        Value.Object.Reset(Objects.FindRef(Identifier));
        if (!Value.Object)
        {
            Error = TEXT("Missing configuration object: ") + Identifier;
            return false;
        }
    }
    if (Record->TryGetStringField(TEXT("ObjectPath"), Identifier))
    {
        Value.Object.Reset(LoadObject<UObject>(nullptr, *Identifier));
        if (!Value.Object)
        {
            Error = TEXT("Missing configuration asset: ") + Identifier;
            return false;
        }
    }
    if (Record->TryGetStringField(TEXT("BlueprintPath"), Identifier))
    {
        Value.BlueprintClass.Reset(LoadObject<UBlueprint>(nullptr, *Identifier));
        if (!Value.BlueprintClass)
        {
            Error = TEXT("Missing configuration Blueprint: ") + Identifier;
            return false;
        }
    }
    if (Record->TryGetStringField(TEXT("StructurePath"), Identifier))
    {
        Value.Structure.Reset(LoadObject<UScriptStruct>(nullptr, *Identifier));
        if (!Value.Structure)
        {
            Error = TEXT("Missing configuration structure: ") + Identifier;
            return false;
        }
    }
    if (Value.Kind == EMigrationValueKind::NativeStructure)
    {
        FString NativeData;
        TArray<uint8> Bytes;
        if (!Value.Structure || !Record->TryGetStringField(TEXT("NativeData"), NativeData) || !FBase64::Decode(NativeData, Bytes))
        {
            Error = TEXT("Missing native configuration data: ") + Value.SourcePath;
            return false;
        }
        Value.NativeValues = MakeShared<FStructOnScope>(Value.Structure.Get());
        if (!DeserializeMigrationStructure(Value.Structure.Get(), Bytes, Value.NativeValues->GetStructMemory()))
        {
            Error = TEXT("Invalid serialized native configuration data: ") + Value.SourcePath;
            return false;
        }
        FMigrationStructureObjectCollector Collector([&](UObject*& Object)
        {
            if (Object)
            {
                if (UObject* ConfigurationObject = Objects.FindRef(Object->GetPathName()))
                {
                    Object = ConfigurationObject;
                }
                Value.NativeObjects.Emplace(Object);
            }
        });
        Collector.AddPropertyReferencesWithStructARO(Value.Structure.Get(), Value.NativeValues->GetStructMemory());
    }
    for (const TSharedPtr<FJsonValue>& Child : *Children)
    {
        FMigrationValue ChildValue;
        if (!DeserializeConfigurationValue(Child->AsObject(), Objects, ChildValue, Error))
        {
            return false;
        }
        Value.Children.Add(MoveTemp(ChildValue));
    }
    if (Value.Kind == EMigrationValueKind::Map && Value.Children.Num() % 2 != 0)
    {
        Error = TEXT("Unpaired configuration map values.");
        return false;
    }
    return true;
}

static bool HydrateConfigurationTemplates(const TSharedPtr<FJsonObject>& Configuration,
    FMigrationValues& Values, TMap<FString, UObject*>& Roots, TMap<FString, FString>& RootPropertyNames,
    TMap<FString, FString>& RootMemberNames, FString& Error)
{
    if (!Configuration.IsValid())
    {
        return true;
    }
    double Version = 0;
    const TSharedPtr<FJsonObject>* ObjectRecords = nullptr;
    const TSharedPtr<FJsonObject>* RootRecords = nullptr;
    if (!Configuration->TryGetNumberField(TEXT("Version"), Version) || Version != 1 ||
        !Configuration->TryGetObjectField(TEXT("Objects"), ObjectRecords) || !Configuration->TryGetObjectField(TEXT("Roots"), RootRecords))
    {
        Error = TEXT("Unsupported configuration template snapshot.");
        return false;
    }
    TMap<FString, UObject*> Objects;
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*ObjectRecords)->Values)
    {
        const TSharedPtr<FJsonObject> Record = Entry.Value->AsObject();
        FString ClassPath;
        if (!Record.IsValid() || !Record->TryGetStringField(TEXT("ClassPath"), ClassPath))
        {
            Error = TEXT("Invalid configuration object record: ") + Entry.Key;
            return false;
        }
        UClass* SourceClass = LoadObject<UClass>(nullptr, *ClassPath);
        if (!SourceClass || SourceClass->HasAnyClassFlags(CLASS_Abstract))
        {
            Error = TEXT("Cannot instantiate configuration source class: ") + ClassPath;
            return false;
        }
        FString ObjectName;
        bool bDefaultSubobject = false;
        Record->TryGetStringField(TEXT("Name"), ObjectName);
        Record->TryGetBoolField(TEXT("DefaultSubobject"), bDefaultSubobject);
        UObject* ConfigurationOwner = nullptr;
        {
            FScopedAllowAbstractClassAllocation AbstractOwnerAllocation;
            ConfigurationOwner = NewObject<UObject>(GetTransientPackage(), NAME_None, RF_Transient);
        }
        Values.ConfigurationOwners.Emplace(ConfigurationOwner);
        EObjectFlags SourceFlags = RF_Transient | RF_ArchetypeObject;
        if (bDefaultSubobject)
        {
            SourceFlags |= RF_DefaultSubObject;
        }
        UObject* Source = NewObject<UObject>(ConfigurationOwner, SourceClass, FName(*ObjectName), SourceFlags);
        FMigrationObject Snapshot;
        Snapshot.Source.Reset(Source);
        UClass* DestinationClass = SourceClass;
        if (UField* const* Replacement = Values.TypeMappings.Find(SourceClass))
        {
            DestinationClass = CastChecked<UClass>(*Replacement);
        }
        else if (UClass* const* DefaultClassReplacement = Values.DefaultOnlyClassReplacements.Find(SourceClass))
        {
            DestinationClass = *DefaultClassReplacement;
        }
        Snapshot.DestinationClass.Reset(DestinationClass);
        Values.Objects.Add(Source, MoveTemp(Snapshot));
        Values.CapturedObjectPaths.Add(Source, Entry.Key);
        Objects.Add(Entry.Key, Source);
    }
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*ObjectRecords)->Values)
    {
        const TArray<TSharedPtr<FJsonValue>>* Properties = nullptr;
        if (!Entry.Value->AsObject()->TryGetArrayField(TEXT("Properties"), Properties))
        {
            Error = TEXT("Missing configuration properties: ") + Entry.Key;
            return false;
        }
        for (const TSharedPtr<FJsonValue>& Property : *Properties)
        {
            FMigrationValue Value;
            if (!DeserializeConfigurationValue(Property->AsObject(), Objects, Value, Error))
            {
                return false;
            }
            Values.Objects.FindChecked(Objects.FindChecked(Entry.Key)).Properties.Add(MoveTemp(Value));
        }
    }
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*RootRecords)->Values)
    {
        const TSharedPtr<FJsonObject> Record = Entry.Value->AsObject();
        FString Identifier;
        FString PropertyName;
        if (!Record.IsValid() || !Record->TryGetStringField(TEXT("ObjectId"), Identifier))
        {
            Error = TEXT("Invalid configuration root: ") + Entry.Key;
            return false;
        }
        UObject* Root = nullptr;
        if (!Identifier.IsEmpty())
        {
            Root = Objects.FindRef(Identifier);
            if (!Root)
            {
                Error = TEXT("Missing configuration root object: ") + Identifier;
                return false;
            }
        }
        Record->TryGetStringField(TEXT("PropertyName"), PropertyName);
        Roots.Add(Entry.Key, Root);
        RootPropertyNames.Add(Entry.Key, PropertyName);
        FString DestinationMemberName;
        if (!Record->TryGetStringField(TEXT("DestinationMemberName"), DestinationMemberName))
        {
            if (const FString* ExistingName = Values.MemberNames.Find(Entry.Key))
            {
                DestinationMemberName = *ExistingName;
            }
            else
            {
                const int32 Separator = Entry.Key.Find(TEXT(":"));
                if (Separator != INDEX_NONE)
                {
                    UClass* ContextClass = LoadObject<UClass>(nullptr, *Entry.Key.Left(Separator));
                    const FString MemberIdentifier = Entry.Key.Mid(Separator);
                    TSet<FString> CandidateNames;
                    for (const TPair<FString, FString>& Member : Values.MemberNames)
                    {
                        if (!Member.Key.EndsWith(MemberIdentifier))
                        {
                            continue;
                        }
                        UClass* DeclaringClass = LoadObject<UClass>(nullptr, *Member.Key.LeftChop(MemberIdentifier.Len()));
                        UClass* NativeDeclaringClass = Cast<UClass>(Values.TypeMappings.FindRef(DeclaringClass));
                        if (ContextClass && DeclaringClass &&
                            (ContextClass->IsChildOf(DeclaringClass) || (NativeDeclaringClass && ContextClass->IsChildOf(NativeDeclaringClass))))
                        {
                            CandidateNames.Add(Member.Value);
                        }
                    }
                    if (CandidateNames.Num() == 1)
                    {
                        DestinationMemberName = *CandidateNames.CreateConstIterator();
                    }
                }
            }
        }
        if (DestinationMemberName.IsEmpty())
        {
            Error = TEXT("Cannot resolve the configuration destination member: ") + Entry.Key;
            return false;
        }
        RootMemberNames.Add(Entry.Key, DestinationMemberName);

    }
    return true;
}

bool UNativizationV2Subsystem::ApplyMigrationTypes(const TMap<UField*, UField*>& TypeMappings,
    const TMap<FString, FString>& MemberNames, FString& Error, bool bPreviewOnly,
    const TMap<UClass*, UClass*>& DefaultOnlyClassReplacements, bool bContinueOnError, bool bKeepAssetReferencesInBlueprint,
    const TSharedPtr<FJsonObject>& ConfigurationTemplates)
{
    if (!bKeepAssetReferencesInBlueprint)
    {
        Error = TEXT("Native asset defaults are no longer supported. Regenerate with serialized Blueprint overrides before migration.");
        return false;
    }
    FMigrationValues Values(TypeMappings, MemberNames, DefaultOnlyClassReplacements, bKeepAssetReferencesInBlueprint);
    TMap<FString, UObject*> ConfigurationRoots;
    TMap<FString, FString> ConfigurationPropertyNames;
    TMap<FString, FString> ConfigurationMemberNames;
    if (!HydrateConfigurationTemplates(ConfigurationTemplates, Values, ConfigurationRoots, ConfigurationPropertyNames, ConfigurationMemberNames, Error))
    {
        return false;
    }

    TArray<FString> MigrationErrors;
    TSet<FName> FailedPackages;
    auto RecordMigrationFailure = [&](const FString& AssetPath, const FString& Stage, const FString& Failure)
    {
        const FString Diagnostic = FString::Format(TEXT("Asset: {0}; stage: {1}; error: {2}"), {AssetPath, Stage, Failure});
        UE_LOG(LogTemp, Error, TEXT("%s"), *Diagnostic);
        MigrationErrors.Add(Diagnostic);
        FailedPackages.Add(FName(*FSoftObjectPath(AssetPath).GetLongPackageName()));
        Error.Reset();
        Values.Error.Reset();
    };
    TArray<FMigrationBlueprint> Blueprints;
    TArray<FMigrationTable> Tables;
    TArray<FMigrationInstance> Instances;
    TArray<FMigrationInstance> EmbeddedObjects;
    TArray<FMigrationWidget> Widgets;
    TMap<UField*, UField*> ReferenceMappings(TypeMappings);
    TSet<UClass*> DefaultOnlySourceClasses;
    TArray<TStrongObjectPtr<UClass>> SourceAliases;
    auto RegisterSourceAliases = [&](UBlueprint* Blueprint, UClass* NativeClass, bool bDefaultOnlyReplacement = false)
    {
        for (TObjectIterator<UClass> ClassIterator; ClassIterator; ++ClassIterator)
        {
            if (ClassIterator->ClassGeneratedBy == Blueprint)
            {
                SourceAliases.Emplace(*ClassIterator);
                ReferenceMappings.Add(*ClassIterator, NativeClass);
                Values.AddObjectMapping(*ClassIterator, NativeClass);
                Values.SoftMappings.Add(FSoftObjectPath(*ClassIterator), FSoftObjectPath(NativeClass));
                if (bDefaultOnlyReplacement)
                {
                    DefaultOnlySourceClasses.Add(*ClassIterator);
                }
            }
        }
    };
    for (const TPair<UClass*, UClass*>& Replacement : DefaultOnlyClassReplacements)
    {
        ReferenceMappings.Add(Replacement.Key, Replacement.Value);
        DefaultOnlySourceClasses.Add(Replacement.Key);
        RegisterSourceAliases(UBlueprint::GetBlueprintFromClass(Replacement.Key), Replacement.Value, true);
    }
    for (const TPair<UField*, UField*>& Mapping : TypeMappings)
    {
        if (UClass* SourceClass = Cast<UClass>(Mapping.Key))
        {
            if (UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(SourceClass))
            {
                RegisterSourceAliases(Blueprint, CastChecked<UClass>(Mapping.Value));
            }
        }
    }
    TArray<TStrongObjectPtr<UBlueprint>> DependentBlueprints;
    TMap<UBlueprint*, UObject*> DependentDefaults;
    TMap<UBlueprint*, TMap<FName, UObject*>> DependentInheritedComponents;
    TMap<UBlueprint*, UBlueprint*> DependentConfigurationParents;
    TSet<UBlueprint*> ConvertedBlueprints;
    TArray<UBlueprint*> ChangedBlueprints;
    for (const TPair<UField*, UField*>& Mapping : TypeMappings)
    {
        UClass* SourceClass = Cast<UClass>(Mapping.Key);
        UBlueprint* Blueprint = nullptr;
        if (SourceClass)
        {
            Blueprint = UBlueprint::GetBlueprintFromClass(SourceClass);
        }
        if (Blueprint && Values.IsProjectAsset(Blueprint))
        {
            ConvertedBlueprints.Add(Blueprint);
        }
        if (!Blueprint || !Values.IsProjectAsset(Blueprint) || SourceClass->HasAnyClassFlags(CLASS_Interface))
        {
            continue;
        }
        auto CaptureBlueprintDefaults = [&]() -> bool
        {
            FMigrationBlueprint Snapshot;
            Snapshot.Blueprint.Reset(Blueprint);
            Snapshot.NativeClass.Reset(CastChecked<UClass>(Mapping.Value));
            Snapshot.SourceClassPath = SourceClass->GetPathName();
            Snapshot.DefaultObject = SourceClass->GetDefaultObject();
            Values.CaptureObject(Snapshot.DefaultObject);
            TMap<FString, FString> SnapshotMemberNames(MemberNames);
            for (const TPair<FString, FString>& Member : ConfigurationMemberNames)
            {
                if (Member.Key.StartsWith(Snapshot.SourceClassPath + TEXT(":")))
                {
                    SnapshotMemberNames.Add(Member.Key, Member.Value);
                }
            }
            TMap<FString, UObject*> OwnerConfigurationRoots(ConfigurationRoots);
            TMap<FString, FString> OwnerConfigurationPropertyNames(ConfigurationPropertyNames);
            for (const TPair<FString, FString>& Member : SnapshotMemberNames)
            {
                const int32 Separator = Member.Key.Find(TEXT(":"));
                if (Separator != INDEX_NONE)
                {
                    const FString ContextKey = SourceClass->GetPathName() + Member.Key.Mid(Separator);
                    if (ConfigurationRoots.Contains(ContextKey))
                    {
                        OwnerConfigurationRoots.Add(Member.Key, ConfigurationRoots.FindChecked(ContextKey));
                        OwnerConfigurationPropertyNames.Add(Member.Key, ConfigurationPropertyNames.FindRef(ContextKey));
                    }
                }
            }
            if (bKeepAssetReferencesInBlueprint)
            {
                for (UClass* TemplateOwnerClass = SourceClass; TemplateOwnerClass; TemplateOwnerClass = TemplateOwnerClass->GetSuperClass())
                {
                    const FString TemplatePrefix = TemplateOwnerClass->GetPathName() + TEXT(":InstancePropertyTemplate:");
                    for (const TPair<FString, FString>& Member : SnapshotMemberNames)
                    {
                        if (!Member.Key.StartsWith(TemplatePrefix))
                        {
                            continue;
                        }
                        const FString PropertyPath = Member.Key.RightChop(TemplatePrefix.Len());
                        FObjectPropertyBase* SourceProperty = nullptr;
                        for (TFieldIterator<FProperty> PropertyIterator(SourceClass); PropertyIterator; ++PropertyIterator)
                        {
                            if (PropertyIterator->GetPathName() == PropertyPath)
                            {
                                SourceProperty = CastField<FObjectPropertyBase>(*PropertyIterator);
                                break;
                            }
                        }
                        const bool bUseConfiguration = OwnerConfigurationRoots.Contains(Member.Key) &&
                            (!SourceProperty || SourceClass->IsChildOf(Snapshot.NativeClass.Get()));
                        if (!SourceProperty && !bUseConfiguration)
                        {
                            Error = TEXT("Migration cannot resolve the required instance template property: ") + PropertyPath;
                            return false;
                        }
                        UObject* Template = nullptr;
                        FString PropertyName;
                        if (bUseConfiguration)
                        {
                            Template = OwnerConfigurationRoots.FindChecked(Member.Key);
                            PropertyName = OwnerConfigurationPropertyNames.FindRef(Member.Key);
                        }
                        else
                        {
                            Template = SourceProperty->GetObjectPropertyValue_InContainer(Snapshot.DefaultObject);
                            PropertyName = Values.GetMemberName(SourceProperty);
                        }
                        for (FMigrationValue& PropertyValue : Values.Objects.FindChecked(Snapshot.DefaultObject).Properties)
                        {
                            if (PropertyValue.Name == PropertyName && PropertyValue.Kind == EMigrationValueKind::Object)
                            {
                                PropertyValue.Object.Reset();
                                PropertyValue.SourcePath.Reset();
                            }
                        }
                        FMigrationValue TemplateValue;
                        TemplateValue.Kind = EMigrationValueKind::Object;
                        TemplateValue.Name = Member.Value;
                        if (Template)
                        {
                            Values.CaptureObject(Template);
                            Snapshot.InstanceTemplates.Add(FName(*Member.Value), Template);
                            TemplateValue.Object.Reset(Template);
                            TemplateValue.SourcePath = Template->GetPathName();
                        }
                        Values.Objects.FindChecked(Snapshot.DefaultObject).Properties.Add(MoveTemp(TemplateValue));
                    }
                }
            }
            TArray<FName> ComponentNames;
            const TArray<UActorComponent*> Components = UBlueprintNativizationLibrary::GetClassComponentTemplates(
                ComponentNames, SourceClass, UActorComponent::StaticClass(), true);
            if (UBlueprintGeneratedClass* BlueprintClass = Cast<UBlueprintGeneratedClass>(SourceClass))
            {
                if (BlueprintClass->SimpleConstructionScript)
                {
                    USCS_Node* RootNode = nullptr;
                    BlueprintClass->SimpleConstructionScript->GetSceneRootComponentTemplate(true, &RootNode);
                    if (RootNode && UBlueprintNativizationLibrary::IsRootComponentTemplate(RootNode->ComponentTemplate) &&
                        !ComponentNames.Contains(RootNode->GetVariableName()))
                    {
                        FName RootMemberName = RootNode->GetVariableName();
                        if (FProperty* SourceProperty = FindFProperty<FProperty>(SourceClass, RootMemberName))
                        {
                            RootMemberName = FName(*Values.GetMemberName(SourceProperty));
                        }
                        AActor* NativeDefaults = Cast<AActor>(Snapshot.NativeClass->GetDefaultObject());
                        FObjectPropertyBase* RootProperty = FindFProperty<FObjectPropertyBase>(Snapshot.NativeClass.Get(), RootMemberName);
                        if (!NativeDefaults || !NativeDefaults->GetRootComponent() || !RootProperty ||
                            RootProperty->GetObjectPropertyValue_InContainer(NativeDefaults) != NativeDefaults->GetRootComponent())
                        {
                            Error = TEXT("Migration cannot match the inherited SCS root to the generated constructor root: ") +
                                SourceClass->GetPathName() + TEXT(":") + RootMemberName.ToString();
                            return false;
                        }
                        // A shared parent SCS template cannot have a global mapping to several child CDO components.
                        Values.Objects.FindChecked(Snapshot.DefaultObject).Properties.RemoveAll([](const FMigrationValue& Property)
                        {
                            return Property.Name == TEXT("RootComponent") && Property.Kind == EMigrationValueKind::Object &&
                                !Property.Object.IsValid();
                        });
                    }
                }
            }
            for (UClass* TemplateOwnerClass = SourceClass; TemplateOwnerClass; TemplateOwnerClass = TemplateOwnerClass->GetSuperClass())
            {
                const FString TemplatePrefix = TemplateOwnerClass->GetPathName() + TEXT(":ComponentInstanceTemplate:");
                for (const TPair<FString, FString>& Member : SnapshotMemberNames)
                {
                    if (!Member.Key.StartsWith(TemplatePrefix))
                    {
                        continue;
                    }
                    const FString Identifier = Member.Key.RightChop(TemplatePrefix.Len());
                    const int32 PropertySeparator = Identifier.Find(TEXT(":"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
                    UActorComponent* Component = nullptr;
                    if (PropertySeparator != INDEX_NONE)
                    {
                        Component = LoadObject<UActorComponent>(nullptr, *Identifier.Left(PropertySeparator));
                    }
                    const bool bUseConfiguration = OwnerConfigurationRoots.Contains(Member.Key) &&
                        (!Component || SourceClass->IsChildOf(Snapshot.NativeClass.Get()));
                    UObject* Template = nullptr;
                    if (bUseConfiguration)
                    {
                        Template = OwnerConfigurationRoots.FindChecked(Member.Key);
                    }
                    else
                    {
                        if (!Component)
                        {
                            Error = TEXT("Migration cannot resolve the component instance configuration: ") + Member.Key;
                            return false;
                        }
                        for (UActorComponent* CurrentComponent : Components)
                        {
                            if (CurrentComponent->GetFName() == Component->GetFName())
                            {
                                Component = CurrentComponent;
                                break;
                            }
                        }
                        FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Component->GetClass(),
                            FName(*Identifier.RightChop(PropertySeparator + 1)));
                        if (!Property)
                        {
                            Error = TEXT("Migration cannot resolve the component instance configuration property: ") + Member.Key;
                            return false;
                        }
                        Template = Property->GetObjectPropertyValue_InContainer(Component);
                    }
                    FMigrationValue TemplateValue;
                    TemplateValue.Kind = EMigrationValueKind::Object;
                    TemplateValue.Name = Member.Value;
                    if (Template)
                    {
                        Values.CaptureObject(Template);
                        Snapshot.InstanceTemplates.Add(FName(*Member.Value), Template);
                        TemplateValue.Object.Reset(Template);
                        TemplateValue.SourcePath = Template->GetPathName();
                    }
                    Values.Objects.FindChecked(Snapshot.DefaultObject).Properties.Add(MoveTemp(TemplateValue));
                }
            }
            for (UClass* TemplateOwnerClass = SourceClass; TemplateOwnerClass; TemplateOwnerClass = TemplateOwnerClass->GetSuperClass())
            {
                const FString RuntimeTemplatePrefix = TemplateOwnerClass->GetPathName() + TEXT(":RuntimeComponentTemplate:");
                for (const TPair<FString, FString>& Member : SnapshotMemberNames)
                {
                    if (!Member.Key.StartsWith(RuntimeTemplatePrefix))
                    {
                        continue;
                    }
                    const FString TemplatePath = Member.Key.RightChop(RuntimeTemplatePrefix.Len());
                    UActorComponent* OriginalTemplate = LoadObject<UActorComponent>(nullptr, *TemplatePath);
                    const bool bUseConfiguration = OwnerConfigurationRoots.Contains(Member.Key) &&
                        (!OriginalTemplate || SourceClass->IsChildOf(Snapshot.NativeClass.Get()));
                    if (bUseConfiguration)
                    {
                        OriginalTemplate = Cast<UActorComponent>(OwnerConfigurationRoots.FindChecked(Member.Key));
                        const FString ComponentMember = OwnerConfigurationPropertyNames.FindRef(Member.Key);
                        if (!ComponentMember.IsEmpty())
                        {
                            Snapshot.RuntimeComponentMembers.Add(FName(*ComponentMember));
                        }
                    }
                    if (!OriginalTemplate)
                    {
                        Error = TEXT("Migration cannot resolve the required runtime component template: ") + TemplatePath;
                        return false;
                    }
                    UActorComponent* Template = OriginalTemplate;
                    if (!bUseConfiguration)
                    {
                        if (USCS_Node* ComponentNode = UBlueprintNativizationLibrary::FindSCSNodeForComponent(OriginalTemplate))
                        {
                            FName ComponentName = ComponentNode->GetVariableName();
                            if (FProperty* ComponentProperty = FindFProperty<FProperty>(SourceClass, ComponentName))
                            {
                                ComponentName = FName(*Values.GetMemberName(ComponentProperty));
                            }
                            Snapshot.RuntimeComponentMembers.Add(ComponentName);
                            Template = ComponentNode->GetActualComponentTemplate(CastChecked<UBlueprintGeneratedClass>(SourceClass));
                        }
                    }
                    for (int32 ComponentIndex = 0; !bUseConfiguration && ComponentIndex < Components.Num(); ++ComponentIndex)
                    {
                        if (Components[ComponentIndex]->GetFName() == OriginalTemplate->GetFName())
                        {
                            Template = Components[ComponentIndex];
                            FName ComponentName = ComponentNames[ComponentIndex];
                            if (FProperty* ComponentProperty = FindFProperty<FProperty>(SourceClass, ComponentName))
                            {
                                ComponentName = FName(*Values.GetMemberName(ComponentProperty));
                            }
                            Snapshot.RuntimeComponentMembers.Add(ComponentName);
                            break;
                        }
                    }
                    const FName MemberName(*Member.Value);
                    Snapshot.Components.Add(MemberName, Template);
                    Snapshot.ConstructionTemplateMembers.Add(MemberName);
                    Snapshot.ConstructionTemplateNames.Add(MemberName, FName(*Member.Value));
                    Values.CaptureObject(Template);
                    FMigrationValue Value;
                    Value.Kind = EMigrationValueKind::Object;
                    Value.Name = Member.Value;
                    Value.SourcePath = Template->GetPathName();
                    Value.Object.Reset(Template);
                    Values.Objects.FindChecked(Snapshot.DefaultObject).Properties.Add(MoveTemp(Value));
                }
            }
            for (int32 Index = 0; Index < Components.Num(); ++Index)
            {
                FName ComponentName = ComponentNames[Index];
                if (FProperty* ComponentProperty = FindFProperty<FProperty>(SourceClass, ComponentName))
                {
                    ComponentName = FName(*Values.GetMemberName(ComponentProperty));
                }
                if (Snapshot.RuntimeComponentMembers.Contains(ComponentName))
                {
                    continue;
                }
                Snapshot.Components.Add(ComponentName, Components[Index]);
                Values.CaptureObject(Components[Index]);
                for (FMigrationValue& Property : Values.Objects.FindChecked(Snapshot.DefaultObject).Properties)
                {
                    if ((Property.Name == ComponentName.ToString() || (Property.Name == TEXT("RootComponent") &&
                        UBlueprintNativizationLibrary::IsRootComponentTemplate(Components[Index]))) && Property.Kind == EMigrationValueKind::Object)
                    {
                        Property.Object.Reset(Components[Index]);
                    }
                }
            }
            for (UClass* TemplateOwnerClass = SourceClass; TemplateOwnerClass; TemplateOwnerClass = TemplateOwnerClass->GetSuperClass())
            {
                UBlueprintGeneratedClass* TemplateOwner = Cast<UBlueprintGeneratedClass>(TemplateOwnerClass);
                if (!TemplateOwner)
                {
                    continue;
                }
                const FString ComponentTemplatePrefix = TemplateOwnerClass->GetPathName() + TEXT(":ComponentTemplate:");
                for (const TPair<FString, FString>& Member : SnapshotMemberNames)
                {
                    if (!Member.Key.StartsWith(ComponentTemplatePrefix))
                    {
                        continue;
                    }
                    const FName TemplateName(*Member.Key.RightChop(ComponentTemplatePrefix.Len()));
                    UActorComponent* Template = TemplateOwner->FindComponentTemplateByName(TemplateName);
                    const bool bUseConfiguration = OwnerConfigurationRoots.Contains(Member.Key) &&
                        (!Template || SourceClass->IsChildOf(Snapshot.NativeClass.Get()));
                    if (bUseConfiguration)
                    {
                        Template = Cast<UActorComponent>(OwnerConfigurationRoots.FindChecked(Member.Key));
                    }
                    if (!Template)
                    {
                        Error = TEXT("Migration cannot resolve construction component template: ") + Member.Key;
                        return false;
                    }
                    for (UClass* OverrideClass = SourceClass; !bUseConfiguration && OverrideClass && OverrideClass != TemplateOwnerClass;
                        OverrideClass = OverrideClass->GetSuperClass())
                    {
                        UBlueprintGeneratedClass* OverrideOwner = Cast<UBlueprintGeneratedClass>(OverrideClass);
                        UInheritableComponentHandler* Overrides = nullptr;
                        if (OverrideOwner)
                        {
                            Overrides = OverrideOwner->GetInheritableComponentHandler();
                        }
                        if (Overrides)
                        {
                            const FComponentKey Key = Overrides->FindKey(TemplateName);
                            if (UActorComponent* Override = Overrides->GetOverridenComponentTemplate(Key))
                            {
                                Template = Override;
                                break;
                            }
                        }
                    }
                    const FName MemberName(*Member.Value);
                    Snapshot.Components.Add(MemberName, Template);
                    Snapshot.ConstructionTemplateMembers.Add(MemberName);
                    Snapshot.ConstructionTemplateNames.Add(MemberName, TemplateName);
                    Values.CaptureObject(Template);
                    FMigrationValue Value;
                    Value.Kind = EMigrationValueKind::Object;
                    Value.Name = Member.Value;
                    Value.SourcePath = Template->GetPathName();
                    Value.Object.Reset(Template);
                    Values.Objects.FindChecked(Snapshot.DefaultObject).Properties.Add(MoveTemp(Value));
                }
            }
            TArray<FName> InheritedComponentNames;
            const TArray<UActorComponent*> AllComponents = UBlueprintNativizationLibrary::GetClassComponentTemplates(
                InheritedComponentNames, SourceClass, UActorComponent::StaticClass(), false);
            for (int32 ComponentIndex = 0; ComponentIndex < AllComponents.Num(); ++ComponentIndex)
            {
                FName ComponentName = InheritedComponentNames[ComponentIndex];
                if (FProperty* ComponentProperty = FindFProperty<FProperty>(SourceClass, ComponentName))
                {
                    ComponentName = FName(*Values.GetMemberName(ComponentProperty));
                }
                if (Snapshot.Components.Contains(ComponentName) || Snapshot.RuntimeComponentMembers.Contains(ComponentName))
                {
                    continue;
                }
                FObjectPropertyBase* DestinationProperty = FindFProperty<FObjectPropertyBase>(Snapshot.NativeClass.Get(), ComponentName);
                if (!DestinationProperty || !DestinationProperty->GetObjectPropertyValue_InContainer(Snapshot.NativeClass->GetDefaultObject()))
                {
                    Error = TEXT("Migration cannot resolve the inherited constructor component: ") +
                        SourceClass->GetPathName() + TEXT(":") + ComponentName.ToString();
                    return false;
                }
                UObject* Component = AllComponents[ComponentIndex];
                Snapshot.InheritedComponents.Add(ComponentName, Component);
                Values.CaptureObject(Component);
                for (FMigrationValue& Property : Values.Objects.FindChecked(Snapshot.DefaultObject).Properties)
                {
                    if (Property.Name == ComponentName.ToString() && Property.Kind == EMigrationValueKind::Object)
                    {
                        Property.Object.Reset(Component);
                    }
                }
            }
            TArray<UEdGraph*> Graphs;
            Blueprint->GetAllGraphs(Graphs);
            for (UEdGraph* Graph : Graphs)
            {
                for (UEdGraphNode* Node : Graph->Nodes)
                {
                    UK2Node_EnhancedInputAction* Input = Cast<UK2Node_EnhancedInputAction>(Node);
                    if (Input && Input->InputAction)
                    {
                        const FString* Name = MemberNames.Find(SourceClass->GetPathName() + TEXT(":InputAction:") + Input->NodeGuid.ToString());
                        if (Name)
                        {
                            FMigrationValue Value;
                            Value.Kind = EMigrationValueKind::Object;
                            Value.Name = *Name;
                            Value.SourcePath = Input->GetPathName();
                            Value.Object.Reset(const_cast<UInputAction*>(Input->InputAction.Get()));
                            Values.Objects.FindChecked(Snapshot.DefaultObject).Properties.Add(MoveTemp(Value));
                        }
                    }
                }
            }
            for (UClass* AssetOwnerClass = SourceClass; AssetOwnerClass; AssetOwnerClass = AssetOwnerClass->GetSuperClass())
            {
                const FString AssetClassPrefix = AssetOwnerClass->GetPathName() + TEXT(":AssetClass:");
                const FString AssetObjectPrefix = AssetOwnerClass->GetPathName() + TEXT(":AssetObject:");
                for (const TPair<FString, FString>& Member : SnapshotMemberNames)
                {
                    FString AssetPath;
                    UObject* Asset = nullptr;
                    if (Member.Key.StartsWith(AssetClassPrefix))
                    {
                        AssetPath = Member.Key.RightChop(AssetClassPrefix.Len());
                        Asset = LoadObject<UClass>(nullptr, *AssetPath);
                    }
                    else if (Member.Key.StartsWith(AssetObjectPrefix))
                    {
                        AssetPath = Member.Key.RightChop(AssetObjectPrefix.Len());
                        Asset = LoadObject<UObject>(nullptr, *AssetPath);
                    }
                    else
                    {
                        continue;
                    }
                    if (!Asset)
                    {
                        Error = TEXT("Migration cannot resolve the required asset: ") + AssetPath;
                        return false;
                    }
                    FMigrationValue Value;
                    Value.Kind = EMigrationValueKind::Object;
                    Value.Name = Member.Value;
                    Value.SourcePath = AssetPath;
                    Value.Object.Reset(Asset);
                    if (UClass* Class = Cast<UClass>(Asset))
                    {
                        Value.BlueprintClass.Reset(Cast<UBlueprint>(Class->ClassGeneratedBy));
                    }
                    Values.Objects.FindChecked(Snapshot.DefaultObject).Properties.Add(MoveTemp(Value));
                }
            }
            if (!Values.ValidateStructure(Snapshot.NativeClass.Get(), Values.Objects.FindChecked(Snapshot.DefaultObject).Properties))
            {
                Error = Values.Error + TEXT(" No assets were migrated.");
                return false;
            }
            ConvertedBlueprints.Add(Blueprint);
            Blueprints.Add(MoveTemp(Snapshot));
            return true;
        };
        if (!CaptureBlueprintDefaults())
        {
            if (!bContinueOnError)
            {
                return false;
            }
            RecordMigrationFailure(Blueprint->GetPathName(), TEXT("Blueprint preflight defaults"), Error);
        }
    }

    FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    TArray<FName> Packages;
    for (const TPair<UField*, UField*>& Mapping : ReferenceMappings)
    {
        Packages.AddUnique(Mapping.Key->GetOutermost()->GetFName());
    }
    for (int32 Index = 0; Index < Packages.Num(); ++Index)
    {
        TArray<FName> Referencers;
        Registry.Get().GetReferencers(Packages[Index], Referencers);
        for (FName Package : Referencers)
        {
            Packages.AddUnique(Package);
        }
    }
    FARFilter Filter;
    Filter.PackageNames = Packages;
    TArray<FAssetData> Assets;
    Registry.Get().GetAssets(Filter, Assets);
    for (TObjectIterator<UObject> Object; Object; ++Object)
    {
        if (Object->IsAsset() && Values.IsProjectAsset(*Object))
        {
            Assets.AddUnique(FAssetData(*Object));
        }
    }
    TArray<TStrongObjectPtr<UObject>> LoadedAssets;
    for (const FAssetData& Asset : Assets)
    {
        FString Filename;
        if (!FPackageName::TryConvertLongPackageNameToFilename(Asset.PackageName.ToString(), Filename) ||
            !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Filename), FPaths::ConvertRelativePathToFull(FPaths::ProjectDir())))
        {
            continue;
        }
        UObject* Object = Asset.GetAsset();
        if (!Object)
        {
            Error = TEXT("Migration could not load asset: ") + Asset.GetSoftObjectPath().ToString();
            if (!bContinueOnError)
            {
                return false;
            }
            RecordMigrationFailure(Asset.GetSoftObjectPath().ToString(), TEXT("Load asset"), Error);
            continue;
        }
        LoadedAssets.Emplace(Object);
        if (Object->IsA<UBlueprint>())
        {
            TArray<UObject*> Subobjects;
            GetObjectsWithOuter(Object, Subobjects, EGetObjectsFlags::IncludeNestedObjects);
            for (UObject* Subobject : Subobjects)
            {
                UWidget* Widget = Cast<UWidget>(Subobject);
                if (!Widget)
                {
                    continue;
                }
                FMigrationWidget Snapshot;
                Snapshot.Path = Widget->GetPathName();
                Snapshot.Source.Reset(Widget);
                for (TFieldIterator<FProperty> Property(Widget->GetClass()); Property; ++Property)
                {
                    UEnum* Enumeration = nullptr;
                    if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(*Property))
                    {
                        Enumeration = EnumProperty->GetEnum();
                    }
                    else if (FByteProperty* ByteProperty = CastField<FByteProperty>(*Property))
                    {
                        Enumeration = ByteProperty->Enum;
                    }
                    if (TypeMappings.Contains(Enumeration))
                    {
                        FMigrationValue Value = Values.CaptureValue(*Property, Property->ContainerPtrToValuePtr<void>(Widget));
                        Value.Name = Values.GetMemberName(*Property);
                        Snapshot.Properties.Add(MoveTemp(Value));
                    }
                }
                if (!Snapshot.Properties.IsEmpty())
                {
                    Widgets.Add(MoveTemp(Snapshot));
                }
            }
        }
        auto CaptureAssetValues = [&]() -> bool
        {
            if (UBlueprint* Blueprint = Cast<UBlueprint>(Object))
            {
                if (!ConvertedBlueprints.Contains(Blueprint) && Blueprint->GeneratedClass)
                {
                    DependentBlueprints.Emplace(Blueprint);
                    UObject* DefaultObject = Blueprint->GeneratedClass->GetDefaultObject();
                    DependentDefaults.Add(Blueprint, DefaultObject);
                    Values.CaptureObject(DefaultObject);
                    UClass* SourceClass = Blueprint->GeneratedClass;
                    for (UClass* ParentClass = SourceClass->GetSuperClass(); ParentClass; ParentClass = ParentClass->GetSuperClass())
                    {
                        UBlueprint* ParentBlueprint = UBlueprint::GetBlueprintFromClass(ParentClass);
                        if (ParentBlueprint && ConvertedBlueprints.Contains(ParentBlueprint))
                        {
                            DependentConfigurationParents.Add(Blueprint, ParentBlueprint);
                            break;
                        }
                    }
                    if (SourceClass->IsChildOf(AActor::StaticClass()))
                    {
                        TArray<FName> ComponentNames;
                        const TArray<UActorComponent*> Components = UBlueprintNativizationLibrary::GetClassComponentTemplates(
                            ComponentNames, SourceClass, UActorComponent::StaticClass(), false);
                        for (int32 ComponentIndex = 0; ComponentIndex < Components.Num(); ++ComponentIndex)
                        {
                            FObjectPropertyBase* SourceProperty = FindFProperty<FObjectPropertyBase>(SourceClass, ComponentNames[ComponentIndex]);
                            UClass* NativeOwner = nullptr;
                            if (SourceProperty)
                            {
                                NativeOwner = Cast<UClass>(TypeMappings.FindRef(SourceProperty->GetOwnerClass()));
                            }
                            if (!NativeOwner)
                            {
                                continue;
                            }
                            const FName ComponentName(*Values.GetMemberName(SourceProperty));
                            FObjectPropertyBase* NativeProperty = FindFProperty<FObjectPropertyBase>(NativeOwner, ComponentName);
                            if (!NativeProperty || !NativeProperty->GetObjectPropertyValue_InContainer(NativeOwner->GetDefaultObject()))
                            {
                                continue;
                            }
                            UObject* Component = Components[ComponentIndex];
                            Values.CaptureObject(Component);
                            DependentInheritedComponents.FindOrAdd(Blueprint).Add(ComponentName, Component);
                            for (FMigrationValue& Property : Values.Objects.FindChecked(DefaultObject).Properties)
                            {
                                if (Property.Name == ComponentName.ToString() && Property.Kind == EMigrationValueKind::Object)
                                {
                                    Property.Object.Reset(Component);
                                    Property.SourcePath = Component->GetPathName();
                                }
                            }
                        }
                    }
                }
            }
            if (Object && !Object->IsA<UBlueprint>() && !Object->IsA<UField>())
            {
                TArray<UObject*> Subobjects;
                GetObjectsWithOuter(Object, Subobjects, EGetObjectsFlags::IncludeNestedObjects);
                for (UObject* Subobject : Subobjects)
                {
                    if (!IsValid(Subobject))
                    {
                        continue;
                    }
                    if (AActor* Actor = Cast<AActor>(Subobject))
                    {
                        if (Actor->IsTemplate() || Actor->IsActorBeingDestroyed() || !Actor->GetLevel() || !Actor->GetLevel()->Actors.Contains(Actor))
                        {
                            continue;
                        }
                        if (UChildActorComponent* ParentComponent = Actor->GetParentComponent())
                        {
                            if (ParentComponent->GetChildActor() != Actor)
                            {
                                continue;
                            }
                        }
                    }
                    if (UActorComponent* Component = Cast<UActorComponent>(Subobject))
                    {
                        AActor* Owner = Component->GetOwner();
                        if (!IsValid(Component) || Component->IsBeingDestroyed() || Component->IsVisualizationComponent() || IsMigrationRecoveryRoot(Component) ||
                            (Owner && (Owner->IsActorBeingDestroyed() || !Owner->GetComponents().Contains(Component))))
                        {
                            continue;
                        }
                    }
                    bool bInheritedConvertedClass = false;
                    for (UClass* ParentClass = Subobject->GetClass()->GetSuperClass(); ParentClass; ParentClass = ParentClass->GetSuperClass())
                    {
                        if (TypeMappings.Contains(ParentClass))
                        {
                            bInheritedConvertedClass = true;
                            break;
                        }
                    }
                    if (TypeMappings.Contains(Subobject->GetClass()) || bInheritedConvertedClass ||
                        (DefaultOnlyClassReplacements.Contains(Subobject->GetClass()) && Subobject->HasAnyFlags(RF_WasLoaded)))
                    {
                        Values.CaptureObject(Subobject);
                        UClass* NativeClass = Values.Objects.FindChecked(Subobject).DestinationClass.Get();
                        if ((!bInheritedConvertedClass || TypeMappings.Contains(Subobject->GetClass())) &&
                            !Values.ValidateStructure(NativeClass, Values.Objects.FindChecked(Subobject).Properties))
                        {
                            Error = Values.Error + TEXT(" No assets were migrated.");
                            return false;
                        }
                        if (AActor* Actor = Cast<AActor>(Subobject))
                        {
                            FMigrationInstance Instance;
                            Instance.Path = Subobject->GetPathName();
                            Instance.Source.Reset(Subobject);
                            for (AActor* ChildActor = Actor; ChildActor->GetParentComponent();)
                            {
                                UChildActorComponent* ParentComponent = ChildActor->GetParentComponent();
                                AActor* ParentActor = ParentComponent->GetOwner();
                                if (!IsValid(ParentActor) || ParentActor->IsTemplate() || ParentActor->IsActorBeingDestroyed())
                                {
                                    break;
                                }
                                Values.CaptureObject(ParentActor);
                                Values.CaptureObject(ParentComponent);
                                FMigrationChildActorOwner Parent;
                                Parent.Actor.Reset(ParentActor);
                                Parent.ActorPath = ParentActor->GetPathName();
                                Parent.Component.Reset(ParentComponent);
                                Parent.ComponentName = ParentComponent->GetFName();
                                for (TFieldIterator<FObjectPropertyBase> ComponentProperty(ParentActor->GetClass()); ComponentProperty; ++ComponentProperty)
                                {
                                    if (ComponentProperty->GetObjectPropertyValue_InContainer(ParentActor) == ParentComponent)
                                    {
                                        Parent.ComponentMember = FName(*Values.GetMemberName(*ComponentProperty));
                                        break;
                                    }
                                }
                                Instance.ChildActorOwners.Add(MoveTemp(Parent));
                                ChildActor = ParentActor;
                            }
                            Instances.Add(MoveTemp(Instance));
                            for (UActorComponent* Component : Actor->GetComponents())
                            {
                                if (IsValid(Component) && !Component->IsBeingDestroyed() && !Component->IsVisualizationComponent() && !IsMigrationRecoveryRoot(Component))
                                {
                                    Values.CaptureObject(Component);
                                    FMigrationInstance ComponentInstance;
                                    ComponentInstance.Path = Component->GetPathName();
                                    ComponentInstance.Source.Reset(Component);
                                    ComponentInstance.Owner.Reset(Actor);
                                    ComponentInstance.OwnerPath = Actor->GetPathName();
                                    ComponentInstance.ComponentName = Component->GetFName();
                                    ComponentInstance.bChildActorComponentOwner = Actor->GetParentComponent() != nullptr;
                                    for (UActorComponent* OuterComponent = Cast<UActorComponent>(Component->GetOuter()); OuterComponent;
                                        OuterComponent = Cast<UActorComponent>(OuterComponent->GetOuter()))
                                    {
                                        Values.CaptureObject(OuterComponent);
                                        ComponentInstance.ComponentOuters.Emplace(OuterComponent);
                                        ComponentInstance.ComponentOuterNames.Add(OuterComponent->GetFName());
                                        for (TFieldIterator<FObjectPropertyBase> OuterProperty(Actor->GetClass()); OuterProperty; ++OuterProperty)
                                        {
                                            if (OuterProperty->GetObjectPropertyValue_InContainer(Actor) == OuterComponent)
                                            {
                                                ComponentInstance.ComponentOuterMember = FName(*Values.GetMemberName(*OuterProperty));
                                                break;
                                            }
                                        }
                                    }
                                    for (TFieldIterator<FObjectPropertyBase> ComponentProperty(Subobject->GetClass()); ComponentProperty; ++ComponentProperty)
                                    {
                                        if (ComponentProperty->GetObjectPropertyValue_InContainer(Subobject) == Component)
                                        {
                                            ComponentInstance.ComponentMember = FName(*Values.GetMemberName(*ComponentProperty));
                                            break;
                                        }
                                    }
                                    if (Values.Objects.FindChecked(Component).DestinationClass.Get() == Component->GetClass())
                                    {
                                        ComponentInstance.ComponentData = Component->GetComponentInstanceData();
                                    }
                                    if (Component->CreationMethod == EComponentCreationMethod::UserConstructionScript)
                                    {
                                        ComponentInstance.bConstructionComponent = true;
                                        ComponentInstance.ComponentSerializationIndex = Component->GetUCSSerializationIndex();
                                        ComponentInstance.ComponentTemplateName = Component->GetArchetype()->GetFName();
                                        for (UActorComponent* OtherComponent : Actor->GetComponents())
                                        {
                                            if (IsValid(OtherComponent) && !OtherComponent->IsBeingDestroyed() &&
                                                OtherComponent->CreationMethod == EComponentCreationMethod::UserConstructionScript &&
                                                OtherComponent->GetArchetype() == Component->GetArchetype() &&
                                                OtherComponent->GetFName().GetNumber() < Component->GetFName().GetNumber())
                                            {
                                                ++ComponentInstance.ComponentTemplateOrdinal;
                                            }
                                        }
                                        for (UClass* OwnerClass = Actor->GetClass(); OwnerClass; OwnerClass = OwnerClass->GetSuperClass())
                                        {
                                            const FString TemplateKey = OwnerClass->GetPathName() + TEXT(":ComponentTemplate:") + ComponentInstance.ComponentTemplateName.ToString();
                                            if (const FString* Member = MemberNames.Find(TemplateKey))
                                            {
                                                ComponentInstance.ComponentTemplateMember = FName(**Member);
                                                break;
                                            }
                                        }
                                        if (Values.Objects.FindChecked(Component).DestinationClass.Get() != Component->GetClass())
                                        {
                                            TSet<const FProperty*> ModifiedProperties;
                                            Component->GetUCSModifiedProperties(ModifiedProperties);
                                            TSet<FString> ModifiedNames;
                                            for (const FProperty* ModifiedProperty : ModifiedProperties)
                                            {
                                                ModifiedNames.Add(Values.GetMemberName(const_cast<FProperty*>(ModifiedProperty)));
                                            }
                                            Values.Objects.FindChecked(Component).Properties.RemoveAll([&ModifiedNames](const FMigrationValue& Property)
                                            {
                                                return ModifiedNames.Contains(Property.Name);
                                            });
                                        }
                                    }
                                    Instances.Add(MoveTemp(ComponentInstance));
                                }
                            }
                        }
                        else
                        {
                            FMigrationInstance Instance;
                            Instance.Path = Subobject->GetPathName();
                            Instance.Source.Reset(Subobject);
                            UActorComponent* Component = Cast<UActorComponent>(Subobject);
                            AActor* Owner = nullptr;
                            if (Component)
                            {
                                Owner = Component->GetOwner();
                            }
                            if (Owner && Owner->GetLevel())
                            {
                                Values.CaptureObject(Owner);
                                Instance.Owner.Reset(Owner);
                                Instance.OwnerPath = Owner->GetPathName();
                                Instance.ComponentName = Component->GetFName();
                                for (TFieldIterator<FObjectPropertyBase> Property(Owner->GetClass()); Property; ++Property)
                                {
                                    if (Property->GetObjectPropertyValue_InContainer(Owner) == Component)
                                    {
                                        Instance.ComponentMember = FName(*Values.GetMemberName(*Property));
                                        break;
                                    }
                                }
                                Instances.Add(MoveTemp(Instance));
                            }
                            else
                            {
                                EmbeddedObjects.Add(MoveTemp(Instance));
                            }
                        }
                    }
                }
            }
            if (UDataTable* Table = Cast<UDataTable>(Object))
            {
                FMigrationTable Snapshot;
                Snapshot.Table.Reset(Table);
                Snapshot.NativeStructure.Reset(Values.ResolveStructure(const_cast<UScriptStruct*>(Table->GetRowStruct())));
                for (const TPair<FName, uint8*>& Row : Table->GetRowMap())
                {
                    TArray<FMigrationValue> RowValues = Values.CaptureStructure(const_cast<UScriptStruct*>(Table->GetRowStruct()), Row.Value);
                    if (!Values.ValidateStructure(Snapshot.NativeStructure.Get(), RowValues))
                    {
                        Error = Values.Error + TEXT(" No assets were migrated.");
                        return false;
                    }
                    Snapshot.Rows.Add(Row.Key, MoveTemp(RowValues));
                }
                Tables.Add(MoveTemp(Snapshot));
            }
            return true;
        };
        if (!CaptureAssetValues())
        {
            if (!bContinueOnError)
            {
                return false;
            }
            RecordMigrationFailure(Asset.GetSoftObjectPath().ToString(), TEXT("Asset preflight values"), Error);
        }
    }
    for (const TStrongObjectPtr<UBlueprint>& Blueprint : DependentBlueprints)
    {
        if (Blueprint->Status != BS_Error)
        {
            continue;
        }
        bool bUsesConvertedType = false;
        for (UClass* ParentClass = Blueprint->ParentClass; ParentClass; ParentClass = ParentClass->GetSuperClass())
        {
            bUsesConvertedType |= TypeMappings.Contains(ParentClass);
        }
        TArray<UEdGraph*> Graphs;
        Blueprint->GetAllGraphs(Graphs);
        for (UEdGraph* Graph : Graphs)
        {
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (UK2Node* KismetNode = Cast<UK2Node>(Node))
                {
                    TArray<UStruct*> Dependencies;
                    KismetNode->HasExternalDependencies(&Dependencies);
                    for (UStruct* Dependency : Dependencies)
                    {
                        bUsesConvertedType |= TypeMappings.Contains(Dependency);
                    }
                }
                for (UEdGraphPin* Pin : Node->Pins)
                {
                    bUsesConvertedType |= TypeMappings.Contains(Cast<UField>(Pin->PinType.PinSubCategoryObject.Get()));
                    bUsesConvertedType |= TypeMappings.Contains(Cast<UField>(Pin->PinType.PinValueType.TerminalSubCategoryObject.Get()));
                }
            }
        }
        if (!bUsesConvertedType)
        {
            continue;
        }
        TArray<FString> BlueprintErrors;
        BlueprintErrors.Add(TEXT("Original Blueprint is invalid before migration: ") + Blueprint->GetPathName());
        for (UEdGraph* Graph : Graphs)
        {
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (Node->bHasCompilerMessage && Node->ErrorType == EMessageSeverity::Error && !Node->ErrorMsg.IsEmpty())
                {
                    BlueprintErrors.AddUnique(Node->GetPathName() + TEXT(": ") + Node->ErrorMsg);
                }
            }
        }
        UE_LOG(LogTemp, Warning, TEXT("Original Blueprint diagnostics before migration; final compilation will validate the remapped graph: %s"),
            *FString::Join(BlueprintErrors, TEXT("\n")));
        ChangedBlueprints.AddUnique(Blueprint.Get());
    }

    struct FMigrationRetainedStructure
    {
        TStrongObjectPtr<UUserDefinedStruct> Structure;
        TMap<FGuid, FEdGraphPinType> VariableTypes;
        TArray<FMigrationValue> Defaults;
    };
    TArray<FMigrationRetainedStructure> RetainedStructures;
    for (const TStrongObjectPtr<UObject>& Asset : LoadedAssets)
    {
        UUserDefinedStruct* Structure = Cast<UUserDefinedStruct>(Asset.Get());
        if (!Structure || TypeMappings.Contains(Structure))
        {
            continue;
        }
        FMigrationRetainedStructure Snapshot;
        Snapshot.Structure.Reset(Structure);
        for (const FStructVariableDescription& Variable : FStructureEditorUtils::GetVarDesc(Structure))
        {
            FEdGraphPinType VariableType = Variable.ToPinType();
            bool bChanged = false;
            for (const TPair<UField*, UField*>& Mapping : ReferenceMappings)
            {
                if (VariableType.PinSubCategoryObject.Get() == Mapping.Key)
                {
                    VariableType.PinSubCategoryObject = Mapping.Value;
                    bChanged = true;
                }
                if (VariableType.PinValueType.TerminalSubCategoryObject.Get() == Mapping.Key)
                {
                    VariableType.PinValueType.TerminalSubCategoryObject = Mapping.Value;
                    bChanged = true;
                }
            }
            if (bChanged)
            {
                Snapshot.VariableTypes.Add(Variable.VarGuid, VariableType);
            }
        }
        if (!Snapshot.VariableTypes.IsEmpty())
        {
            Snapshot.Defaults = Values.CaptureStructure(Structure, Structure->GetDefaultInstance());
            RetainedStructures.Add(MoveTemp(Snapshot));
        }
    }
    TMap<FName, TSet<FName>> MigrationPackageConnections;
    if (bContinueOnError)
    {
        TSet<FName> TypePackages;
        for (const TPair<UField*, UField*>& Mapping : ReferenceMappings)
        {
            TypePackages.Add(Mapping.Key->GetOutermost()->GetFName());
        }
        for (const TStrongObjectPtr<UBlueprint>& Blueprint : DependentBlueprints)
        {
            TypePackages.Add(Blueprint->GetOutermost()->GetFName());
        }
        for (const FMigrationRetainedStructure& Snapshot : RetainedStructures)
        {
            TypePackages.Add(Snapshot.Structure->GetOutermost()->GetFName());
        }
        for (const TStrongObjectPtr<UObject>& Asset : LoadedAssets)
        {
            if (!Asset)
            {
                continue;
            }
            const FName Package = Asset->GetOutermost()->GetFName();
            TArray<FName> Dependencies;
            Registry.Get().GetDependencies(Package, Dependencies);
            for (FName Dependency : Dependencies)
            {
                if (TypePackages.Contains(Dependency) && Dependency != Package)
                {
                    MigrationPackageConnections.FindOrAdd(Package).Add(Dependency);
                    MigrationPackageConnections.FindOrAdd(Dependency).Add(Package);
                }
            }
        }
        for (const TStrongObjectPtr<UBlueprint>& Blueprint : DependentBlueprints)
        {
            const FName Package = Blueprint->GetOutermost()->GetFName();
            auto ConnectReferencedType = [&](UObject* Type)
            {
                if (Type)
                {
                    const FName Dependency = Type->GetOutermost()->GetFName();
                    if (TypePackages.Contains(Dependency) && Dependency != Package)
                    {
                        MigrationPackageConnections.FindOrAdd(Package).Add(Dependency);
                        MigrationPackageConnections.FindOrAdd(Dependency).Add(Package);
                    }
                }
            };
            ConnectReferencedType(Blueprint->ParentClass);
            for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
            {
                ConnectReferencedType(Variable.VarType.PinSubCategoryObject.Get());
                ConnectReferencedType(Variable.VarType.PinValueType.TerminalSubCategoryObject.Get());
            }
            TArray<UEdGraph*> Graphs;
            Blueprint->GetAllGraphs(Graphs);
            for (UEdGraph* Graph : Graphs)
            {
                for (UEdGraphNode* Node : Graph->Nodes)
                {
                    if (UK2Node* KismetNode = Cast<UK2Node>(Node))
                    {
                        TArray<UStruct*> Dependencies;
                        KismetNode->HasExternalDependencies(&Dependencies);
                        for (UStruct* Dependency : Dependencies)
                        {
                            ConnectReferencedType(Dependency);
                        }
                    }
                    for (UEdGraphPin* Pin : Node->Pins)
                    {
                        ConnectReferencedType(Pin->PinType.PinSubCategoryObject.Get());
                        ConnectReferencedType(Pin->PinType.PinValueType.TerminalSubCategoryObject.Get());
                        ConnectReferencedType(Pin->DefaultObject);
                    }
                }
            }
        }
        for (const FMigrationInstance& Instance : Instances)
        {
            const FName Package(*FSoftObjectPath(Instance.Path).GetLongPackageName());
            const FName ClassPackage = Instance.Source->GetClass()->GetOutermost()->GetFName();
            if (TypePackages.Contains(ClassPackage) && ClassPackage != Package)
            {
                MigrationPackageConnections.FindOrAdd(Package).Add(ClassPackage);
                MigrationPackageConnections.FindOrAdd(ClassPackage).Add(Package);
            }
        }
        for (FName Package : FailedPackages)
        {
            TArray<FName> Dependencies;
            Registry.Get().GetDependencies(Package, Dependencies);
            for (FName Dependency : Dependencies)
            {
                if (TypePackages.Contains(Dependency) && Dependency != Package)
                {
                    MigrationPackageConnections.FindOrAdd(Package).Add(Dependency);
                    MigrationPackageConnections.FindOrAdd(Dependency).Add(Package);
                }
            }
        }
        for (const TPair<UObject*, FMigrationObject>& Captured : Values.Objects)
        {
            const FName Package = Captured.Key->GetOutermost()->GetFName();
            TArray<const FMigrationValue*> PendingValues;
            for (const FMigrationValue& Property : Captured.Value.Properties)
            {
                PendingValues.Add(&Property);
            }
            for (int32 Index = 0; Index < PendingValues.Num(); ++Index)
            {
                const FMigrationValue& Value = *PendingValues[Index];
                TArray<FName> Dependencies;
                if (Value.Object.IsValid())
                {
                    Dependencies.Add(Value.Object->GetOutermost()->GetFName());
                    Dependencies.Add(Value.Object->GetClass()->GetOutermost()->GetFName());
                }
                if (Value.Kind == EMigrationValueKind::SoftObject)
                {
                    Dependencies.Add(FName(*FSoftObjectPath(Value.Text).GetLongPackageName()));
                }
                for (FName Dependency : Dependencies)
                {
                    if (TypePackages.Contains(Dependency) && Dependency != Package)
                    {
                        MigrationPackageConnections.FindOrAdd(Package).Add(Dependency);
                        MigrationPackageConnections.FindOrAdd(Dependency).Add(Package);
                    }
                }
                for (const FMigrationValue& Child : Value.Children)
                {
                    PendingValues.Add(&Child);
                }
            }
        }
    }
    if (bPreviewOnly)
    {
        if (!MigrationErrors.IsEmpty())
        {
            Error = FString::Join(MigrationErrors, TEXT("\n")) + TEXT("\nNo assets were modified or saved.");
            return bContinueOnError;
        }
        return true;
    }
    Values.bCaptureObjectPaths = false;
    FBlueprintSubsystemReinstanceScope SubsystemReinstanceScope;
    if (bKeepAssetReferencesInBlueprint)
    {
        for (const TStrongObjectPtr<UBlueprint>& Blueprint : DependentBlueprints)
        {
            if (ConvertedBlueprints.Contains(Blueprint.Get()))
            {
                continue;
            }
            TSet<UClass*> LibraryClasses;
            TArray<UEdGraph*> Graphs;
            Blueprint->GetAllGraphs(Graphs);
            for (UEdGraph* Graph : Graphs)
            {
                for (UEdGraphNode* Node : Graph->Nodes)
                {
                    UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
                    UFunction* Function = nullptr;
                    if (Call)
                    {
                        Function = Call->GetTargetFunction();
                    }
                    if (!Function)
                    {
                        continue;
                    }
                    UClass* LibraryClass = Function->GetOwnerClass();
                    UBlueprint* LibraryBlueprint = UBlueprint::GetBlueprintFromClass(LibraryClass);
                    if (LibraryBlueprint && LibraryBlueprint->BlueprintType == BPTYPE_FunctionLibrary && TypeMappings.Contains(LibraryClass))
                    {
                        LibraryClasses.Add(LibraryClass);
                    }
                }
            }
            for (UClass* LibraryClass : LibraryClasses)
            {
                const FName DependencyName = FBlueprintEditorUtils::FindUniqueKismetName(Blueprint.Get(),
                    TEXT("BlueprintLibraryAssets") + LibraryClass->GetName());
                FEdGraphPinType DependencyType;
                DependencyType.PinCategory = UEdGraphSchema_K2::PC_Object;
                DependencyType.PinSubCategoryObject = CastChecked<UClass>(TypeMappings.FindChecked(LibraryClass));
                if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint.Get(), DependencyName, DependencyType))
                {
                    Error = TEXT("Migration cannot preserve the Blueprint library asset dependency in ") + Blueprint->GetPathName();
                    if (!bContinueOnError)
                    {
                        return false;
                    }
                    RecordMigrationFailure(Blueprint->GetPathName(), TEXT("Blueprint library dependency"), Error);
                    continue;
                }
                FMigrationValue DependencyValue;
                DependencyValue.Kind = EMigrationValueKind::Object;
                DependencyValue.Name = DependencyName.ToString();
                DependencyValue.Object.Reset(LibraryClass->GetDefaultObject());
                DependencyValue.SourcePath = LibraryClass->GetDefaultObject()->GetPathName();
                UObject* OriginalDefault = DependentDefaults.FindRef(Blueprint.Get());
                Values.CaptureObject(OriginalDefault);
                if (OriginalDefault)
                {
                    Values.Objects.FindChecked(OriginalDefault).Properties.Add(MoveTemp(DependencyValue));
                }
                ChangedBlueprints.AddUnique(Blueprint.Get());
            }
        }
    }

    struct FMigrationOriginalInvalidConnection
    {
        FEdGraphPinType OutputType;
        FEdGraphPinType InputType;
        FString OutputDefaultValue;
        FString InputDefaultValue;
        FString OutputAutogeneratedDefault;
        FString InputAutogeneratedDefault;
        TStrongObjectPtr<UClass> OutputClass;
        TStrongObjectPtr<UClass> InputClass;
        TStrongObjectPtr<UClass> OutputTerminalClass;
        TStrongObjectPtr<UClass> InputTerminalClass;
    };
    TMap<UEdGraphPin*, TMap<UEdGraphPin*, FMigrationOriginalInvalidConnection>> OriginalInvalidConnections;
    TMap<UEdGraphNode*, TMap<FGuid, TStrongObjectPtr<UClass>>> OriginalOutputClasses;
    for (const TStrongObjectPtr<UBlueprint>& Blueprint : DependentBlueprints)
    {
        TArray<UEdGraph*> Graphs;
        Blueprint->GetAllGraphs(Graphs);
        for (UEdGraph* Graph : Graphs)
        {
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                for (UEdGraphPin* Output : Node->Pins)
                {
                    if (Output->Direction != EGPD_Output)
                    {
                        continue;
                    }
                    OriginalOutputClasses.FindOrAdd(Node).Add(Output->PinId,
                        TStrongObjectPtr<UClass>(Cast<UClass>(Output->PinType.PinSubCategoryObject.Get())));
                    for (UEdGraphPin* Input : Output->LinkedTo)
                    {
                        if (!Output->GetSchema()->ArePinsCompatible(Output, Input, Blueprint->GeneratedClass))
                        {
                            OriginalInvalidConnections.FindOrAdd(Output).Add(Input,
                                {Output->PinType, Input->PinType, Output->GetDefaultAsString(), Input->GetDefaultAsString(),
                                    Output->AutogeneratedDefaultValue, Input->AutogeneratedDefaultValue,
                                    TStrongObjectPtr<UClass>(Cast<UClass>(Output->PinType.PinSubCategoryObject.Get())),
                                    TStrongObjectPtr<UClass>(Cast<UClass>(Input->PinType.PinSubCategoryObject.Get())),
                                    TStrongObjectPtr<UClass>(Cast<UClass>(Output->PinType.PinValueType.TerminalSubCategoryObject.Get())),
                                    TStrongObjectPtr<UClass>(Cast<UClass>(Input->PinType.PinValueType.TerminalSubCategoryObject.Get()))});
                        }
                    }
                }
            }
        }
    }

    for (const FMigrationBlueprint& Snapshot : Blueprints)
    {
        UBlueprint* Blueprint = Snapshot.Blueprint.Get();
        UBlueprintNativizationLibrary::ClearBlueprint(Blueprint);
        Blueprint->ParentClass = Snapshot.NativeClass.Get();
        Blueprint->bCachedDependenciesUpToDate = false;
        Blueprint->Status = BS_Dirty;
    }

    for (const TStrongObjectPtr<UObject>& Asset : LoadedAssets)
    {
        UBlueprint* Blueprint = Cast<UBlueprint>(Asset.Get());
        if (!Blueprint || ConvertedBlueprints.Contains(Blueprint))
        {
            continue;
        }
        auto RemapBlueprintGraph = [&]() -> bool
        {
            for (FBPVariableDescription& Variable : Blueprint->NewVariables)
            {
                Variable.DefaultValue = Values.RemapEnumValue(Cast<UEnum>(Variable.VarType.PinSubCategoryObject.Get()), Variable.DefaultValue);
            }
            TArray<UEdGraph*> Graphs;
            Blueprint->GetAllGraphs(Graphs);
            for (UEdGraph* Graph : Graphs)
            {
                TArray<UK2Node_FunctionEntry*> Entries;
                Graph->GetNodesOfClass(Entries);
                UFunction* GraphSignature = nullptr;
                const FString OriginalGraphName = Graph->GetName();
                if (!Entries.IsEmpty())
                {
                    GraphSignature = Values.FindSourceFunction(Entries[0]->FindSignatureFunction());
                    if (GraphSignature)
                    {
                        if (const FString* Name = MemberNames.Find(GraphSignature->GetPathName()))
                        {
                            if (Graph->GetFName() != FName(**Name))
                            {
                                Graph->Rename(**Name, nullptr, REN_DontCreateRedirectors | REN_NonTransactional);
                            }
                        }
                    }
                }
                for (UEdGraphNode* Node : Graph->Nodes)
                {
                    if (UK2Node_EditablePinBase* EditableNode = Cast<UK2Node_EditablePinBase>(Node))
                    {
                        for (const TSharedPtr<FUserPinInfo>& UserPin : EditableNode->UserDefinedPins)
                        {
                            UserPin->PinDefaultValue = Values.RemapEnumValue(
                                Cast<UEnum>(UserPin->PinType.PinSubCategoryObject.Get()), UserPin->PinDefaultValue);
                        }
                    }
                    if (UK2Node_SwitchEnum* Switch = Cast<UK2Node_SwitchEnum>(Node))
                    {
                        for (UEdGraphPin* Pin : Switch->Pins)
                        {
                            if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
                            {
                                Pin->PinName = FName(*Values.RemapEnumValue(Switch->Enum.Get(), Pin->PinName.ToString()));
                            }
                        }
                        for (FName& Entry : Switch->EnumEntries)
                        {
                            Entry = FName(*Values.RemapEnumValue(Switch->Enum.Get(), Entry.ToString()));
                        }
                    }
                    if (UK2Node_Select* Select = Cast<UK2Node_Select>(Node))
                    {
                        UEnum* Enumeration = Select->GetEnum();
                        if (Enumeration && TypeMappings.Contains(Enumeration))
                        {
                            TArray<UEdGraphPin*> OptionPins;
                            Select->GetOptionPins(OptionPins);
                            for (UEdGraphPin* Pin : OptionPins)
                            {
                                Pin->PinName = FName(*Values.RemapEnumValue(Enumeration, Pin->PinName.ToString()));
                            }
                            Select->ReloadEnum(CastChecked<UEnum>(TypeMappings.FindChecked(Enumeration)));
                            ChangedBlueprints.AddUnique(Blueprint);
                        }
                    }
                    UK2Node_CallFunction* OriginalCall = Cast<UK2Node_CallFunction>(Node);
                    UK2Node_BaseMCDelegate* OriginalDelegate = Cast<UK2Node_BaseMCDelegate>(Node);
                    UK2Node_Event* OriginalEvent = Cast<UK2Node_Event>(Node);
                    UK2Node_FunctionTerminator* OriginalTerminator = Cast<UK2Node_FunctionTerminator>(Node);
                    FMemberReference* SignatureMember = nullptr;
                    UFunction* OriginalFunction = nullptr;
                    if (OriginalCall)
                    {
                        OriginalFunction = OriginalCall->GetTargetFunction();
                        SignatureMember = &OriginalCall->FunctionReference;
                    }
                    else if (OriginalEvent)
                    {
                        OriginalFunction = OriginalEvent->EventReference.ResolveMember<UFunction>(Blueprint->SkeletonGeneratedClass.Get());
                        SignatureMember = &OriginalEvent->EventReference;
                    }
                    else if (OriginalTerminator)
                    {
                        OriginalFunction = OriginalTerminator->FindSignatureFunction();
                        SignatureMember = &OriginalTerminator->FunctionReference;
                    }
                    else if (OriginalDelegate)
                    {
                        OriginalFunction = OriginalDelegate->GetDelegateSignature();
                    }
                    if (UK2Node_CreateDelegate* CreateDelegate = Cast<UK2Node_CreateDelegate>(Node))
                    {
                        UClass* Scope = CreateDelegate->GetScopeClass();
                        if (Scope)
                        {
                            UFunction* Function = Values.FindSourceFunction(Scope->FindFunctionByName(CreateDelegate->GetFunctionName()));
                            if (Function)
                            {
                                if (const FString* Name = MemberNames.Find(Function->GetPathName()))
                                {
                                    CreateDelegate->SetFunction(FName(**Name));
                                    ChangedBlueprints.AddUnique(Blueprint);
                                }
                            }
                        }
                    }
                    OriginalFunction = Values.FindSourceFunction(OriginalFunction);
                    if (!OriginalFunction && SignatureMember && !SignatureMember->GetMemberName().IsNone())
                    {
                        UClass* SourceOwner = SignatureMember->GetMemberParentClass();
                        if (SignatureMember->IsSelfContext())
                        {
                            SourceOwner = Blueprint->GeneratedClass;
                        }
                        for (UClass* Owner = SourceOwner; Owner; Owner = Owner->GetSuperClass())
                        {
                            UClass* SourceClass = Values.FindSourceClass(Owner);
                            if (!SourceClass)
                            {
                                SourceClass = Owner;
                                for (const TPair<UField*, UField*>& Mapping : TypeMappings)
                                {
                                    if (Mapping.Value == Owner)
                                    {
                                        SourceClass = CastChecked<UClass>(Mapping.Key);
                                        break;
                                    }
                                }
                            }
                            UField* const* NativeType = TypeMappings.Find(SourceClass);
                            const FString OriginalFunctionPath = SourceClass->GetPathName() + TEXT(":") + SignatureMember->GetMemberName().ToString();
                            const FString* NativeName = MemberNames.Find(OriginalFunctionPath);
                            UClass* NativeClass = nullptr;
                            if (NativeType)
                            {
                                NativeClass = Cast<UClass>(*NativeType);
                            }
                            if (!NativeClass || !NativeName || !NativeClass->FindFunctionByName(FName(**NativeName)))
                            {
                                continue;
                            }
                            SignatureMember->SetExternalMember(FName(**NativeName), NativeClass);
                            for (UEdGraphPin* Pin : Node->Pins)
                            {
                                if (const FString* ParameterName = MemberNames.Find(OriginalFunctionPath + TEXT(":") + Pin->PinName.ToString()))
                                {
                                    Pin->PinName = FName(**ParameterName);
                                }
                            }
                            ChangedBlueprints.AddUnique(Blueprint);
                            break;
                        }
                    }
                    if (!OriginalFunction && OriginalTerminator)
                    {
                        OriginalFunction = GraphSignature;
                    }
                    if (OriginalFunction && SignatureMember)
                    {
                        if (const FString* Name = MemberNames.Find(OriginalFunction->GetPathName()))
                        {
                            if (OriginalCall && !OriginalCall->IsA<UK2Node_CallParentFunction>() && SignatureMember->IsSelfContext())
                            {
                                SignatureMember->SetSelfMember(FName(**Name));
                            }
                            else
                            {
                                SignatureMember->SetExternalMember(FName(**Name), CastChecked<UClass>(TypeMappings.FindChecked(OriginalFunction->GetOwnerClass())));
                            }
                            ChangedBlueprints.AddUnique(Blueprint);
                        }
                        if (UK2Node_EditablePinBase* EditableNode = Cast<UK2Node_EditablePinBase>(Node))
                        {
                            for (const TSharedPtr<FUserPinInfo>& UserPin : EditableNode->UserDefinedPins)
                            {
                                if (FProperty* Parameter = FindFProperty<FProperty>(OriginalFunction, UserPin->PinName))
                                {
                                    if (const FString* Name = MemberNames.Find(Parameter->GetPathName()))
                                    {
                                        UserPin->PinName = FName(**Name);
                                    }
                                }
                            }
                        }
                    }
                    if (OriginalFunction && SignatureMember && TypeMappings.Contains(OriginalFunction->GetOwnerClass()) &&
                        (OriginalEvent || (OriginalCall && OriginalCall->IsA<UK2Node_CallParentFunction>())))
                    {
                        if (const FString* Name = MemberNames.Find(OriginalFunction->GetPathName()))
                        {
                            UClass* NativeClass = CastChecked<UClass>(TypeMappings.FindChecked(OriginalFunction->GetOwnerClass()));
                            UFunction* NativeFunction = NativeClass->FindFunctionByName(FName(**Name));
                            if (NativeFunction && NativeFunction->HasAnyFunctionFlags(FUNC_Net))
                            {
                                const FString BodyName = NativeFunction->GetMetaData(TEXT("NativeEventBody"));
                                if (!BodyName.IsEmpty())
                                {
                                    UFunction* BodyFunction = NativeClass->FindFunctionByName(FName(*BodyName));
                                    if (!BodyFunction || !BodyFunction->HasAnyFunctionFlags(FUNC_BlueprintEvent))
                                    {
                                        Error = TEXT("Native network event body is unavailable for ") + NativeFunction->GetPathName();
                                        return false;
                                    }
                                    SignatureMember->SetExternalMember(FName(*BodyName), NativeClass);
                                    ChangedBlueprints.AddUnique(Blueprint);
                                }
                            }
                        }
                    }
                    UK2Node_Variable* OriginalVariable = Cast<UK2Node_Variable>(Node);
                    FProperty* OriginalProperty = nullptr;
                    if (OriginalVariable)
                    {
                        OriginalProperty = OriginalVariable->GetPropertyForVariable();
                        if (OriginalVariable->VariableReference.IsLocalScope() &&
                            OriginalVariable->VariableReference.GetMemberScopeName() == OriginalGraphName &&
                            OriginalGraphName != Graph->GetName())
                        {
                            OriginalVariable->VariableReference.SetLocalMember(OriginalVariable->VariableReference.GetMemberName(),
                                Graph->GetName(), OriginalVariable->VariableReference.GetMemberGuid());
                        }
                        if (GraphSignature && OriginalVariable->VariableReference.IsLocalScope())
                        {
                            if (FProperty* Parameter = FindFProperty<FProperty>(GraphSignature, OriginalVariable->VariableReference.GetMemberName()))
                            {
                                if (const FString* Name = MemberNames.Find(Parameter->GetPathName()))
                                {
                                    OriginalProperty = Parameter;
                                    OriginalVariable->VariableReference.SetLocalMember(FName(**Name), Graph->GetName(), OriginalVariable->VariableReference.GetMemberGuid());
                                    ChangedBlueprints.AddUnique(Blueprint);
                                }
                            }
                        }
                    }
                    else if (OriginalDelegate)
                    {
                        OriginalProperty = OriginalDelegate->GetProperty();
                    }
                    if (OriginalProperty && OriginalProperty->GetOwnerStruct()->IsA<UClass>())
                    {
                        if (UClass* SourceClass = Values.FindSourceClass(OriginalProperty->GetOwnerClass()))
                        {
                            OriginalProperty = FindFProperty<FProperty>(SourceClass, OriginalProperty->GetFName());
                        }
                    }
                    if (!OriginalProperty && (OriginalVariable || OriginalDelegate))
                    {
                        FMemberReference* PropertyMember = nullptr;
                        if (OriginalVariable)
                        {
                            PropertyMember = &OriginalVariable->VariableReference;
                        }
                        else if (FStructProperty* DelegateMember = FindFProperty<FStructProperty>(OriginalDelegate->GetClass(), TEXT("DelegateReference")))
                        {
                            PropertyMember = DelegateMember->ContainerPtrToValuePtr<FMemberReference>(OriginalDelegate);
                        }
                        if (PropertyMember && !PropertyMember->IsLocalScope())
                        {
                            const FName OriginalName = PropertyMember->GetMemberName();
                            for (UClass* Owner = PropertyMember->GetMemberParentClass(Blueprint->GeneratedClass); Owner; Owner = Owner->GetSuperClass())
                            {
                                UClass* SourceClass = Values.FindSourceClass(Owner);
                                if (!SourceClass)
                                {
                                    SourceClass = Owner;
                                    for (const TPair<UField*, UField*>& Mapping : TypeMappings)
                                    {
                                        if (Mapping.Value == Owner)
                                        {
                                            SourceClass = CastChecked<UClass>(Mapping.Key);
                                            break;
                                        }
                                    }
                                }
                                UField* const* NativeType = TypeMappings.Find(SourceClass);
                                const FString* NativeName = MemberNames.Find(SourceClass->GetPathName() + TEXT(":") + OriginalName.ToString());
                                UClass* NativeClass = nullptr;
                                if (NativeType)
                                {
                                    NativeClass = Cast<UClass>(*NativeType);
                                }
                                if (!NativeClass || !NativeName || !FindFProperty<FProperty>(NativeClass, FName(**NativeName)))
                                {
                                    continue;
                                }
                                PropertyMember->SetExternalMember(FName(**NativeName), NativeClass);
                                for (UEdGraphPin* Pin : Node->Pins)
                                {
                                    if (Pin->PinName == OriginalName)
                                    {
                                        Pin->PinName = FName(**NativeName);
                                    }
                                }
                                ChangedBlueprints.AddUnique(Blueprint);
                                break;
                            }
                        }
                    }
                    UK2Node_StructOperation* StructureOperation = Cast<UK2Node_StructOperation>(Node);
                    UK2Node_GetClassDefaults* ClassDefaults = Cast<UK2Node_GetClassDefaults>(Node);
                    UClass* DefaultsClass = nullptr;
                    TMap<FName, FProperty*> MappedClassDefaultsProperties;
                    auto GetClassDefaultsPropertyName = [&](FName OriginalName)
                    {
                        if (FProperty* Property = FindFProperty<FProperty>(DefaultsClass, OriginalName))
                        {
                            const FName DestinationName(*Values.GetMemberName(Property));
                            UField* const* NativeOwner = TypeMappings.Find(Property->GetOwnerClass());
                            if (NativeOwner && MemberNames.Contains(Property->GetPathName()))
                            {
                                if (FProperty* NativeProperty = FindFProperty<FProperty>(Cast<UClass>(*NativeOwner), DestinationName))
                                {
                                    MappedClassDefaultsProperties.Add(OriginalName, NativeProperty);
                                }
                            }
                            return DestinationName;
                        }
                        for (UClass* Owner = DefaultsClass; Owner; Owner = Owner->GetSuperClass())
                        {
                            for (const TPair<UField*, UField*>& Mapping : TypeMappings)
                            {
                                UClass* SourceClass = Cast<UClass>(Mapping.Key);
                                UClass* NativeClass = Cast<UClass>(Mapping.Value);
                                if (!SourceClass || !NativeClass || (Owner != SourceClass && Owner != NativeClass))
                                {
                                    continue;
                                }
                                const FString* NativeName = MemberNames.Find(SourceClass->GetPathName() + TEXT(":") + OriginalName.ToString());
                                if (NativeName)
                                {
                                    if (FProperty* NativeProperty = FindFProperty<FProperty>(NativeClass, FName(**NativeName)))
                                    {
                                        MappedClassDefaultsProperties.Add(OriginalName, NativeProperty);
                                        return FName(**NativeName);
                                    }
                                }
                            }
                        }
                        return OriginalName;
                    };
                    if (ClassDefaults)
                    {
                        DefaultsClass = ClassDefaults->GetInputClass();
                        if (UClass* SourceClass = Values.FindSourceClass(DefaultsClass))
                        {
                            DefaultsClass = SourceClass;
                        }
                        for (FOptionalPinFromProperty& Option : ClassDefaults->*GetStructPrivateMember(FClassDefaultsPinOptionsAccess{}))
                        {
                            const FName PropertyName = GetClassDefaultsPropertyName(Option.PropertyName);
                            if (PropertyName != Option.PropertyName)
                            {
                                Option.PropertyName = PropertyName;
                                ChangedBlueprints.AddUnique(Blueprint);
                            }
                        }
                    }
                    for (UEdGraphPin* Pin : Node->Pins)
                    {
                        if (Pin->ParentPin)
                        {
                            continue;
                        }
                        FString DestinationName = Pin->PinName.ToString();
                        if (ClassDefaults && Pin->Direction == EGPD_Output)
                        {
                            if (DefaultsClass)
                            {
                                DestinationName = GetClassDefaultsPropertyName(Pin->PinName).ToString();
                            }
                            if (Pin->bOrphanedPin && !Pin->LinkedTo.IsEmpty())
                            {
                                FProperty* const* NativeProperty = MappedClassDefaultsProperties.Find(Pin->PinName);
                                if (NativeProperty && (*NativeProperty)->HasAllPropertyFlags(CPF_BlueprintVisible))
                                {
                                    FProperty* ValueProperty = *NativeProperty;
                                    if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(ValueProperty))
                                    {
                                        ValueProperty = ArrayProperty->Inner;
                                    }
                                    else if (FSetProperty* SetProperty = CastField<FSetProperty>(ValueProperty))
                                    {
                                        ValueProperty = SetProperty->ElementProp;
                                    }
                                    FEdGraphPinType NativePinType;
                                    const UEdGraphSchema_K2* GraphSchema = GetDefault<UEdGraphSchema_K2>();
                                    if (!ValueProperty->IsA<FMapProperty>() &&
                                        (!ValueProperty->IsA<FObjectPropertyBase>() || ValueProperty->IsA<FClassProperty>()) &&
                                        GraphSchema->ConvertPropertyToPinType(*NativeProperty, NativePinType))
                                    {
                                        TArray<FOptionalPinFromProperty>& Options = ClassDefaults->*GetStructPrivateMember(FClassDefaultsPinOptionsAccess{});
                                        FOptionalPinFromProperty* Option = Options.FindByPredicate([&DestinationName](const FOptionalPinFromProperty& Candidate)
                                        {
                                            return Candidate.PropertyName == FName(*DestinationName);
                                        });
                                        if (!Option)
                                        {
                                            Option = &Options.AddDefaulted_GetRef();
                                            Option->PropertyName = FName(*DestinationName);
                                            Option->bCanToggleVisibility = true;
                                        }
                                        Option->bShowPin = true;
                                        Pin->bOrphanedPin = false;
                                        ChangedBlueprints.AddUnique(Blueprint);
                                    }
                                }
                            }
                            if (Pin->bOrphanedPin || !FindFProperty<FProperty>(DefaultsClass, FName(*DestinationName)))
                            {
                                UE_LOG(LogTemp, Warning, TEXT("%s"), *FString::Format(
                                    TEXT("Migration class defaults pin diagnostic: node={0}; inputClass={1}; sourceClass={2}; originalPin={3}; destinationPin={4}; orphaned={5}; links={6}."),
                                    {Node->GetPathName(), GetPathNameSafe(ClassDefaults->GetInputClass()), GetPathNameSafe(DefaultsClass),
                                        Pin->PinName.ToString(), DestinationName, Pin->bOrphanedPin != 0, Pin->LinkedTo.Num()}));
                                for (const FOptionalPinFromProperty& Option : ClassDefaults->*GetStructPrivateMember(FClassDefaultsPinOptionsAccess{}))
                                {
                                    if (Option.PropertyName == Pin->PinName || Option.PropertyName == FName(*DestinationName))
                                    {
                                        UE_LOG(LogTemp, Warning, TEXT("%s"), *FString::Format(
                                            TEXT("Migration class defaults option diagnostic: property={0}; show={1}; toggle={2}."),
                                            {Option.PropertyName.ToString(), Option.bShowPin != 0, Option.bCanToggleVisibility != 0}));
                                    }
                                }
                            }
                        }
                        if (OriginalFunction && TypeMappings.Contains(OriginalFunction->GetOwnerClass()))
                        {
                            if (FProperty* Parameter = FindFProperty<FProperty>(OriginalFunction, Pin->PinName))
                            {
                                if (const FString* Name = MemberNames.Find(Parameter->GetPathName()))
                                {
                                    DestinationName = *Name;
                                }
                                if (Parameter->HasAnyPropertyFlags(CPF_ReturnParm))
                                {
                                    DestinationName = UEdGraphSchema_K2::PN_ReturnValue.ToString();
                                }
                            }
                        }
                        if (UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(Node))
                        {
                            if (Pin == CastNode->GetCastResultPin() && TypeMappings.Contains(CastNode->TargetType.Get()))
                            {
                                DestinationName = UEdGraphSchema_K2::PN_CastedValuePrefix +
                                    TypeMappings.FindChecked(CastNode->TargetType.Get())->GetDisplayNameText().ToString();
                            }
                        }
                        if (UK2Node_ConstructObjectFromClass* Construction = Cast<UK2Node_ConstructObjectFromClass>(Node))
                        {
                            if (Construction->IsSpawnVarPin(Pin))
                            {
                                if (UClass* Class = Construction->GetClassToSpawn())
                                {
                                    if (FProperty* Property = FindFProperty<FProperty>(Class, Pin->PinName))
                                    {
                                        DestinationName = Values.GetMemberName(Property);
                                    }
                                }
                            }
                        }
                        if (OriginalProperty && TypeMappings.Contains(OriginalProperty->GetOwnerClass()) && Pin->PinName == OriginalProperty->GetFName())
                        {
                            DestinationName = Values.GetMemberName(OriginalProperty);
                        }
                        if (StructureOperation && StructureOperation->StructType && TypeMappings.Contains(StructureOperation->StructType.Get()))
                        {
                            if (FProperty* Property = FindFProperty<FProperty>(StructureOperation->StructType.Get(), Pin->PinName))
                            {
                                DestinationName = Values.GetMemberName(Property);
                            }
                            else if (Pin->PinName == StructureOperation->StructType->GetFName())
                            {
                                DestinationName = TypeMappings.FindChecked(StructureOperation->StructType.Get())->GetName();
                            }
                        }
                        if (!Values.RemapPin(Pin, DestinationName))
                        {
                            Error = Values.Error;
                            return false;
                        }
                    }
                    if (OriginalDelegate && OriginalProperty && TypeMappings.Contains(OriginalProperty->GetOwnerClass()))
                    {
                        UClass* NativeClass = CastChecked<UClass>(TypeMappings.FindChecked(OriginalProperty->GetOwnerClass()));
                        FMulticastDelegateProperty* Property = FindFProperty<FMulticastDelegateProperty>(NativeClass, FName(*Values.GetMemberName(OriginalProperty)));
                        if (!Property)
                        {
                            Error = TEXT("Native multicast delegate is missing for ") + OriginalProperty->GetPathName();
                            return false;
                        }
                        OriginalDelegate->SetFromProperty(Property, false, NativeClass);
                        ChangedBlueprints.AddUnique(Blueprint);
                    }
                    if (UK2Node_Variable* Variable = Cast<UK2Node_Variable>(Node))
                    {
                        if (FProperty* Property = OriginalProperty)
                        {
                            if (TypeMappings.Contains(Property->GetOwnerClass()))
                            {
                                if (!Variable->VariableReference.IsLocalScope())
                                {
                                    if (const FString* Name = MemberNames.Find(Property->GetPathName()))
                                    {
                                        Variable->Modify();
                                        if (Variable->VariableReference.IsSelfContext())
                                        {
                                            Variable->VariableReference.SetSelfMember(FName(**Name));
                                        }
                                        else
                                        {
                                            Variable->VariableReference.SetExternalMember(FName(**Name), CastChecked<UClass>(TypeMappings.FindChecked(Property->GetOwnerClass())));
                                        }
                                        ChangedBlueprints.AddUnique(Blueprint);
                                    }
                                }
                            }
                        }
                    }
                }
            }
            return true;
        };
        if (!RemapBlueprintGraph())
        {
            if (!bContinueOnError)
            {
                return false;
            }
            RecordMigrationFailure(Blueprint->GetPathName(), TEXT("Graph type and member remapping"), Error);
        }
    }

    struct FMigrationGraphConnection
    {
        UEdGraphNode* OutputNode;
        FName OutputName;
        UEdGraphNode* InputNode;
        FName InputName;
        TStrongObjectPtr<UClass> OriginalOutputClass;
        TOptional<FMigrationOriginalInvalidConnection> OriginalInvalidConnection;
    };
    TMap<UBlueprint*, TArray<FMigrationGraphConnection>> GraphConnections;
    for (UBlueprint* Blueprint : ChangedBlueprints)
    {
        if (ConvertedBlueprints.Contains(Blueprint))
        {
            continue;
        }
        TArray<UEdGraph*> Graphs;
        Blueprint->GetAllGraphs(Graphs);
        for (UEdGraph* Graph : Graphs)
        {
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                for (UEdGraphPin* Pin : Node->Pins)
                {
                    if (Pin->Direction != EGPD_Output)
                    {
                        continue;
                    }
                    for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
                    {
                        FMigrationGraphConnection Connection{Node, Pin->PinName, LinkedPin->GetOwningNode(), LinkedPin->PinName,
                            TStrongObjectPtr<UClass>(), {}};
                        if (const TMap<FGuid, TStrongObjectPtr<UClass>>* OriginalNodeOutputs = OriginalOutputClasses.Find(Node))
                        {
                            if (const TStrongObjectPtr<UClass>* OriginalClass = OriginalNodeOutputs->Find(Pin->PinId))
                            {
                                Connection.OriginalOutputClass = *OriginalClass;
                            }
                        }
                        if (const TMap<UEdGraphPin*, FMigrationOriginalInvalidConnection>* Inputs = OriginalInvalidConnections.Find(Pin))
                        {
                            if (const FMigrationOriginalInvalidConnection* Original = Inputs->Find(LinkedPin))
                            {
                                Connection.OriginalInvalidConnection = *Original;
                            }
                        }
                        GraphConnections.FindOrAdd(Blueprint).Add(MoveTemp(Connection));
                    }
                }
            }
        }
    }
    auto RestoreGraphConnections = [&GraphConnections, &ConvertedBlueprints, &ReferenceMappings, &Error](UBlueprint* Blueprint, bool bRequireAllConnections)
    {
        auto IsUnchangedRetainedClass = [&ConvertedBlueprints, &ReferenceMappings](UClass* OriginalClass, UClass* CurrentClass) -> bool
        {
            if (!OriginalClass || !CurrentClass || !OriginalClass->HasAnyClassFlags(CLASS_CompiledFromBlueprint) ||
                !CurrentClass->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || ReferenceMappings.Contains(OriginalClass))
            {
                return false;
            }
            UBlueprint* OriginalBlueprint = Cast<UBlueprint>(OriginalClass->ClassGeneratedBy);
            UBlueprint* CurrentBlueprint = Cast<UBlueprint>(CurrentClass->ClassGeneratedBy);
            if (!OriginalBlueprint || OriginalBlueprint != CurrentBlueprint || ConvertedBlueprints.Contains(OriginalBlueprint) ||
                ReferenceMappings.Contains(OriginalBlueprint->GeneratedClass.Get()))
            {
                return false;
            }
            return OriginalClass->GetAuthoritativeClass() == CurrentClass->GetAuthoritativeClass();
        };
        auto IsUnchangedPinType = [&IsUnchangedRetainedClass](const FEdGraphPinType& CurrentType, const FEdGraphPinType& OriginalType,
            UClass* OriginalClass, UClass* OriginalTerminalClass) -> bool
        {
            FEdGraphPinType NormalizedOriginal = OriginalType;
            if (IsUnchangedRetainedClass(OriginalClass, Cast<UClass>(CurrentType.PinSubCategoryObject.Get())))
            {
                NormalizedOriginal.PinSubCategoryObject = CurrentType.PinSubCategoryObject;
            }
            if (IsUnchangedRetainedClass(OriginalTerminalClass, Cast<UClass>(CurrentType.PinValueType.TerminalSubCategoryObject.Get())))
            {
                NormalizedOriginal.PinValueType.TerminalSubCategoryObject = CurrentType.PinValueType.TerminalSubCategoryObject;
            }
            return CurrentType == NormalizedOriginal && CurrentType.bIsUObjectWrapper == NormalizedOriginal.bIsUObjectWrapper &&
                CurrentType.bSerializeAsSinglePrecisionFloat == NormalizedOriginal.bSerializeAsSinglePrecisionFloat;
        };
        const TArray<FMigrationGraphConnection>* SavedConnections = GraphConnections.Find(Blueprint);
        if (!SavedConnections)
        {
            return true;
        }
        TArray<UK2Node_CallFunction*> FunctionNodes;
        FBlueprintEditorUtils::GetAllNodesOfClass(Blueprint, FunctionNodes);
        for (UK2Node_CallFunction* FunctionNode : FunctionNodes)
        {
            UFunction* Function = FunctionNode->GetTargetFunction();
            if (Function && Function->HasMetaData(TEXT("DeterminesOutputType")))
            {
                const FName ParameterName(*Function->GetMetaData(TEXT("DeterminesOutputType")));
                if (UEdGraphPin* ClassPin = FunctionNode->FindPin(ParameterName, EGPD_Input))
                {
                    FunctionNode->PinDefaultValueChanged(ClassPin);
                }
            }
        }
        for (const FMigrationGraphConnection& Connection : *SavedConnections)
        {
            UK2Node_CallArrayFunction* ArrayFunction = Cast<UK2Node_CallArrayFunction>(Connection.InputNode);
            UEdGraphPin* Output = Connection.OutputNode->FindPin(Connection.OutputName, EGPD_Output);
            UEdGraphPin* Input = Connection.InputNode->FindPin(Connection.InputName, EGPD_Input);
            if (ArrayFunction && Output && Input && Input == ArrayFunction->GetTargetArrayPin() &&
                Output->LinkedTo.Contains(Input))
            {
                ArrayFunction->PinConnectionListChanged(Input);
            }
        }
        TSet<UK2Node_MacroInstance*> ChangedArrayMacros;
        for (const FMigrationGraphConnection& Connection : *SavedConnections)
        {
            UK2Node_MacroInstance* Macro = Cast<UK2Node_MacroInstance>(Connection.InputNode);
            UEdGraphPin* Output = Connection.OutputNode->FindPin(Connection.OutputName, EGPD_Output);
            UEdGraphPin* Input = Connection.InputNode->FindPin(Connection.InputName, EGPD_Input);
            if (!Macro || !Output || !Input || !Output->PinType.IsArray() || !Macro->GetMacroGraph() ||
                Output->GetSchema()->ArePinsCompatible(Output, Input, Blueprint->GeneratedClass))
            {
                continue;
            }
            for (UEdGraphNode* MacroNode : Macro->GetMacroGraph()->Nodes)
            {
                if (MacroNode->GetClass() != UK2Node_Tunnel::StaticClass())
                {
                    continue;
                }
                UEdGraphPin* Port = MacroNode->FindPin(Connection.InputName, EGPD_Output);
                if (Port && Port->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
                {
                    ChangedArrayMacros.Add(Macro);
                }
            }
        }
        for (UK2Node_MacroInstance* Macro : ChangedArrayMacros)
        {
            Macro->Modify();
            for (UEdGraphNode* MacroNode : Macro->GetMacroGraph()->Nodes)
            {
                if (MacroNode->GetClass() != UK2Node_Tunnel::StaticClass())
                {
                    continue;
                }
                for (UEdGraphPin* Port : MacroNode->Pins)
                {
                    if (Port->PinType.PinCategory != UEdGraphSchema_K2::PC_Wildcard)
                    {
                        continue;
                    }
                    if (UEdGraphPin* Pin = Macro->FindPin(Port->PinName, UEdGraphPin::GetComplementaryDirection(Port->Direction)))
                    {
                        Pin->BreakAllPinLinks();
                    }
                }
            }
            Macro->ResolvedWildcardType.ResetToDefaults();
            Macro->ReconstructNode();
        }
        for (const FMigrationGraphConnection& Connection : *SavedConnections)
        {
            UK2Node_MacroInstance* Macro = Cast<UK2Node_MacroInstance>(Connection.InputNode);
            UEdGraphPin* Output = Connection.OutputNode->FindPin(Connection.OutputName, EGPD_Output);
            UEdGraphPin* Input = Connection.InputNode->FindPin(Connection.InputName, EGPD_Input);
            if (Macro && ChangedArrayMacros.Contains(Macro) && Output && Input && Output->PinType.IsArray())
            {
                if (!Output->GetSchema()->TryCreateConnection(Output, Input))
                {
                    Error = TEXT("Migration could not infer the macro array element type: ") + Macro->GetPathName();
                    return false;
                }
            }
        }
        for (const FMigrationGraphConnection& Connection : *SavedConnections)
        {
            UEdGraphPin* Output = Connection.OutputNode->FindPin(Connection.OutputName, EGPD_Output);
            UEdGraphPin* Input = Connection.InputNode->FindPin(Connection.InputName, EGPD_Input);
            UClass* OriginalClass = Connection.OriginalOutputClass.Get();
            if (!Output || !Input || !OriginalClass || Output->PinType.PinCategory != UEdGraphSchema_K2::PC_Class ||
                Input->PinType.PinCategory != UEdGraphSchema_K2::PC_Class || Output->PinType.IsContainer() || Input->PinType.IsContainer())
            {
                continue;
            }
            UClass* OutputClass = Cast<UClass>(Output->PinType.PinSubCategoryObject.Get());
            UClass* RetainedClass = OriginalClass->GetAuthoritativeClass();
            if (!OutputClass || !RetainedClass->ClassGeneratedBy ||
                ConvertedBlueprints.Contains(Cast<UBlueprint>(RetainedClass->ClassGeneratedBy)) ||
                !UBlueprintNativizationLibrary::IsCppDefinedField(OutputClass) || !RetainedClass->IsChildOf(OutputClass))
            {
                continue;
            }
            UK2Node_ClassDynamicCast* ClassCast = nullptr;
            for (UEdGraphPin* LinkedPin : Output->LinkedTo)
            {
                UK2Node_ClassDynamicCast* ExistingCast = Cast<UK2Node_ClassDynamicCast>(LinkedPin->GetOwningNode());
                if (ExistingCast && ExistingCast->TargetType == RetainedClass)
                {
                    ClassCast = ExistingCast;
                    break;
                }
            }
            const UEdGraphSchema* Schema = Output->GetSchema();
            if (!ClassCast)
            {
                FGraphNodeCreator<UK2Node_ClassDynamicCast> CastCreator(*Output->GetOwningNode()->GetGraph());
                ClassCast = CastCreator.CreateNode();
                ClassCast->TargetType = RetainedClass;
                ClassCast->NodePosX = Output->GetOwningNode()->NodePosX + 160;
                ClassCast->NodePosY = Output->GetOwningNode()->NodePosY + 80;
                CastCreator.Finalize();
                ClassCast->SetPurity(true);
                if (!Schema->TryCreateConnection(Output, ClassCast->GetCastSourcePin()))
                {
                    Error = TEXT("Migration could not preserve the retained Blueprint class constraint: ") + Output->GetOwningNode()->GetPathName();
                    return false;
                }
            }
            if (ClassCast->GetCastResultPin()->LinkedTo.Contains(Input))
            {
                continue;
            }
            Output->BreakLinkTo(Input);
            if (!Schema->TryCreateConnection(ClassCast->GetCastResultPin(), Input))
            {
                Error = TEXT("Migration could not connect the retained Blueprint class constraint: ") + Input->GetOwningNode()->GetPathName();
                return false;
            }
        }
        TArray<UK2Node_ConstructObjectFromClass*> CreationNodes;
        FBlueprintEditorUtils::GetAllNodesOfClass(Blueprint, CreationNodes);
        for (UK2Node_ConstructObjectFromClass* CreationNode : CreationNodes)
        {
            CreationNode->PinConnectionListChanged(CreationNode->GetClassPin());
        }
        if (const TArray<FMigrationGraphConnection>* Connections = GraphConnections.Find(Blueprint))
        {
            for (const FMigrationGraphConnection& Connection : *Connections)
            {
                UEdGraphPin* Output = Connection.OutputNode->FindPin(Connection.OutputName, EGPD_Output);
                UEdGraphPin* Input = Connection.InputNode->FindPin(Connection.InputName, EGPD_Input);
                if (!Output || !Input)
                {
                    if (bRequireAllConnections)
                    {
                        Error = FString::Format(TEXT("Migration could not restore graph connection: {0}.{1} -> {2}.{3}."),
                            {Connection.OutputNode->GetPathName(), Connection.OutputName.ToString(), Connection.InputNode->GetPathName(), Connection.InputName.ToString()});
                        return false;
                    }
                    continue;
                }
                UK2Node_DynamicCast* ExistingCast = nullptr;
                if (Input->LinkedTo.Num() == 1)
                {
                    ExistingCast = Cast<UK2Node_DynamicCast>(Input->LinkedTo[0]->GetOwningNode());
                }
                const UEdGraphSchema* Schema = Output->GetSchema();
                if (ExistingCast && ExistingCast->GetCastSourcePin()->LinkedTo.Contains(Output) &&
                    Schema->ArePinsCompatible(Output, ExistingCast->GetCastSourcePin(), Blueprint->GeneratedClass) &&
                    Schema->ArePinsCompatible(ExistingCast->GetCastResultPin(), Input, Blueprint->GeneratedClass))
                {
                    continue;
                }
                const bool bOriginalConnectionStillLinked = Output->LinkedTo.Contains(Input);
                if (bOriginalConnectionStillLinked && Schema->ArePinsCompatible(Output, Input, Blueprint->GeneratedClass))
                {
                    continue;
                }
                if (Connection.OriginalInvalidConnection.IsSet() && (bOriginalConnectionStillLinked || Input->LinkedTo.IsEmpty()))
                {
                    const FMigrationOriginalInvalidConnection& Original = Connection.OriginalInvalidConnection.GetValue();
                    if (IsUnchangedPinType(Output->PinType, Original.OutputType, Original.OutputClass.Get(), Original.OutputTerminalClass.Get()) &&
                        IsUnchangedPinType(Input->PinType, Original.InputType, Original.InputClass.Get(), Original.InputTerminalClass.Get()) &&
                        Output->GetDefaultAsString() == Original.OutputDefaultValue &&
                        Input->GetDefaultAsString() == Original.InputDefaultValue &&
                        Output->AutogeneratedDefaultValue == Original.OutputAutogeneratedDefault &&
                        Input->AutogeneratedDefaultValue == Original.InputAutogeneratedDefault)
                    {
                        if (!bOriginalConnectionStillLinked)
                        {
                            Output->MakeLinkTo(Input);
                        }
                        const FString Warning = FString::Format(
                            TEXT("Preserving or restoring an unchanged incompatible source Blueprint connection for final compilation: {0}.{1} -> {2}.{3}."),
                            {Connection.OutputNode->GetPathName(), Connection.OutputName.ToString(),
                                Connection.InputNode->GetPathName(), Connection.InputName.ToString()});
                        UE_LOG(LogTemp, Warning, TEXT("%s"), *Warning);
                        continue;
                    }
                }
                if (bOriginalConnectionStillLinked)
                {
                    Output->BreakLinkTo(Input);
                }
                if (Schema->CanCreateConnection(Output, Input).Response != CONNECT_RESPONSE_MAKE &&
                    Output->PinType.PinCategory == UEdGraphSchema_K2::PC_Object && !Output->PinType.IsContainer() &&
                    Input->PinType.PinCategory == UEdGraphSchema_K2::PC_Object && !Input->PinType.IsContainer())
                {
                    UClass* OutputClass = Cast<UClass>(Output->PinType.PinSubCategoryObject.Get());
                    UClass* InputClass = Cast<UClass>(Input->PinType.PinSubCategoryObject.Get());
                    UClass* OriginalClass = Connection.OriginalOutputClass.Get();
                    if (OriginalClass && OutputClass && InputClass && InputClass->ClassGeneratedBy &&
                        UBlueprintNativizationLibrary::IsCppDefinedField(OutputClass) &&
                        !UBlueprintNativizationLibrary::IsCppDefinedField(InputClass) &&
                        OriginalClass->GetAuthoritativeClass()->IsChildOf(InputClass->GetAuthoritativeClass()) &&
                        OriginalClass->GetAuthoritativeClass()->IsChildOf(OutputClass))
                    {
                        FGraphNodeCreator<UK2Node_DynamicCast> CastCreator(*Output->GetOwningNode()->GetGraph());
                        UK2Node_DynamicCast* CastNode = CastCreator.CreateNode();
                        CastNode->TargetType = InputClass->GetAuthoritativeClass();
                        CastNode->NodePosX = Output->GetOwningNode()->NodePosX + 160;
                        CastNode->NodePosY = Output->GetOwningNode()->NodePosY + 80;
                        CastCreator.Finalize();
                        CastNode->SetPurity(true);
                        if (Schema->TryCreateConnection(Output, CastNode->GetCastSourcePin()) &&
                            Schema->TryCreateConnection(CastNode->GetCastResultPin(), Input))
                        {
                            continue;
                        }
                    }
                }
                if (Schema->CanCreateConnection(Output, Input).Response != CONNECT_RESPONSE_MAKE || !Schema->TryCreateConnection(Output, Input))
                {
                    if (bRequireAllConnections)
                    {
                        Error = FString::Format(TEXT("Migration graph connection is incompatible after type replacement: {0}.{1} -> {2}.{3}."),
                            {Connection.OutputNode->GetPathName(), Connection.OutputName.ToString(), Connection.InputNode->GetPathName(), Connection.InputName.ToString()});
                        Error += FString::Format(TEXT(" Original invalid connection recorded: {0}; linked before restore: {1}."),
                            {Connection.OriginalInvalidConnection.IsSet(), bOriginalConnectionStillLinked});
                        if (Connection.OriginalInvalidConnection.IsSet())
                        {
                            const FMigrationOriginalInvalidConnection& Original = Connection.OriginalInvalidConnection.GetValue();
                            Error += FString::Format(TEXT(" Original output type unchanged: {0}; input type unchanged: {1}; output defaults unchanged: {2}; input defaults unchanged: {3}."),
                                {IsUnchangedPinType(Output->PinType, Original.OutputType, Original.OutputClass.Get(), Original.OutputTerminalClass.Get()),
                                    IsUnchangedPinType(Input->PinType, Original.InputType, Original.InputClass.Get(), Original.InputTerminalClass.Get()),
                                    Output->GetDefaultAsString() == Original.OutputDefaultValue && Output->AutogeneratedDefaultValue == Original.OutputAutogeneratedDefault,
                                    Input->GetDefaultAsString() == Original.InputDefaultValue && Input->AutogeneratedDefaultValue == Original.InputAutogeneratedDefault});
                        }
                        Error += FString::Format(TEXT(" Output type: {0}; input type: {1}."),
                            {UEdGraphSchema_K2::TypeToText(Output->PinType).ToString(), UEdGraphSchema_K2::TypeToText(Input->PinType).ToString()});
                        Error += FString::Format(TEXT(" Output field: {0}; input field: {1}."),
                            {GetPathNameSafe(Output->PinType.PinSubCategoryObject.Get()), GetPathNameSafe(Input->PinType.PinSubCategoryObject.Get())});
                        if (UK2Node_ConstructObjectFromClass* Creation = Cast<UK2Node_ConstructObjectFromClass>(Connection.OutputNode))
                        {
                            UEdGraphPin* ClassPin = Creation->GetClassPin();
                            Error += TEXT(" Creation class: ") + GetPathNameSafe(Creation->GetClassToSpawn()) + TEXT(".");
                            if (ClassPin)
                            {
                                for (UEdGraphPin* ClassSource : ClassPin->LinkedTo)
                                {
                                    Error += FString::Format(TEXT(" Class source: {0}.{1}; class field: {2}; default object: {3}."),
                                        {ClassSource->GetOwningNode()->GetPathName(), ClassSource->PinName.ToString(),
                                            GetPathNameSafe(ClassSource->PinType.PinSubCategoryObject.Get()), GetPathNameSafe(ClassSource->DefaultObject)});
                                }
                            }
                        }
                        if (UClass* OriginalClass = Connection.OriginalOutputClass.Get())
                        {
                            Error += TEXT(" Original output class: ") + OriginalClass->GetPathName() + TEXT(".");
                        }
                        if (UK2Node_CallFunction* FunctionNode = Cast<UK2Node_CallFunction>(Connection.OutputNode))
                        {
                            if (UFunction* Function = FunctionNode->GetTargetFunction())
                            {
                                Error += TEXT(" Source function: ") + Function->GetPathName() + TEXT(".");
                            }
                        }
                        if (UK2Node_Variable* Variable = Cast<UK2Node_Variable>(Connection.InputNode))
                        {
                            if (!Variable->GetPropertyForVariable())
                            {
                                UClass* MemberParent = Variable->VariableReference.GetMemberParentClass(Blueprint->GeneratedClass);
                                FString ParentPath = TEXT("None");
                                if (MemberParent)
                                {
                                    ParentPath = MemberParent->GetPathName();
                                }
                                Error += FString::Format(TEXT(" Unresolved variable member: {0}; owner: {1}."),
                                    {Variable->VariableReference.GetMemberName().ToString(), ParentPath});
                            }
                        }
                        return false;
                    }
                    continue;
                }
            }
        }
        return true;
    };
    for (const TPair<UField*, UField*>& Mapping : ReferenceMappings)
    {
        UBlueprintNativizationLibrary::ReplaceReferencesToType(Mapping.Key, Mapping.Value, ChangedBlueprints, true,
            !bKeepAssetReferencesInBlueprint && !DefaultOnlySourceClasses.Contains(Cast<UClass>(Mapping.Key)));
    }

    for (const TStrongObjectPtr<UBlueprint>& Blueprint : DependentBlueprints)
    {
        FMigrationReferenceArchive Archive(Blueprint.Get(), Values.ObjectMappings, Values.SoftMappings, DefaultOnlySourceClasses, bKeepAssetReferencesInBlueprint);
        if (Archive.GetCount() > 0 || Archive.bSoftReferencesChanged)
        {
            ChangedBlueprints.AddUnique(Blueprint.Get());
        }
    }
    for (const FMigrationRetainedStructure& Snapshot : RetainedStructures)
    {
        UUserDefinedStruct* Structure = Snapshot.Structure.Get();
        auto UpdateRetainedStructure = [&]() -> bool
        {
            FStructureEditorUtils::ModifyStructData(Structure);
            for (FStructVariableDescription& Variable : FStructureEditorUtils::GetVarDesc(Structure))
            {
                // Restore typed defaults after compilation instead of importing obsolete enum and structure text.
                Variable.DefaultValue.Reset();
                Variable.CurrentDefaultValue.Reset();
                if (const FEdGraphPinType* VariableType = Snapshot.VariableTypes.Find(Variable.VarGuid))
                {
                    Variable.SetPinType(*VariableType);
                }
            }
            FlushMigrationComponentUpdates();
            FStructureEditorUtils::OnStructureChanged(Structure, FStructureEditorUtils::VariableTypeChanged);
            if (Structure->Status != EUserDefinedStructureStatus::UDSS_UpToDate)
            {
                Error = TEXT("Retained structure failed to compile: ") + Structure->GetPathName();
                return false;
            }
            if (!Values.ApplyStructure(Structure, const_cast<uint8*>(Structure->GetDefaultInstance()), Snapshot.Defaults, Structure))
            {
                Error = Values.Error;
                return false;
            }
            CastChecked<UUserDefinedStructEditorData>(Structure->EditorData)->RefreshValuesFromDefaultInstance();
            FStructureEditorUtils::OnStructureChanged(Structure, FStructureEditorUtils::DefaultValueChanged);
            if (Structure->Status != EUserDefinedStructureStatus::UDSS_UpToDate)
            {
                Error = TEXT("Retained structure defaults failed to compile: ") + Structure->GetPathName();
                return false;
            }
            for (const TStrongObjectPtr<UBlueprint>& Blueprint : DependentBlueprints)
            {
                FBlueprintEditorUtils::EnsureCachedDependenciesUpToDate(Blueprint.Get());
                if (Blueprint->CachedUDSDependencies.Contains(Structure))
                {
                    ChangedBlueprints.AddUnique(Blueprint.Get());
                }
            }
            return true;
        };
        if (!UpdateRetainedStructure())
        {
            if (!bContinueOnError)
            {
                return false;
            }
            RecordMigrationFailure(Structure->GetPathName(), TEXT("Retained structure defaults"), Error);
        }
    }
    for (UBlueprint* Blueprint : ChangedBlueprints)
    {
        if (ConvertedBlueprints.Contains(Blueprint))
        {
            continue;
        }
        FBlueprintEditorUtils::ReconstructAllNodes(Blueprint);
        if (!RestoreGraphConnections(Blueprint, false))
        {
            if (!bContinueOnError)
            {
                return false;
            }
            RecordMigrationFailure(Blueprint->GetPathName(), TEXT("Graph connections"), Error);
        }
    }

    Blueprints.StableSort([](const FMigrationBlueprint& First, const FMigrationBlueprint& Second)
    {
        int32 FirstDepth = 0;
        int32 SecondDepth = 0;
        for (UClass* Class = First.NativeClass.Get(); Class; Class = Class->GetSuperClass())
        {
            ++FirstDepth;
        }
        for (UClass* Class = Second.NativeClass.Get(); Class; Class = Class->GetSuperClass())
        {
            ++SecondDepth;
        }
        return FirstDepth < SecondDepth;
    });
    TSet<UObject*> ChangedAssets;
    for (const FMigrationRetainedStructure& Snapshot : RetainedStructures)
    {
        ChangedAssets.Add(Snapshot.Structure.Get());
    }
    for (const FMigrationBlueprint& Snapshot : Blueprints)
    {
        FBlueprintCompilationManager::QueueForCompilation(Snapshot.Blueprint.Get());
    }
    for (UBlueprint* Blueprint : ChangedBlueprints)
    {
        if (!ConvertedBlueprints.Contains(Blueprint))
        {
            FBlueprintCompilationManager::QueueForCompilation(Blueprint);
        }
    }
    UE_LOG(LogTemp, Display, TEXT("Compiling the prepared migration batch: %d converted Blueprints."), Blueprints.Num());
    FlushMigrationComponentUpdates();
    FBlueprintCompilationManager::FlushCompilationQueueAndReinstance(true);
    // Reference replacement also visits the world's native component update queues.
    FlushMigrationComponentUpdates();
    for (const FMigrationBlueprint& Snapshot : Blueprints)
    {
        UBlueprint* Blueprint = Snapshot.Blueprint.Get();
        if (Blueprint->Status == BS_Error)
        {
            Error = TEXT("Converted Blueprint failed to compile: ") + Blueprint->GetPathName();
            if (!bContinueOnError)
            {
                return false;
            }
            RecordMigrationFailure(Blueprint->GetPathName(), TEXT("Converted Blueprint compilation"), Error);
            FCompilerResultsLog CompilationResults;
            FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &CompilationResults);
            for (const TSharedRef<FTokenizedMessage>& Message : CompilationResults.Messages)
            {
                if (Message->GetSeverity() == EMessageSeverity::Error)
                {
                    RecordMigrationFailure(Blueprint->GetPathName(), TEXT("Converted Blueprint compiler diagnostic"), Message->ToText().ToString());
                }
            }
            continue;
        }
        RegisterSourceAliases(Blueprint, Snapshot.NativeClass.Get());
        ReferenceMappings.Add(Blueprint->GeneratedClass.Get(), Snapshot.NativeClass.Get());
        Values.AddObjectMapping(Blueprint->GeneratedClass.Get(), Snapshot.NativeClass.Get());
        UObject* DefaultObject = Blueprint->GeneratedClass->GetDefaultObject();
        Values.MapObject(Snapshot.DefaultObject, DefaultObject);
        Values.MapDefaultSubobjects(Snapshot.DefaultObject, DefaultObject);
        TArray<FName> ComponentNames;
        const TArray<UActorComponent*> Components = UBlueprintNativizationLibrary::GetClassComponentTemplates(
            ComponentNames, Blueprint->GeneratedClass.Get(), UActorComponent::StaticClass(), true);
        for (int32 Index = 0; Index < Components.Num(); ++Index)
        {
            if (UObject* const* Source = Snapshot.Components.Find(ComponentNames[Index]))
            {
                Values.AddObjectMapping(*Source, Components[Index]);
            }
        }
        for (const TPair<FName, UObject*>& Component : Snapshot.Components)
        {
            if (!Values.ObjectMappings.Contains(Component.Value) || Snapshot.ConstructionTemplateMembers.Contains(Component.Key))
            {
                if (FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(DefaultObject->GetClass(), Component.Key))
                {
                    UObject* Destination = Property->GetObjectPropertyValue_InContainer(DefaultObject);
                    if (Snapshot.ConstructionTemplateMembers.Contains(Component.Key))
                    {
                        UClass* TemplateClass = Values.Objects.FindChecked(Component.Value).DestinationClass.Get();
                        const FName TemplateName = Snapshot.ConstructionTemplateNames.FindChecked(Component.Key);
                        UE_LOG(LogTemp, Display, TEXT("Migration construction template: asset '%s'; member '%s'; source template '%s'; expected class '%s'."),
                            *Blueprint->GetPathName(), *Component.Key.ToString(), *TemplateName.ToString(), *TemplateClass->GetPathName());
                        UObject* Existing = StaticFindObjectFast(nullptr, Blueprint->GeneratedClass.Get(), TemplateName);
                        Destination = nullptr;
                        if (IsValid(Existing) && Existing->GetClass() == TemplateClass &&
                            !Existing->GetClass()->HasAnyClassFlags(CLASS_NewerVersionExists))
                        {
                            Destination = Existing;
                        }
                        else
                        {
                            if (Existing)
                            {
                                if (UActorComponent* ExistingComponent = Cast<UActorComponent>(Existing))
                                {
                                    Blueprint->ComponentTemplates.Remove(ExistingComponent);
                                    CastChecked<UBlueprintGeneratedClass>(Blueprint->GeneratedClass.Get())->ComponentTemplates.Remove(ExistingComponent);
                                }
                                const FName PreservedName = MakeUniqueObjectName(GetTransientPackage(), Existing->GetClass(), TemplateName);
                                if (!Existing->Rename(*PreservedName.ToString(), GetTransientPackage(),
                                    REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty))
                                {
                                    Error = TEXT("Migration cannot preserve the conflicting construction component template in ") +
                                        Blueprint->GetPathName() + TEXT(": ") + TemplateName.ToString();
                                    if (!bContinueOnError)
                                    {
                                        return false;
                                    }
                                    RecordMigrationFailure(Blueprint->GetPathName(), TEXT("Construction template identity"), Error);
                                    break;
                                }
                            }
                            Destination = NewObject<UActorComponent>(Blueprint->GeneratedClass.Get(), TemplateClass,
                                TemplateName, RF_ArchetypeObject | RF_InheritableComponentTemplate | RF_Public | RF_Transactional);
                        }
                        Destination->SetFlags(RF_ArchetypeObject | RF_InheritableComponentTemplate | RF_Public | RF_Transactional);
                        Property->SetObjectPropertyValue_InContainer(DefaultObject, Destination);
                        Blueprint->ComponentTemplates.AddUnique(CastChecked<UActorComponent>(Destination));
                        CastChecked<UBlueprintGeneratedClass>(Blueprint->GeneratedClass.Get())->ComponentTemplates.AddUnique(
                            CastChecked<UActorComponent>(Destination));
                    }
                    if (Destination)
                    {
                        Values.AddObjectMapping(Component.Value, Destination);
                    }
                }
            }
        }
        if (!Snapshot.ConstructionTemplateMembers.IsEmpty())
        {
            UBlueprintGeneratedClass* GeneratedClass = CastChecked<UBlueprintGeneratedClass>(Blueprint->GeneratedClass.Get());
            if (!Blueprint->SimpleConstructionScript)
            {
                Blueprint->SimpleConstructionScript = NewObject<USimpleConstructionScript>(GeneratedClass, NAME_None, RF_Transactional);
                const TArray<USCS_Node*> Nodes = Blueprint->SimpleConstructionScript->GetAllNodes();
                for (USCS_Node* Node : Nodes)
                {
                    Blueprint->SimpleConstructionScript->RemoveNode(Node, false);
                }
            }
            GeneratedClass->SimpleConstructionScript = Blueprint->SimpleConstructionScript;
        }
        ChangedAssets.Add(Blueprint);
    }
    for (const TPair<UClass*, UClass*>& Replacement : DefaultOnlyClassReplacements)
    {
        RegisterSourceAliases(UBlueprint::GetBlueprintFromClass(Replacement.Key), Replacement.Value, true);
    }
    for (const FMigrationBlueprint& Snapshot : Blueprints)
    {
        UBlueprint* Blueprint = Snapshot.Blueprint.Get();
        if (FailedPackages.Contains(Blueprint->GetOutermost()->GetFName()))
        {
            continue;
        }
        UObject* DefaultObject = Values.ObjectMappings.FindChecked(Snapshot.DefaultObject);
        for (const TPair<FName, UObject*>& Template : Snapshot.InstanceTemplates)
        {
            FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(DefaultObject->GetClass(), Template.Key);
            if (!Property)
            {
                Error = TEXT("Migration cannot resolve the destination instance template member: ") +
                    Blueprint->GetPathName() + TEXT(":") + Template.Key.ToString();
                if (!bContinueOnError)
                {
                    return false;
                }
                RecordMigrationFailure(Blueprint->GetPathName(), TEXT("Instance template member"), Error);
                continue;
            }
            UClass* TemplateClass = Values.Objects.FindChecked(Template.Value).DestinationClass.Get();
            if (bKeepAssetReferencesInBlueprint)
            {
                if (UBlueprint* TemplateBlueprint = UBlueprint::GetBlueprintFromClass(Template.Value->GetClass()))
                {
                    TemplateClass = TemplateBlueprint->GeneratedClass;
                }
            }
            UObject* Destination = NewObject<UObject>(Blueprint->GeneratedClass.Get(), TemplateClass,
                MakeUniqueObjectName(Blueprint->GeneratedClass.Get(), TemplateClass, Template.Key),
                RF_ArchetypeObject | RF_Public | RF_Transactional);
            Property->SetObjectPropertyValue_InContainer(DefaultObject, Destination);
            Values.AddObjectMapping(Template.Value, Destination);
            Values.DestinationObjects.FindOrAdd(Destination).Reset(Destination);
        }
    }
    TMap<UObject*, TArray<FString>> InstanceInitializationDiagnostics;
    TSet<UObject*> ReconstructedSceneOwners;
    auto ResolveMigrationInstance = [&Values, &Blueprints, &InstanceInitializationDiagnostics, &ReconstructedSceneOwners, &Error](const FMigrationInstance& Instance) -> UObject*
    {
        auto IsCurrentObject = [](UObject* Object) -> bool
        {
            if (!IsValid(Object) || Object->GetClass()->HasAnyClassFlags(CLASS_NewerVersionExists))
            {
                return false;
            }
            if (UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(Object->GetClass()))
            {
                if (Blueprint->GeneratedClass != Object->GetClass())
                {
                    return false;
                }
            }
            if (AActor* Actor = Cast<AActor>(Object))
            {
                if (Actor->IsActorBeingDestroyed())
                {
                    return false;
                }
            }
            if (UActorComponent* Component = Cast<UActorComponent>(Object))
            {
                if (Component->IsBeingDestroyed())
                {
                    return false;
                }
            }
            return true;
        };
        UObject* Destination = nullptr;
        UObject* ResolvedOwner = nullptr;
        FString ResolutionDetails;
        UClass* ExpectedClass = Values.Objects.FindChecked(Instance.Source.Get()).DestinationClass.Get()->GetAuthoritativeClass();
        if (!Instance.ChildActorOwners.IsEmpty())
        {
            AActor* CurrentActor = FindObject<AActor>(nullptr, *Instance.ChildActorOwners.Last().ActorPath);
            for (int32 HierarchyIndex = Instance.ChildActorOwners.Num() - 1; HierarchyIndex >= 0; --HierarchyIndex)
            {
                const FMigrationChildActorOwner& Parent = Instance.ChildActorOwners[HierarchyIndex];
                UClass* ExpectedOwnerClass = Values.Objects.FindChecked(Parent.Actor.Get()).DestinationClass.Get()->GetAuthoritativeClass();
                if (!IsCurrentObject(CurrentActor) || !CurrentActor->IsA(ExpectedOwnerClass))
                {
                    CurrentActor = nullptr;
                    break;
                }
                Values.MapObject(Parent.Actor.Get(), CurrentActor);
                UClass* ExpectedComponentClass = Values.Objects.FindChecked(Parent.Component.Get()).DestinationClass.Get()->GetAuthoritativeClass();
                auto IsCurrentParentComponent = [&IsCurrentObject, CurrentActor, ExpectedComponentClass](UObject* Candidate) -> bool
                {
                    if (!IsCurrentObject(Candidate) || !Candidate->IsA(ExpectedComponentClass))
                    {
                        return false;
                    }
                    UChildActorComponent* Component = Cast<UChildActorComponent>(Candidate);
                    return Component && Component->GetOwner() == CurrentActor && CurrentActor->GetComponents().Contains(Component);
                };
                FObjectPropertyBase* Property = nullptr;
                UObject* ParentComponent = nullptr;
                if (!Parent.ComponentMember.IsNone())
                {
                    Property = FindFProperty<FObjectPropertyBase>(CurrentActor->GetClass(), Parent.ComponentMember);
                    if (Property)
                    {
                        ParentComponent = Property->GetObjectPropertyValue_InContainer(CurrentActor);
                    }
                    if (!IsCurrentParentComponent(ParentComponent))
                    {
                        ParentComponent = CurrentActor->GetDefaultSubobjectByName(Parent.ComponentMember);
                    }
                }
                if (!IsCurrentParentComponent(ParentComponent))
                {
                    ParentComponent = FindObject<UObject>(CurrentActor, *Parent.ComponentName.ToString());
                }
                if (!IsCurrentParentComponent(ParentComponent))
                {
                    CurrentActor = nullptr;
                    break;
                }
                if (Property)
                {
                    Property->SetObjectPropertyValue_InContainer(CurrentActor, ParentComponent);
                }
                Values.MapObject(Parent.Component.Get(), ParentComponent);
                ResolvedOwner = CurrentActor;
                UChildActorComponent* ChildComponent = CastChecked<UChildActorComponent>(ParentComponent);
                UClass* ExpectedChildClass = ExpectedClass;
                if (HierarchyIndex > 0)
                {
                    ExpectedChildClass = Values.Objects.FindChecked(Instance.ChildActorOwners[HierarchyIndex - 1].Actor.Get()).DestinationClass.Get()->GetAuthoritativeClass();
                }
                UClass* ConfiguredChildClass = ChildComponent->GetChildActorClass();
                if (!ChildComponent->GetChildActor() && ReconstructedSceneOwners.Contains(Parent.Actor.Get()) &&
                    IsValid(ConfiguredChildClass) && ConfiguredChildClass->IsChildOf(ExpectedChildClass) && ChildComponent->GetWorld())
                {
                    FEditorScriptExecutionGuard ExecutionGuard;
                    ChildComponent->CreateChildActor();
                }
                CurrentActor = ChildComponent->GetChildActor();
                if (!IsCurrentObject(CurrentActor) || !CurrentActor->IsA(ExpectedChildClass) ||
                    CurrentActor->GetParentComponent() != ChildComponent)
                {
                    UClass* CurrentChildClass = nullptr;
                    UChildActorComponent* CurrentParentComponent = nullptr;
                    if (CurrentActor)
                    {
                        CurrentChildClass = CurrentActor->GetClass();
                        CurrentParentComponent = CurrentActor->GetParentComponent();
                    }
                    ResolutionDetails = FString::Format(TEXT(" Child hierarchy: component {0}; configured class {1}; expected class {2}; child {3}; child class {4}; current {5}; parent component {6}; parent reconstructed {7}; component registered {8}."),
                        {GetPathNameSafe(ChildComponent), GetPathNameSafe(ConfiguredChildClass), GetPathNameSafe(ExpectedChildClass),
                            GetPathNameSafe(CurrentActor), GetPathNameSafe(CurrentChildClass),
                            IsCurrentObject(CurrentActor), GetPathNameSafe(CurrentParentComponent),
                            ReconstructedSceneOwners.Contains(Parent.Actor.Get()), ChildComponent->IsRegistered()});
                    CurrentActor = nullptr;
                    break;
                }
            }
            Destination = CurrentActor;
        }
        else if (!Instance.OwnerPath.IsEmpty())
        {
            ResolvedOwner = FindObject<UObject>(nullptr, *Instance.OwnerPath);
            if (!IsCurrentObject(ResolvedOwner))
            {
                ResolvedOwner = Values.ObjectMappings.FindRef(Instance.Owner.Get());
            }
            bool bCurrentOwner = IsCurrentObject(ResolvedOwner);
            if (bCurrentOwner)
            {
                if (const FMigrationObject* OwnerSnapshot = Values.Objects.Find(Instance.Owner.Get()))
                {
                    bCurrentOwner = ResolvedOwner->IsA(OwnerSnapshot->DestinationClass.Get()->GetAuthoritativeClass());
                }
            }
            if (bCurrentOwner)
            {
                Values.MapObject(Instance.Owner.Get(), ResolvedOwner);
                AActor* Actor = CastChecked<AActor>(ResolvedOwner);
                auto IsOwnedDestination = [&IsCurrentObject, Actor, ExpectedClass](UObject* Candidate) -> bool
                {
                    if (!IsCurrentObject(Candidate) || !Candidate->IsA(ExpectedClass))
                    {
                        return false;
                    }
                    UActorComponent* Component = Cast<UActorComponent>(Candidate);
                    return Component && Component->GetOwner() == Actor && Actor->GetComponents().Contains(Component);
                };
                FObjectPropertyBase* ComponentProperty = nullptr;
                if (!Instance.ComponentMember.IsNone())
                {
                    ComponentProperty = FindFProperty<FObjectPropertyBase>(ResolvedOwner->GetClass(), Instance.ComponentMember);
                    if (ComponentProperty)
                    {
                        Destination = ComponentProperty->GetObjectPropertyValue_InContainer(ResolvedOwner);
                    }
                }
                if (!IsOwnedDestination(Destination))
                {
                    Destination = nullptr;
                }
                if (!Destination && !Instance.ComponentOuters.IsEmpty())
                {
                    UObject* CurrentOuter = Actor;
                    for (int32 OuterIndex = Instance.ComponentOuters.Num() - 1; OuterIndex >= 0; --OuterIndex)
                    {
                        UClass* OuterClass = Values.Objects.FindChecked(Instance.ComponentOuters[OuterIndex].Get()).DestinationClass.Get()->GetAuthoritativeClass();
                        UObject* Candidate = nullptr;
                        if (CurrentOuter == Actor && !Instance.ComponentOuterMember.IsNone())
                        {
                            if (FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Actor->GetClass(), Instance.ComponentOuterMember))
                            {
                                Candidate = Property->GetObjectPropertyValue_InContainer(Actor);
                            }
                            if (!IsCurrentObject(Candidate) || !Candidate->IsA(OuterClass))
                            {
                                Candidate = Actor->GetDefaultSubobjectByName(Instance.ComponentOuterMember);
                            }
                        }
                        if (!IsCurrentObject(Candidate) || !Candidate->IsA(OuterClass))
                        {
                            Candidate = FindObject<UObject>(CurrentOuter, *Instance.ComponentOuterNames[OuterIndex].ToString());
                        }
                        UActorComponent* OuterComponent = Cast<UActorComponent>(Candidate);
                        if (!IsCurrentObject(Candidate) || !Candidate->IsA(OuterClass) || !OuterComponent ||
                            OuterComponent->GetOwner() != Actor || !Actor->GetComponents().Contains(OuterComponent))
                        {
                            CurrentOuter = nullptr;
                            break;
                        }
                        Values.MapObject(Instance.ComponentOuters[OuterIndex].Get(), Candidate);
                        CurrentOuter = Candidate;
                    }
                    if (CurrentOuter)
                    {
                        Destination = FindObject<UObject>(CurrentOuter, *Instance.ComponentName.ToString());
                        if (!IsOwnedDestination(Destination))
                        {
                            Destination = nullptr;
                        }
                    }
                }
                if (!Destination && Instance.bConstructionComponent && !Instance.ComponentTemplateMember.IsNone())
                {
                    FObjectPropertyBase* TemplateProperty = FindFProperty<FObjectPropertyBase>(ResolvedOwner->GetClass(),
                        Instance.ComponentTemplateMember);
                    UActorComponent* Template = nullptr;
                    if (TemplateProperty)
                    {
                        Template = Cast<UActorComponent>(TemplateProperty->GetObjectPropertyValue_InContainer(ResolvedOwner));
                    }
                    if (Template && Template->GetFName() == Instance.ComponentTemplateName)
                    {
                        TArray<UActorComponent*> TemplateComponents;
                        for (UActorComponent* Component : Actor->GetComponents())
                        {
                            if (IsOwnedDestination(Component) &&
                                Component->CreationMethod == EComponentCreationMethod::UserConstructionScript &&
                                Component->GetFName().GetComparisonIndex() == Template->GetFName().GetComparisonIndex())
                            {
                                TemplateComponents.Add(Component);
                            }
                        }
                        TemplateComponents.Sort([](const UActorComponent& First, const UActorComponent& Second)
                        {
                            return First.GetFName().GetNumber() < Second.GetFName().GetNumber();
                        });
                        if (TemplateComponents.IsValidIndex(Instance.ComponentTemplateOrdinal))
                        {
                            Destination = TemplateComponents[Instance.ComponentTemplateOrdinal];
                        }
                        ResolutionDetails = FString::Format(TEXT(" Construction identity: current template {0}; expected template name {1}; ordinal {2}; matching components {3}."),
                            {GetPathNameSafe(Template), Instance.ComponentTemplateName.ToString(), Instance.ComponentTemplateOrdinal, TemplateComponents.Num()});
                    }
                    else
                    {
                        ResolutionDetails = FString::Format(TEXT(" Construction identity: current template {0}; current name {1}; expected name {2}."),
                            {GetPathNameSafe(Template), GetNameSafe(Template), Instance.ComponentTemplateName.ToString()});
                    }
                }
                if (!Destination)
                {
                    if (!Instance.ComponentMember.IsNone())
                    {
                        Destination = Actor->GetDefaultSubobjectByName(Instance.ComponentMember);
                        if (!IsOwnedDestination(Destination))
                        {
                            Destination = nullptr;
                        }
                        if (!Destination && ComponentProperty)
                        {
                            UActorComponent* RuntimeComponent = Cast<UActorComponent>(
                                ComponentProperty->GetObjectPropertyValue_InContainer(Actor));
                            if (IsOwnedDestination(RuntimeComponent) && RuntimeComponent->CreationMethod == EComponentCreationMethod::Native &&
                                RuntimeComponent->IsA(ComponentProperty->PropertyClass) && Actor->GetComponents().Contains(RuntimeComponent))
                            {
                                Destination = RuntimeComponent;
                            }
                        }
                    }
                }
                if (!Destination && !Instance.bConstructionComponent)
                {
                    Destination = FindObject<UObject>(ResolvedOwner, *Instance.ComponentName.ToString());
                }
                if (ComponentProperty && IsOwnedDestination(Destination))
                {
                    ComponentProperty->SetObjectPropertyValue_InContainer(Actor, Destination);
                }
            }
        }
        else
        {
            Destination = FindObject<UObject>(nullptr, *Instance.Path);
        }
        bool bCurrentDestination = IsCurrentObject(Destination);
        if (bCurrentDestination && ResolvedOwner)
        {
            if (UActorComponent* Component = Cast<UActorComponent>(Destination))
            {
                AActor* Owner = Cast<AActor>(ResolvedOwner);
                bCurrentDestination = Owner && Component->GetOwner() == Owner && Owner->GetComponents().Contains(Component);
            }
        }
        if (bCurrentDestination && Destination->IsA(ExpectedClass))
        {
            return Destination;
        }
        UClass* ResolvedClass = nullptr;
        if (Destination)
        {
            ResolvedClass = Destination->GetClass();
        }
        UClass* ResolvedOwnerClass = nullptr;
        if (ResolvedOwner)
        {
            ResolvedOwnerClass = ResolvedOwner->GetClass();
        }
        FString ChildActorOwnerPath;
        FName ChildActorComponentName;
        if (!Instance.ChildActorOwners.IsEmpty())
        {
            ChildActorOwnerPath = Instance.ChildActorOwners.Last().ActorPath;
            ChildActorComponentName = Instance.ChildActorOwners[0].ComponentName;
        }
        bool bNativeSubobjectExists = false;
        bool bCurrentNativeSubobject = false;
        bool bOwnedNativeSubobject = false;
        int32 NativeCreationMethod = INDEX_NONE;
        if (AActor* Actor = Cast<AActor>(ResolvedOwner))
        {
            if (!Instance.ComponentMember.IsNone())
            {
                UObject* NativeSubobject = Actor->GetDefaultSubobjectByName(Instance.ComponentMember);
                bNativeSubobjectExists = NativeSubobject != nullptr;
                bCurrentNativeSubobject = IsCurrentObject(NativeSubobject);
                if (bCurrentNativeSubobject)
                {
                    if (UActorComponent* Component = Cast<UActorComponent>(NativeSubobject))
                    {
                        bOwnedNativeSubobject = Component->GetOwner() == Actor && Actor->GetComponents().Contains(Component);
                        NativeCreationMethod = static_cast<int32>(Component->CreationMethod);
                    }
                }
            }
        }
        Error = FString::Format(TEXT("Migration could not resolve reinstanced object: {0}. Owner: {1}; member: {2}; expected class: {3}; resolved object: {4}; resolved class: {5}; owner path: {6}; resolved owner: {7}; resolved owner class: {8}; component template: {9}; template member: {10}; original serialization index: {11}; child actor root owner: {12}; child actor component: {13}; native default subobject exists: {14}; current: {15}; owned: {16}; native creation method: {17}."),
            {Instance.Path, Instance.OwnerPath, Instance.ComponentMember.ToString(), GetPathNameSafe(ExpectedClass),
                GetPathNameSafe(Destination), GetPathNameSafe(ResolvedClass), Instance.OwnerPath, GetPathNameSafe(ResolvedOwner),
                GetPathNameSafe(ResolvedOwnerClass), Instance.ComponentTemplateName.ToString(),
                Instance.ComponentTemplateMember.ToString(), Instance.ComponentSerializationIndex, ChildActorOwnerPath,
                ChildActorComponentName.ToString(), bNativeSubobjectExists, bCurrentNativeSubobject,
                bOwnedNativeSubobject, NativeCreationMethod});
        Error += ResolutionDetails;
        if (IsValid(ResolvedOwner))
        {
            Error += FString::Format(TEXT(" Owner flags: {0}; template: {1}; needs load: {2}; needs postload: {3}."),
                {static_cast<uint32>(ResolvedOwner->GetFlags()), ResolvedOwner->IsTemplate(),
                    ResolvedOwner->HasAnyFlags(RF_NeedLoad), ResolvedOwner->HasAnyFlags(RF_NeedPostLoad)});
            UObject* OwnerDefault = ResolvedOwner->GetClass()->GetDefaultObject(false);
            Error += TEXT(" Owner CDO: ") + GetPathNameSafe(OwnerDefault) + TEXT(".");
            TSet<FName> ReportedConfigurationMembers;
            for (const TPair<FString, FString>& Member : Values.MemberNames)
            {
                if (!Member.Key.Contains(TEXT(":RuntimeComponentTemplate:")) &&
                    !(Member.Key.Contains(TEXT(":AssetClass:")) && Member.Key.EndsWith(ExpectedClass->GetPathName())))
                {
                    continue;
                }
                const FName MemberName(*Member.Value);
                FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(ResolvedOwner->GetClass(), MemberName);
                if (!Property || ReportedConfigurationMembers.Contains(MemberName))
                {
                    continue;
                }
                ReportedConfigurationMembers.Add(MemberName);
                UObject* Configuration = Property->GetObjectPropertyValue_InContainer(ResolvedOwner);
                UObject* DefaultConfiguration = nullptr;
                if (OwnerDefault)
                {
                    DefaultConfiguration = Property->GetObjectPropertyValue_InContainer(OwnerDefault);
                }
                UClass* ConfigurationClass = nullptr;
                UClass* DefaultConfigurationClass = nullptr;
                if (Configuration)
                {
                    ConfigurationClass = Configuration->GetClass();
                }
                if (DefaultConfiguration)
                {
                    DefaultConfigurationClass = DefaultConfiguration->GetClass();
                }
                Error += FString::Format(TEXT(" Configuration {0}: instance {1} (class {2}); CDO {3} (class {4})."),
                    {Member.Value, GetPathNameSafe(Configuration), GetPathNameSafe(ConfigurationClass),
                        GetPathNameSafe(DefaultConfiguration), GetPathNameSafe(DefaultConfigurationClass)});
            }
            if (const TArray<FString>* Diagnostics = InstanceInitializationDiagnostics.Find(ResolvedOwner))
            {
                Error += TEXT(" Initializer attempts: ") + FString::Join(*Diagnostics, TEXT("; ")) + TEXT(".");
            }
            else
            {
                Error += TEXT(" Initializer attempts: none.");
            }
            for (UClass* Layer = ResolvedOwner->GetClass(); Layer; Layer = Layer->GetSuperClass())
            {
                for (const FMigrationBlueprint& Snapshot : Blueprints)
                {
                    if (Snapshot.NativeClass.Get() != Layer)
                    {
                        continue;
                    }
                    const FString* InitializerName = Values.MemberNames.Find(Snapshot.SourceClassPath + TEXT(":InstanceInitializer:"));
                    FString InitializerDescription = TEXT("none");
                    bool bInitializerFound = false;
                    if (InitializerName)
                    {
                        InitializerDescription = *InitializerName;
                        bInitializerFound = Layer->FindFunctionByName(FName(**InitializerName)) != nullptr;
                    }
                    Error += FString::Format(TEXT(" Native layer {0}; source {1}; initializer {2}; found {3}."),
                        {Layer->GetPathName(), Snapshot.SourceClassPath, InitializerDescription, bInitializerFound});
                }
            }
        }
        return nullptr;
    };
    TMap<UObject*, const FMigrationInstance*> SceneInstances;
    for (const FMigrationInstance& Instance : Instances)
    {
        SceneInstances.Add(Instance.Source.Get(), &Instance);
    }
    Values.SceneObjectResolver = [&Values, &SceneInstances, &ResolveMigrationInstance](UObject* Source, const FString& ScenePath) -> UObject*
    {
        if (const FMigrationInstance* const* Instance = SceneInstances.Find(Source))
        {
            return ResolveMigrationInstance(**Instance);
        }
        UObject* Destination = FindObject<UObject>(nullptr, *ScenePath);
        if (!IsValid(Destination) || Destination->GetClass()->HasAnyClassFlags(CLASS_NewerVersionExists))
        {
            return nullptr;
        }
        if (UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(Destination->GetClass()))
        {
            if (Blueprint->GeneratedClass != Destination->GetClass())
            {
                return nullptr;
            }
        }
        if (const FMigrationObject* Snapshot = Values.Objects.Find(Source))
        {
            if (!Destination->IsA(Snapshot->DestinationClass.Get()->GetAuthoritativeClass()))
            {
                return nullptr;
            }
        }
        if (const TStrongObjectPtr<UClass>* ExpectedClass = Values.SceneObjectClasses.Find(Source))
        {
            if (!Destination->IsA(ExpectedClass->Get()->GetAuthoritativeClass()))
            {
                return nullptr;
            }
        }
        if (AActor* Actor = Cast<AActor>(Destination))
        {
            if (!Actor->IsActorBeingDestroyed() && Actor->GetLevel() && Actor->GetLevel()->Actors.Contains(Actor))
            {
                return Actor;
            }
        }
        if (UActorComponent* Component = Cast<UActorComponent>(Destination))
        {
            AActor* Actor = Component->GetOwner();
            if (!Component->IsBeingDestroyed() && IsValid(Actor) && !Actor->IsActorBeingDestroyed() &&
                Actor->GetLevel() && Actor->GetLevel()->Actors.Contains(Actor) && Actor->GetComponents().Contains(Component))
            {
                return Component;
            }
        }
        return nullptr;
    };
    auto ApplyConvertedBlueprintDefaults = [&]() -> bool
    {
        for (const FMigrationBlueprint& Snapshot : Blueprints)
        {
            if (FailedPackages.Contains(Snapshot.Blueprint->GetOutermost()->GetFName()))
            {
                continue;
            }
            auto ApplyBlueprintDefaults = [&]() -> bool
            {
                UObject* DefaultObject = Values.ObjectMappings.FindChecked(Snapshot.DefaultObject);
                UObject* CurrentDefaultObject = Snapshot.Blueprint->GeneratedClass->GetDefaultObject();
                if (DefaultObject != CurrentDefaultObject)
                {
                    RegisterSourceAliases(Snapshot.Blueprint.Get(), Snapshot.NativeClass.Get());
                    ReferenceMappings.Add(Snapshot.Blueprint->GeneratedClass.Get(), Snapshot.NativeClass.Get());
                    Values.AddObjectMapping(Snapshot.Blueprint->GeneratedClass.Get(), Snapshot.NativeClass.Get());
                    Values.MapDefaultSubobjects(DefaultObject, CurrentDefaultObject);
                    Values.MapObject(Snapshot.DefaultObject, CurrentDefaultObject);
                    Values.MapDefaultSubobjects(Snapshot.DefaultObject, CurrentDefaultObject);
                    DefaultObject = CurrentDefaultObject;
                    for (const TPair<FName, UObject*>& Component : Snapshot.Components)
                    {
                        UObject* Destination = nullptr;
                        if (FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(DefaultObject->GetClass(), Component.Key))
                        {
                            Destination = Property->GetObjectPropertyValue_InContainer(DefaultObject);
                        }
                        if (!Destination || (!Snapshot.ConstructionTemplateMembers.Contains(Component.Key) && Destination->GetOuter() != DefaultObject))
                        {
                            Destination = DefaultObject->GetDefaultSubobjectByName(Component.Key);
                        }
                        if (Destination)
                        {
                            Values.MapObject(Component.Value, Destination);
                        }
                    }
                }
                TMap<UObject*, UObject*> PreviousComponentMappings;
                TSet<UObject*> PreviouslyUnmappedComponents;
                ON_SCOPE_EXIT
                {
                    for (const TPair<UObject*, UObject*>& Mapping : PreviousComponentMappings)
                    {
                        Values.ObjectMappings.Add(Mapping.Key, Mapping.Value);
                    }
                    for (UObject* Component : PreviouslyUnmappedComponents)
                    {
                        Values.ObjectMappings.Remove(Component);
                    }
                };
                for (const TPair<FName, UObject*>& Component : Snapshot.InheritedComponents)
                {
                    FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(DefaultObject->GetClass(), Component.Key);
                    UObject* Destination = nullptr;
                    if (Property)
                    {
                        Destination = Property->GetObjectPropertyValue_InContainer(DefaultObject);
                    }
                    if (!Destination || Destination->GetOuter() != DefaultObject)
                    {
                        Destination = DefaultObject->GetDefaultSubobjectByName(Component.Key);
                    }
                    if (!Property || !Destination || Destination->GetOuter() != DefaultObject)
                    {
                        Error = TEXT("Migration cannot resolve the owned inherited component in ") +
                            Snapshot.Blueprint->GetPathName() + TEXT(":") + Component.Key.ToString();
                        return false;
                    }
                    Property->SetObjectPropertyValue_InContainer(DefaultObject, Destination);
                    if (UObject* const* PreviousDestination = Values.ObjectMappings.Find(Component.Value))
                    {
                        PreviousComponentMappings.Add(Component.Value, *PreviousDestination);
                    }
                    else
                    {
                        PreviouslyUnmappedComponents.Add(Component.Value);
                    }
                    Values.AddObjectMapping(Component.Value, Destination);
                }
                for (const TPair<FName, UObject*>& Template : Snapshot.InstanceTemplates)
                {
                    FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(DefaultObject->GetClass(), Template.Key);
                    UObject* Destination = nullptr;
                    if (Property)
                    {
                        Destination = Property->GetObjectPropertyValue_InContainer(DefaultObject);
                        UClass* TemplateClass = Values.Objects.FindChecked(Template.Value).DestinationClass.Get();
                        if (bKeepAssetReferencesInBlueprint)
                        {
                            if (UBlueprint* TemplateBlueprint = UBlueprint::GetBlueprintFromClass(Template.Value->GetClass()))
                            {
                                TemplateClass = TemplateBlueprint->GeneratedClass;
                            }
                        }
                        if (!Destination || Destination->GetOuter() != Snapshot.Blueprint->GeneratedClass.Get() ||
                            Destination->GetClass() != TemplateClass)
                        {
                            UObject* PreviousDestination = Values.ObjectMappings.FindRef(Template.Value);
                            Destination = NewObject<UObject>(Snapshot.Blueprint->GeneratedClass.Get(), TemplateClass,
                                MakeUniqueObjectName(Snapshot.Blueprint->GeneratedClass.Get(), TemplateClass, Template.Key),
                                RF_ArchetypeObject | RF_Public | RF_Transactional);
                            Property->SetObjectPropertyValue_InContainer(DefaultObject, Destination);
                            if (PreviousDestination)
                            {
                                Values.MapDefaultSubobjects(PreviousDestination, Destination);
                            }
                            Values.MapObject(Template.Value, Destination);
                        }
                    }
                    if (!Destination)
                    {
                        Error = TEXT("Migration cannot resolve the destination instance configuration in ") +
                            Snapshot.Blueprint->GetPathName() + TEXT(": ") + Template.Key.ToString();
                        return false;
                    }
                    Values.AddObjectMapping(Template.Value, Destination);
                    Values.DestinationObjects.FindOrAdd(Destination).Reset(Destination);
                    if (!Values.ApplyStructure(Destination->GetClass(), Destination,
                        Values.Objects.FindChecked(Template.Value).Properties, Destination))
                    {
                        Error = Values.Error;
                        return false;
                    }
                }
                for (FName Member : Snapshot.ConstructionTemplateMembers)
                {
                    FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(DefaultObject->GetClass(), Member);
                    UObject* Template = nullptr;
                    if (Property)
                    {
                        Template = Property->GetObjectPropertyValue_InContainer(DefaultObject);
                    }
                    if (!Template)
                    {
                        Error = TEXT("Migration cannot resolve the destination construction component template in ") +
                            Snapshot.Blueprint->GetPathName() + TEXT(": ") + Member.ToString();
                        return false;
                    }
                    Values.AddObjectMapping(Snapshot.Components.FindChecked(Member), Template);
                }
                if (!Values.ApplyStructure(DefaultObject->GetClass(), DefaultObject,
                    Values.Objects.FindChecked(Snapshot.DefaultObject).Properties, DefaultObject))
                {
                    Error = Values.Error;
                    return false;
                }
                for (const TPair<FName, UObject*>& Component : Snapshot.Components)
                {
                    if (UObject* const* Destination = Values.ObjectMappings.Find(Component.Value))
                    {
                        if (!Values.ApplyStructure((*Destination)->GetClass(), *Destination,
                            Values.Objects.FindChecked(Component.Value).Properties, *Destination))
                        {
                            Error = Values.Error;
                            return false;
                        }
                        if (UActorComponent* UpdatedComponent = Cast<UActorComponent>(*Destination))
                        {
                            UpdatedComponent->PostApplyToComponent();
                        }
                    }
                    else
                    {
                        Error = TEXT("Migration cannot resolve component ") + Component.Key.ToString() + TEXT(" in ") + Snapshot.Blueprint->GetPathName();
                        return false;
                    }
                }
                for (const TPair<FName, UObject*>& Component : Snapshot.InheritedComponents)
                {
                    UObject* Destination = Values.ObjectMappings.FindChecked(Component.Value);
                    if (!Values.ApplyStructure(Destination->GetClass(), Destination,
                        Values.Objects.FindChecked(Component.Value).Properties, Destination))
                    {
                        Error = Values.Error;
                        return false;
                    }
                    CastChecked<UActorComponent>(Destination)->PostApplyToComponent();
                }
                if (AActor* DefaultActor = Cast<AActor>(DefaultObject))
                {
                    if (USceneComponent* RootComponent = DefaultActor->GetRootComponent())
                    {
                        if (RootComponent->GetOwner() != DefaultActor)
                        {
                            Error = TEXT("Migrated root component belongs to a different actor: ") +
                                DefaultActor->GetPathName() + TEXT(" -> ") + RootComponent->GetPathName();
                            return false;
                        }
                    }
                }
                if (bKeepAssetReferencesInBlueprint)
                {
                    if (const FString* BridgeName = MemberNames.Find(Snapshot.SourceClassPath + TEXT(":LibraryAssetBridge:")))
                    {
                        UFunction* Bridge = DefaultObject->FindFunction(FName(**BridgeName));
                        if (!Bridge)
                        {
                            Error = TEXT("Migration cannot resolve the Blueprint library asset bridge in ") + Snapshot.Blueprint->GetPathName();
                            return false;
                        }
                        DefaultObject->ProcessEvent(Bridge, nullptr);
                    }
                }
                return true;
            };
            if (!ApplyBlueprintDefaults())
            {
                if (!bContinueOnError)
                {
                    return false;
                }
                RecordMigrationFailure(Snapshot.Blueprint->GetPathName(), TEXT("Converted Blueprint defaults and components"), Error);
            }
        }
        return true;
    };
    if (!ApplyConvertedBlueprintDefaults())
    {
        return false;
    }
    TSet<FName> ConfigurationMembers;
    TSet<FName> AssetConfigurationMembers;
    for (const TPair<FString, FString>& Member : MemberNames)
    {
        if (Member.Key.Contains(TEXT(":AssetClass:")) || Member.Key.Contains(TEXT(":AssetObject:")))
        {
            AssetConfigurationMembers.Add(FName(*Member.Value));
        }
        if (Member.Key.Contains(TEXT(":AssetClass:")) || Member.Key.Contains(TEXT(":AssetObject:")) ||
            Member.Key.Contains(TEXT(":RuntimeComponentTemplate:")) || Member.Key.Contains(TEXT(":ComponentTemplate:")) ||
            Member.Key.Contains(TEXT(":InstancePropertyTemplate:")) || Member.Key.Contains(TEXT(":ComponentInstanceTemplate:")))
        {
            ConfigurationMembers.Add(FName(*Member.Value));
        }
    }
    TMap<UClass*, UObject*> GeneratedAssetDefaults;
    if (bKeepAssetReferencesInBlueprint)
    {
        for (const FMigrationBlueprint& Snapshot : Blueprints)
        {
            GeneratedAssetDefaults.Add(Snapshot.NativeClass.Get(), Snapshot.Blueprint->GeneratedClass->GetDefaultObject());
        }
    }
    auto CopyConfiguration = [&ConfigurationMembers, &AssetConfigurationMembers, &GeneratedAssetDefaults](UObject* Object,
        const TArray<FMigrationValue>* OriginalProperties = nullptr) -> bool
    {
        bool bChanged = false;
        UObject* DefaultObject = Object->GetClass()->GetDefaultObject();
        UObject* AssetDefaults = GeneratedAssetDefaults.FindRef(Object->GetClass());
        for (FName Member : ConfigurationMembers)
        {
            if (OriginalProperties && OriginalProperties->ContainsByPredicate([Member](const FMigrationValue& Property)
            {
                return Property.Name == Member.ToString();
            }))
            {
                continue;
            }
            if (FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Object->GetClass(), Member))
            {
                UObject* Configuration = DefaultObject;
                if (AssetDefaults && AssetConfigurationMembers.Contains(Member))
                {
                    Configuration = AssetDefaults;
                }
                if (!Property->Identical_InContainer(Object, Configuration))
                {
                    Property->CopyCompleteValue_InContainer(Object, Configuration);
                    bChanged = true;
                }
            }
        }
        return bChanged;
    };
    auto InitializeConfiguredInstance = [&Blueprints, &MemberNames, &CopyConfiguration, &InstanceInitializationDiagnostics, &Error](UObject* Object) -> bool
    {
        if (!IsValid(Object))
        {
            return true;
        }
        TArray<FString>& Diagnostics = InstanceInitializationDiagnostics.FindOrAdd(Object);
        if (Object->IsTemplate() || Object->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad))
        {
            Diagnostics.AddUnique(FString::Format(TEXT("skipped: template {0}, needs load {1}, needs postload {2}"),
                {Object->IsTemplate(), Object->HasAnyFlags(RF_NeedLoad), Object->HasAnyFlags(RF_NeedPostLoad)}));
            return true;
        }
        CopyConfiguration(Object);
        TArray<UClass*> NativeLayers;
        for (UClass* Layer = Object->GetClass(); Layer; Layer = Layer->GetSuperClass())
        {
            NativeLayers.Add(Layer);
        }
        bool bMatchedNativeLayer = false;
        for (int32 LayerIndex = NativeLayers.Num() - 1; LayerIndex >= 0; --LayerIndex)
        {
            for (const FMigrationBlueprint& Snapshot : Blueprints)
            {
                if (Snapshot.NativeClass.Get() != NativeLayers[LayerIndex])
                {
                    continue;
                }
                bMatchedNativeLayer = true;
                const FString* InitializerName = MemberNames.Find(Snapshot.SourceClassPath + TEXT(":InstanceInitializer:"));
                if (!InitializerName)
                {
                    Diagnostics.AddUnique(TEXT("no cached initializer for ") + Snapshot.SourceClassPath + TEXT(" on ") + NativeLayers[LayerIndex]->GetPathName());
                    continue;
                }
                UFunction* Initializer = NativeLayers[LayerIndex]->FindFunctionByName(FName(**InitializerName));
                if (!Initializer || Initializer->NumParms != 0 || !Initializer->HasAnyFunctionFlags(FUNC_Native) ||
                    Initializer->GetOwnerClass() != NativeLayers[LayerIndex])
                {
                    Diagnostics.AddUnique(TEXT("unavailable initializer ") + Snapshot.SourceClassPath + TEXT(":") + *InitializerName);
                    Error = TEXT("Migration cannot resolve the generated instance initializer in ") + Object->GetPathName() + TEXT(": ") + *InitializerName;
                    return false;
                }
                {
                    FEditorScriptExecutionGuard ExecutionGuard;
                    AActor* Actor = Cast<AActor>(Object);
                    if (Actor && !Actor->GetWorld())
                    {
                        Object->UObject::ProcessEvent(Initializer, nullptr);
                    }
                    else
                    {
                        Object->ProcessEvent(Initializer, nullptr);
                    }
                }
                Diagnostics.AddUnique(TEXT("invoked ") + Initializer->GetPathName() + TEXT(" from ") + Snapshot.SourceClassPath);
            }
        }
        if (!bMatchedNativeLayer)
        {
            Diagnostics.AddUnique(TEXT("no captured native layer for ") + Object->GetClass()->GetPathName());
        }
        return true;
    };
    if (bKeepAssetReferencesInBlueprint)
    {
        for (const TPair<UObject*, TStrongObjectPtr<UObject>>& Destination : Values.DestinationObjects)
        {
            UObject* Object = Destination.Value.Get();
            if (!Object->IsTemplate() || Object->HasAnyFlags(RF_ClassDefaultObject) ||
                Object->GetClass()->HasAnyClassFlags(CLASS_NewerVersionExists))
            {
                continue;
            }
            CopyConfiguration(Object);
        }
    }
    TSet<UObject*> InitializedOwners;
    for (const FMigrationInstance& Instance : Instances)
    {
        if (!Instance.ChildActorOwners.IsEmpty() || Instance.bChildActorComponentOwner)
        {
            continue;
        }
        UObject* Owner = nullptr;
        if (Instance.Source->IsA<AActor>())
        {
            Owner = ResolveMigrationInstance(Instance);
        }
        else if (!Instance.OwnerPath.IsEmpty())
        {
            Owner = FindObject<AActor>(nullptr, *Instance.OwnerPath);
        }
        if (!IsValid(Owner) || InitializedOwners.Contains(Owner))
        {
            continue;
        }
        InitializedOwners.Add(Owner);
        if (!InitializeConfiguredInstance(Owner))
        {
            if (!bContinueOnError)
            {
                return false;
            }
            RecordMigrationFailure(Instance.Path, TEXT("Configured instance initialization"), Error);
        }
    }
    for (const FMigrationInstance& Instance : Instances)
    {
        if (Instance.bConstructionComponent || Instance.bChildActorComponentOwner || !Instance.ChildActorOwners.IsEmpty())
        {
            continue;
        }
        UObject* Destination = ResolveMigrationInstance(Instance);
        if (!Destination)
        {
            if (!bContinueOnError)
            {
                return false;
            }
            RecordMigrationFailure(Instance.Path, TEXT("Resolve instance before defaults"), Error);
            Values.ObjectMappings.Remove(Instance.Source.Get());
            continue;
        }
        Values.MapObject(Instance.Source.Get(), Destination);
        if (!InitializeConfiguredInstance(Destination))
        {
            if (!bContinueOnError)
            {
                return false;
            }
            RecordMigrationFailure(Instance.Path, TEXT("Configured instance initialization"), Error);
        }
    }
    for (const FMigrationTable& Snapshot : Tables)
    {
        UDataTable* Table = Snapshot.Table.Get();
        auto ApplyTableRows = [&]() -> bool
        {
            Table->Modify();
            Table->EmptyTable();
            Table->RowStruct = Snapshot.NativeStructure.Get();
            for (const TPair<FName, TArray<FMigrationValue>>& Row : Snapshot.Rows)
            {
                FStructOnScope Memory(Snapshot.NativeStructure.Get());
                if (!Values.ApplyStructure(Snapshot.NativeStructure.Get(), Memory.GetStructMemory(), Row.Value, Table))
                {
                    Error = Values.Error;
                    return false;
                }
                Table->AddRow(Row.Key, Memory.GetStructMemory(), Snapshot.NativeStructure.Get());
            }
            ChangedAssets.Add(Table);
            return true;
        };
        if (!ApplyTableRows())
        {
            if (!bContinueOnError)
            {
                return false;
            }
            RecordMigrationFailure(Table->GetPathName(), TEXT("Data table rows"), Error);
        }
    }
    for (const TPair<UField*, UField*>& Mapping : ReferenceMappings)
    {
        UBlueprintNativizationLibrary::ReplaceReferencesToType(Mapping.Key, Mapping.Value, ChangedBlueprints, true,
            !bKeepAssetReferencesInBlueprint && !DefaultOnlySourceClasses.Contains(Cast<UClass>(Mapping.Key)));
    }
    for (const TStrongObjectPtr<UBlueprint>& Blueprint : DependentBlueprints)
    {
        FMigrationReferenceArchive Archive(Blueprint.Get(), Values.ObjectMappings, Values.SoftMappings, DefaultOnlySourceClasses, bKeepAssetReferencesInBlueprint);
        if (Archive.GetCount() > 0 || Archive.bSoftReferencesChanged)
        {
            ChangedBlueprints.AddUnique(Blueprint.Get());
        }
    }
    for (const TStrongObjectPtr<UObject>& Asset : LoadedAssets)
    {
        UObject* Object = Asset.Get();
        if (!Object || Object->IsA<UBlueprint>() || Object->IsA<UField>() || Object->IsA<UDataTable>())
        {
            continue;
        }
        auto UpdateEmbeddedObjects = [&]() -> bool
        {
            Object->Modify(false);
            for (const FMigrationInstance& Instance : EmbeddedObjects)
            {
                UObject* Source = Instance.Source.Get();
                if (Source->GetOutermost() != Object->GetOutermost())
                {
                    continue;
                }
                UObject* Current = FindObject<UObject>(nullptr, *Instance.Path);
                UObject* Owner = Source->GetOuter();
                if (UObject* const* Replacement = Values.ObjectMappings.Find(Owner))
                {
                    Owner = *Replacement;
                }
                UObject* Destination = Values.ResolveObject(Source, Owner);
                if (!Values.Error.IsEmpty())
                {
                    Error = Values.Error;
                    return false;
                }
                if (Current)
                {
                    Values.MapObject(Current, Destination);
                }
            }
            FlushMigrationComponentUpdates();
            FMigrationReferenceArchive Archive(Object, Values.ObjectMappings, Values.SoftMappings, DefaultOnlySourceClasses, bKeepAssetReferencesInBlueprint);
            if (Archive.GetCount() > 0 || Archive.bSoftReferencesChanged)
            {
                ChangedAssets.Add(Object);
            }
            return true;
        };
        if (!UpdateEmbeddedObjects())
        {
            if (!bContinueOnError)
            {
                return false;
            }
            RecordMigrationFailure(Object->GetPathName(), TEXT("Embedded objects and references"), Error);
        }
    }
    TArray<FString> ValidationErrors;
    int32 ValidatedBlueprints = 0;
    int32 SuccessfulBlueprints = 0;
    for (UBlueprint* Blueprint : ChangedBlueprints)
    {
        if (ConvertedBlueprints.Contains(Blueprint))
        {
            continue;
        }
        ++ValidatedBlueprints;
        FBlueprintEditorUtils::ReconstructAllNodes(Blueprint);
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        if (!RestoreGraphConnections(Blueprint, true))
        {
            ValidationErrors.Add(Error);
            if (bContinueOnError)
            {
                RecordMigrationFailure(Blueprint->GetPathName(), TEXT("Final graph connections"), Error);
            }
            Error.Reset();
            continue;
        }
        TArray<UEdGraph*> Graphs;
        Blueprint->GetAllGraphs(Graphs);
        for (UEdGraph* Graph : Graphs)
        {
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                for (UEdGraphPin* Pin : Node->Pins)
                {
                    UEnum* Enumeration = Cast<UEnum>(Pin->PinType.PinSubCategoryObject.Get());
                    Pin->DefaultValue = Values.RemapEnumValue(Enumeration, Pin->DefaultValue);
                    Pin->AutogeneratedDefaultValue = Values.RemapEnumValue(Enumeration, Pin->AutogeneratedDefaultValue);
                }
            }
        }
        FCompilerResultsLog CompilationResults;
        FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &CompilationResults);
        if (Blueprint->Status == BS_Error)
        {
            ValidationErrors.Add(TEXT("Dependent Blueprint failed to compile: ") + Blueprint->GetPathName());
            for (const TSharedRef<FTokenizedMessage>& Message : CompilationResults.Messages)
            {
                if (Message->GetSeverity() == EMessageSeverity::Error)
                {
                    ValidationErrors.Add(Message->ToText().ToString());
                    if (bContinueOnError)
                    {
                        RecordMigrationFailure(Blueprint->GetPathName(), TEXT("Dependent Blueprint compiler diagnostic"), Message->ToText().ToString());
                    }
                }
            }
            if (bContinueOnError)
            {
                RecordMigrationFailure(Blueprint->GetPathName(), TEXT("Dependent Blueprint compilation"), TEXT("Dependent Blueprint failed to compile."));
            }
            continue;
        }
        ++SuccessfulBlueprints;
        ChangedAssets.Add(Blueprint);
    }
    const FString ValidationSummary = FString::Format(
        TEXT("Migration validation: {0}/{1} dependent Blueprints passed; {2} converted Blueprints prepared; {3} retained structures updated."),
        {SuccessfulBlueprints, ValidatedBlueprints, Blueprints.Num(), RetainedStructures.Num()});
    UE_LOG(LogTemp, Display, TEXT("%s"), *ValidationSummary);
    if (!ValidationErrors.IsEmpty() && !bContinueOnError)
    {
        Error = ValidationSummary + TEXT(" No migrated assets were saved.\n") + FString::Join(ValidationErrors, TEXT("\n"));
        return false;
    }
    if (!ApplyConvertedBlueprintDefaults())
    {
        return false;
    }
    if (bKeepAssetReferencesInBlueprint)
    {
        for (const FMigrationBlueprint& Snapshot : Blueprints)
        {
            GeneratedAssetDefaults.Add(Snapshot.NativeClass.Get(), Snapshot.Blueprint->GeneratedClass->GetDefaultObject());
        }
    }
    for (const TStrongObjectPtr<UBlueprint>& Blueprint : DependentBlueprints)
    {
        if (!ChangedAssets.Contains(Blueprint.Get()))
        {
            continue;
        }
        if (FailedPackages.Contains(Blueprint->GetOutermost()->GetFName()) || Blueprint->Status == BS_Error || !Blueprint->GeneratedClass)
        {
            continue;
        }
        UObject* DefaultObject = Blueprint->GeneratedClass->GetDefaultObject();
        UObject* Original = DependentDefaults.FindChecked(Blueprint.Get());
        Values.MapObject(Original, DefaultObject);
        Values.MapDefaultSubobjects(Original, DefaultObject);
        TMap<UObject*, UObject*> PreviousComponentMappings;
        TSet<UObject*> PreviouslyUnmappedComponents;
        ON_SCOPE_EXIT
        {
            for (const TPair<UObject*, UObject*>& Mapping : PreviousComponentMappings)
            {
                Values.ObjectMappings.Add(Mapping.Key, Mapping.Value);
            }
            for (UObject* Component : PreviouslyUnmappedComponents)
            {
                Values.ObjectMappings.Remove(Component);
            }
        };
        const TMap<FName, UObject*>* InheritedComponents = DependentInheritedComponents.Find(Blueprint.Get());
        if (InheritedComponents)
        {
            for (const TPair<FName, UObject*>& Component : *InheritedComponents)
            {
                FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(DefaultObject->GetClass(), Component.Key);
                UObject* Destination = nullptr;
                if (Property)
                {
                    Destination = Property->GetObjectPropertyValue_InContainer(DefaultObject);
                }
                if (!Destination || Destination->GetOuter() != DefaultObject)
                {
                    Destination = DefaultObject->GetDefaultSubobjectByName(Component.Key);
                }
                if (!Property || !Destination || Destination->GetOuter() != DefaultObject)
                {
                    Error = TEXT("Migration cannot resolve the owned inherited component in dependent Blueprint: ") +
                        Blueprint->GetPathName() + TEXT(":") + Component.Key.ToString();
                    return false;
                }
                Property->SetObjectPropertyValue_InContainer(DefaultObject, Destination);
                if (UObject* const* PreviousDestination = Values.ObjectMappings.Find(Component.Value))
                {
                    PreviousComponentMappings.Add(Component.Value, *PreviousDestination);
                }
                else
                {
                    PreviouslyUnmappedComponents.Add(Component.Value);
                }
                Values.AddObjectMapping(Component.Value, Destination);
            }
        }
        if (AActor* Actor = Cast<AActor>(DefaultObject))
        {
            if (Actor->GetRootComponent())
            {
                Values.Objects.FindChecked(Original).Properties.RemoveAll([](const FMigrationValue& Property)
                {
                    return Property.Name == TEXT("RootComponent") && Property.Kind == EMigrationValueKind::Object && !Property.Object.IsValid();
                });
            }
        }
        if (!Values.ValidateStructure(DefaultObject->GetClass(), Values.Objects.FindChecked(Original).Properties) ||
            !Values.ApplyStructure(DefaultObject->GetClass(), DefaultObject, Values.Objects.FindChecked(Original).Properties, DefaultObject))
        {
            Error = Values.Error;
            if (!bContinueOnError)
            {
                return false;
            }
            RecordMigrationFailure(Blueprint->GetPathName(), TEXT("Dependent Blueprint defaults"), Error);
        }
        if (InheritedComponents)
        {
            for (const TPair<FName, UObject*>& Component : *InheritedComponents)
            {
                UObject* Destination = Values.ObjectMappings.FindChecked(Component.Value);
                if (!Values.ApplyStructure(Destination->GetClass(), Destination,
                    Values.Objects.FindChecked(Component.Value).Properties, Destination))
                {
                    Error = Values.Error;
                    return false;
                }
                CopyConfiguration(Destination);
                CastChecked<UActorComponent>(Destination)->PostApplyToComponent();
            }
        }
        if (UBlueprint* ParentBlueprint = DependentConfigurationParents.FindRef(Blueprint.Get()))
        {
            UObject* ParentDefaults = ParentBlueprint->GeneratedClass->GetDefaultObject();
            const TArray<FMigrationValue>& OriginalProperties = Values.Objects.FindChecked(Original).Properties;
            for (FName Member : ConfigurationMembers)
            {
                if (OriginalProperties.ContainsByPredicate([Member](const FMigrationValue& Property)
                {
                    return Property.Name == Member.ToString();
                }))
                {
                    continue;
                }
                FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(ParentDefaults->GetClass(), Member);
                if (Property && DefaultObject->IsA(Property->GetOwnerClass()))
                {
                    Property->CopyCompleteValue_InContainer(DefaultObject, ParentDefaults);
                }
            }
        }
    }
    TSet<UObject*> ConstructionComponents;
    TSet<UObject*> ConstructionOwners;
    for (const FMigrationInstance& Instance : Instances)
    {
        if (Instance.Owner.IsValid())
        {
            ConstructionComponents.Add(Instance.Source.Get());
        }
        if (Instance.bConstructionComponent)
        {
            ConstructionComponents.Add(Instance.Source.Get());
            ConstructionOwners.Add(Instance.Owner.Get());
        }
        if (!Instance.ChildActorOwners.IsEmpty() || Instance.bChildActorComponentOwner)
        {
            ConstructionComponents.Add(Instance.Source.Get());
            for (const FMigrationChildActorOwner& Parent : Instance.ChildActorOwners)
            {
                ConstructionOwners.Add(Parent.Actor.Get());
            }
        }
    }
    TFunction<bool(const FMigrationValue&)> ContainsConstructionComponent;
    ContainsConstructionComponent = [&Values, &ConstructionComponents, &ContainsConstructionComponent](const FMigrationValue& Value)
    {
        if (Value.Kind == EMigrationValueKind::Object &&
            (ConstructionComponents.Contains(Value.Object.Get()) || Values.SceneObjectPaths.Contains(Value.Object.Get())))
        {
            return true;
        }
        for (const FMigrationValue& Child : Value.Children)
        {
            if (ContainsConstructionComponent(Child))
            {
                return true;
            }
        }
        return false;
    };
    TArray<const FMigrationInstance*> ConstructionOwnerInstances;
    for (const FMigrationInstance& Instance : Instances)
    {
        if (ConstructionOwners.Contains(Instance.Source.Get()))
        {
            ConstructionOwnerInstances.Add(&Instance);
        }
    }
    ConstructionOwnerInstances.Sort([](const FMigrationInstance& First, const FMigrationInstance& Second)
    {
        return First.ChildActorOwners.Num() < Second.ChildActorOwners.Num();
    });
    TSet<UObject*> RestoredConstructionComponents;
    TMultiMap<UObject*, const FMigrationInstance*> ConstructionComponentInstances;
    for (const FMigrationInstance& Instance : Instances)
    {
        if (Instance.Owner.IsValid() && !Instance.bConstructionComponent)
        {
            ConstructionComponentInstances.Add(Instance.Owner.Get(), &Instance);
        }
    }
    for (const FMigrationInstance* ConstructionOwnerInstance : ConstructionOwnerInstances)
    {
        const FMigrationInstance& Instance = *ConstructionOwnerInstance;
        AActor* Actor = Cast<AActor>(ResolveMigrationInstance(Instance));
        if (!Actor)
        {
            if (!bContinueOnError)
            {
                return false;
            }
            RecordMigrationFailure(Instance.Path, TEXT("Resolve construction owner"), Error);
            continue;
        }
        Values.MapObject(Instance.Source.Get(), Actor);
        TArray<FMigrationValue> Properties = Values.Objects.FindChecked(Instance.Source.Get()).Properties;
        Properties.RemoveAll([&ContainsConstructionComponent](const FMigrationValue& Property)
        {
            return ContainsConstructionComponent(Property);
        });
        if (!Values.ApplyStructure(Actor->GetClass(), Actor, Properties, Actor))
        {
            Error = Values.Error;
            if (!bContinueOnError)
            {
                return false;
            }
            RecordMigrationFailure(Instance.Path, TEXT("Construction owner defaults"), Error);
            continue;
        }
        if (!InitializeConfiguredInstance(Actor))
        {
            if (!bContinueOnError)
            {
                return false;
            }
            RecordMigrationFailure(Instance.Path, TEXT("Configured construction owner initialization"), Error);
            continue;
        }
        bool bComponentDefaultsRestored = true;
        TArray<const FMigrationInstance*> ComponentInstances;
        ConstructionComponentInstances.MultiFind(Instance.Source.Get(), ComponentInstances);
        for (const FMigrationInstance* ComponentInstanceEntry : ComponentInstances)
        {
            const FMigrationInstance& ComponentInstance = *ComponentInstanceEntry;
            UObject* Component = ResolveMigrationInstance(ComponentInstance);
            if (!Component)
            {
                if (!bContinueOnError)
                {
                    return false;
                }
                RecordMigrationFailure(ComponentInstance.Path, TEXT("Resolve component before construction"), Error);
                bComponentDefaultsRestored = false;
                continue;
            }
            Values.MapObject(ComponentInstance.Source.Get(), Component);
            TArray<FMigrationValue> ComponentProperties = Values.Objects.FindChecked(ComponentInstance.Source.Get()).Properties;
            ComponentProperties.RemoveAll([&ContainsConstructionComponent](const FMigrationValue& Property)
            {
                return ContainsConstructionComponent(Property);
            });
            if (!Values.ApplyStructure(Component->GetClass(), Component, ComponentProperties, Component))
            {
                Error = Values.Error;
                if (!bContinueOnError)
                {
                    return false;
                }
                RecordMigrationFailure(ComponentInstance.Path, TEXT("Component defaults before construction"), Error);
                bComponentDefaultsRestored = false;
                continue;
            }
            if (!InitializeConfiguredInstance(Component))
            {
                if (!bContinueOnError)
                {
                    return false;
                }
                RecordMigrationFailure(ComponentInstance.Path, TEXT("Configured component initialization before construction"), Error);
                bComponentDefaultsRestored = false;
                continue;
            }
            if (ComponentInstance.ComponentData.IsValid())
            {
                ComponentInstance.ComponentData->FindAndReplaceInstances(Values.ObjectMappings);
                ComponentInstance.ComponentData->ApplyToComponent(CastChecked<UActorComponent>(Component), ECacheApplyPhase::PostSimpleConstructionScript);
            }
            CastChecked<UActorComponent>(Component)->PostApplyToComponent();
            RestoredConstructionComponents.Add(ComponentInstance.Source.Get());
        }
        if (!bComponentDefaultsRestored)
        {
            continue;
        }
        if (!InitializeConfiguredInstance(Actor))
        {
            if (!bContinueOnError)
            {
                return false;
            }
            RecordMigrationFailure(Instance.Path, TEXT("Configured owner initialization after component defaults"), Error);
            continue;
        }
        for (const FMigrationBlueprint& Snapshot : Blueprints)
        {
            if (!Actor->IsA(Snapshot.NativeClass.Get()))
            {
                continue;
            }
            UObject* DefaultObject = Actor->GetClass()->GetDefaultObject();
            for (FName Member : Snapshot.ConstructionTemplateMembers)
            {
                FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Actor->GetClass(), Member);
                if (Property)
                {
                    Property->SetObjectPropertyValue_InContainer(Actor, Property->GetObjectPropertyValue_InContainer(DefaultObject));
                }
            }
        }
        UClass* NativeOwnerClass = Values.Objects.FindChecked(Instance.Source.Get()).DestinationClass.Get()->GetAuthoritativeClass();
        while (NativeOwnerClass->HasAnyClassFlags(CLASS_CompiledFromBlueprint))
        {
            NativeOwnerClass = NativeOwnerClass->GetSuperClass();
        }
        TArray<UObject*> NativeSubobjects;
        NativeOwnerClass->GetDefaultObject()->GetDefaultSubobjects(NativeSubobjects);
        for (UObject* NativeSubobject : NativeSubobjects)
        {
            UActorComponent* NativeComponent = Cast<UActorComponent>(NativeSubobject);
            if (!NativeComponent || NativeComponent->CreationMethod != EComponentCreationMethod::Native)
            {
                continue;
            }
            UActorComponent* Component = Cast<UActorComponent>(Actor->GetDefaultSubobjectByName(NativeComponent->GetFName()));
            if (IsValid(Component) && !Component->IsBeingDestroyed() && Component->HasAnyFlags(RF_DefaultSubObject) &&
                Component->IsA(NativeComponent->GetClass()) && Component->GetOwner() == Actor && Actor->GetComponents().Contains(Component))
            {
                Component->CreationMethod = EComponentCreationMethod::Native;
            }
        }
        Actor->RerunConstructionScripts();
        ReconstructedSceneOwners.Add(Instance.Source.Get());
    }
    for (const FMigrationInstance& Instance : Instances)
    {
        UObject* Destination = ResolveMigrationInstance(Instance);
        if (!Destination)
        {
            if (!bContinueOnError)
            {
                return false;
            }
            RecordMigrationFailure(Instance.Path, TEXT("Resolve instance after defaults"), Error);
            Values.ObjectMappings.Remove(Instance.Source.Get());
            continue;
        }
        Values.MapObject(Instance.Source.Get(), Destination);
    }
    for (FMigrationInstance& Instance : Instances)
    {
        UObject* Destination = Values.ObjectMappings.FindRef(Instance.Source.Get());
        if (!Destination)
        {
            continue;
        }
        if (Instance.ComponentData.IsValid())
        {
            if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Destination))
            {
                StaticMeshComponent->PostApplyToComponent();
            }
            if (!Instance.ComponentTemplateMember.IsNone())
            {
                AActor* Owner = CastChecked<UActorComponent>(Destination)->GetOwner();
                FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Owner->GetClass(), Instance.ComponentTemplateMember);
                if (Property)
                {
                    UObject* Template = Property->GetObjectPropertyValue_InContainer(Owner);
                    Values.AddObjectMapping(const_cast<UObject*>(Instance.ComponentData->GetComponentTemplate()), Template);
                }
            }
            FArchiveReplaceObjectRef<UObject> Archive(nullptr, Values.ObjectMappings,
                EArchiveReplaceObjectFlags::IgnoreOuterRef | EArchiveReplaceObjectFlags::IgnoreArchetypeRef);
            UScriptStruct* Structure = const_cast<UScriptStruct*>(CastChecked<UScriptStruct>(Instance.ComponentData.GetStruct()));
            Structure->SerializeItem(Archive, Instance.ComponentData.Get(), nullptr);
            Instance.ComponentData->FindAndReplaceInstances(Values.ObjectMappings);
            Instance.ComponentData->ApplyToComponent(CastChecked<UActorComponent>(Destination), ECacheApplyPhase::PostUserConstructionScript);
        }
        TArray<FMigrationValue> Properties = Values.Objects.FindChecked(Instance.Source.Get()).Properties;
        if (Instance.bConstructionComponent && Instance.ComponentData.IsValid())
        {
            Properties.Reset();
        }
        else if (ConstructionOwners.Contains(Instance.Source.Get()) || RestoredConstructionComponents.Contains(Instance.Source.Get()))
        {
            Properties.RemoveAll([&ContainsConstructionComponent](const FMigrationValue& Property)
            {
                return !ContainsConstructionComponent(Property);
            });
        }
        if (!Values.ValidateStructure(Destination->GetClass(), Properties) ||
            !Values.ApplyStructure(Destination->GetClass(), Destination, Properties, Destination))
        {
            Error = Values.Error;
            if (!bContinueOnError)
            {
                return false;
            }
            RecordMigrationFailure(Instance.Path, TEXT("Instance defaults"), Error);
            continue;
        }
        if (!Properties.IsEmpty())
        {
            if (!InitializeConfiguredInstance(Destination))
            {
                if (!bContinueOnError)
                {
                    return false;
                }
                RecordMigrationFailure(Instance.Path, TEXT("Configured instance initialization after defaults"), Error);
                continue;
            }
            if (UActorComponent* UpdatedComponent = Cast<UActorComponent>(Destination))
            {
                UpdatedComponent->PostApplyToComponent();
            }
        }
        for (const TStrongObjectPtr<UObject>& Asset : LoadedAssets)
        {
            if (Asset && Asset->GetOutermost() == Destination->GetOutermost() && Asset->IsAsset())
            {
                ChangedAssets.Add(Asset.Get());
            }
        }
    }
    for (const FMigrationWidget& Snapshot : Widgets)
    {
        UWidget* Widget = FindObject<UWidget>(nullptr, *Snapshot.Path);
        if (!Widget || !Values.ApplyStructure(Widget->GetClass(), Widget, Snapshot.Properties, Widget))
        {
            Error = TEXT("Migration cannot restore the retained widget defaults: ") + Snapshot.Path + TEXT(". ") + Values.Error;
            return false;
        }
    }
    if (!Values.NormalizeObjectMappings())
    {
        Error = Values.Error;
        UE_LOG(LogTemp, Error, TEXT("Final migration reference normalization failed: %s"), *Error);
        return false;
    }
    for (const FMigrationInstance& Instance : EmbeddedObjects)
    {
        UObject* Destination = Values.ObjectMappings.FindRef(Instance.Source.Get());
        if (!IsValid(Destination) || Destination->GetClass()->HasAnyClassFlags(CLASS_NewerVersionExists))
        {
            continue;
        }
        const TArray<FMigrationValue>& OriginalProperties = Values.Objects.FindChecked(Instance.Source.Get()).Properties;
        if (CopyConfiguration(Destination, &OriginalProperties))
        {
            for (const TStrongObjectPtr<UObject>& Asset : LoadedAssets)
            {
                if (Asset && Asset->GetOutermost() == Destination->GetOutermost())
                {
                    ChangedAssets.Add(Asset.Get());
                }
            }
        }
    }
    FlushMigrationComponentUpdates();
    for (const TStrongObjectPtr<UObject>& Asset : LoadedAssets)
    {
        if (!Asset)
        {
            continue;
        }
        FMigrationReferenceArchive Archive(Asset.Get(), Values.ObjectMappings, Values.SoftMappings, DefaultOnlySourceClasses, bKeepAssetReferencesInBlueprint);
        if (Archive.GetCount() > 0 || Archive.bSoftReferencesChanged)
        {
            ChangedAssets.Add(Asset.Get());
        }
    }
    UEditorAssetSubsystem* AssetSubsystem = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
    TMap<FName, FName> UnsafePackageSources;
    auto UpdateUnsafePackages = [&]()
    {
        TArray<FName> PendingPackages;
        for (FName Package : FailedPackages)
        {
            if (!UnsafePackageSources.Contains(Package))
            {
                UnsafePackageSources.Add(Package, Package);
                PendingPackages.Add(Package);
            }
        }
        for (int32 Index = 0; Index < PendingPackages.Num(); ++Index)
        {
            const FName Package = PendingPackages[Index];
            if (const TSet<FName>* Connections = MigrationPackageConnections.Find(Package))
            {
                for (FName ConnectedPackage : *Connections)
                {
                    if (!UnsafePackageSources.Contains(ConnectedPackage))
                    {
                        const FName FailureSource = UnsafePackageSources.FindChecked(Package);
                        UnsafePackageSources.Add(ConnectedPackage, FailureSource);
                        PendingPackages.Add(ConnectedPackage);
                    }
                }
            }
        }
    };
    UpdateUnsafePackages();
    TArray<FString> SavedAssets;
    TArray<FString> SkippedAssets;
    for (UObject* Asset : ChangedAssets)
    {
        if (const FName* FailurePackage = UnsafePackageSources.Find(Asset->GetOutermost()->GetFName()))
        {
            const FString SkipDiagnostic = FString::Format(TEXT("{0}: skipped because its migration group contains failed package {1}."),
                {Asset->GetPathName(), FailurePackage->ToString()});
            SkippedAssets.Add(SkipDiagnostic);
            UE_LOG(LogTemp, Warning, TEXT("Migration dependency skip: %s"), *SkipDiagnostic);
            continue;
        }
        Asset->MarkPackageDirty();
        if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
        {
            if (UBlueprintGeneratedClass* BlueprintClass = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass))
            {
                BlueprintClass->UpdateCustomPropertyListForPostConstruction();
            }
        }
        if (!AssetSubsystem->SaveLoadedAsset(Asset, false))
        {
            Error = TEXT("Migration failed to save: ") + Asset->GetPathName();
            if (!bContinueOnError)
            {
                return false;
            }
            RecordMigrationFailure(Asset->GetPathName(), TEXT("Save asset"), Error);
            SkippedAssets.Add(Asset->GetPathName() + TEXT(": save failed."));
            UpdateUnsafePackages();
            continue;
        }
        SavedAssets.Add(Asset->GetPathName());
        if (bContinueOnError)
        {
            UE_LOG(LogTemp, Display, TEXT("Migration saved asset: %s"), *Asset->GetPathName());
        }
    }
    if (!MigrationErrors.IsEmpty())
    {
        Error = FString::Format(TEXT("Migration completed with errors. Saved assets: {0}; skipped asset saves: {1}; failed packages: {5}. Failed assets and related objects may remain modified in memory. Restart the editor before retrying. Previously saved assets remain saved.\nPrimary errors:\n{2}\nSaved assets:\n{3}\nSkipped asset saves:\n{4}"),
            {SavedAssets.Num(), SkippedAssets.Num(), FString::Join(MigrationErrors, TEXT("\n")),
                FString::Join(SavedAssets, TEXT("\n")), FString::Join(SkippedAssets, TEXT("\n")), FailedPackages.Num()});
        UE_LOG(LogTemp, Error, TEXT("%s"), *Error);
        return false;
    }
    return true;
}
