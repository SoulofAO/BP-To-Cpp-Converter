/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#include "RetainedBlueprintGeneration.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationSettings.h"
#include "NativeApiCatalog.h"
#include "NativeFunctionExpressions.h"
#include "NativeGenerationContext.h"
#include "TranslatorObjects/StructTranslatorObject.h"
#include "TranslatorObjects/CallFunctionTranslatorObject.h"
#include "Engine/Engine.h"
#include "Misc/ScopeExit.h"
#include "Engine/Blueprint.h"
#include "Engine/ActorInstanceHandle.h"
#include "StructUtils/UserDefinedStruct.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_VariableGet.h"
#include "K2Node_MakeContainer.h"
#include "EdGraphSchema_K2.h"
#include "UObject/UnrealType.h"
#include "UObject/SoftObjectPath.h"
#include "EngineAssetPolicy.h"
#include "Components/ActorComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/AudioComponent.h"
#include "Components/BillboardComponent.h"
#include "Components/DecalComponent.h"
#include "Components/ArrowComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SceneCaptureComponentCube.h"
#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Particles/ParticleSystemComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense.h"
#include "NiagaraComponent.h"
#include "GameFramework/Actor.h"
#include "Subsystems/Subsystem.h"

static TSet<UClass*> GeneratedBlueprintClasses;
static TSet<UField*> GeneratedValueTypes;
static bool bRetainedReflectionUsed = false;
static TMap<UObject*, FString> InstancedObjectNames;
static TMap<UObject*, FString> ConstructingObjects;
static TSet<UClass*> ConstructedClasses;
static int32 TransferSequence = 0;
static UBlueprint* OwningBlueprint = nullptr;
static TMap<UClass*, FString> AssetClassMembers;
static TMap<FString, FString> MigrationClassMembers;
static TMap<UActorComponent*, FString> ComponentTemplateMembers;
static FString InstanceInitialization;
static TMap<UObject*, FString> AssetObjectMembers;
static TMap<UObject*, UClass*> AssetObjectConstraints;
static TMap<UActorComponent*, FString> RuntimeComponentTemplates;
static TSet<UActorComponent*> InheritedRuntimeComponentTemplates;
static TMap<FProperty*, FString> InstanceTemplates;
static TSet<FProperty*> InheritedInstanceTemplates;
static TMap<FString, FString> ComponentInstanceTemplates;
static TSet<FString> InheritedComponentInstanceTemplates;
bool FRetainedBlueprintGeneration::bGeneratingConstructor = false;

void FRetainedBlueprintGeneration::AddInstanceInitialization(const FString& Initialization)
{
    InstanceInitialization += Initialization + TEXT("\n");
}

bool FRetainedBlueprintGeneration::CanInitializeValueBeforeEngineStartup(FProperty* Property, const void* PropertyMemory, bool bInitializeExistingStructure)
{
    if (Property->IsA<FSoftObjectProperty>())
    {
        return true;
    }
    if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
    {
        UObject* Object = ObjectProperty->GetObjectPropertyValue(PropertyMemory);
        if (!Object)
        {
            return true;
        }
        if (Property->HasAnyPropertyFlags(CPF_InstancedReference | CPF_PersistentInstance))
        {
            return false;
        }
        if (UClass* Class = Cast<UClass>(Object))
        {
            if (!Class->HasAnyClassFlags(CLASS_Native))
            {
                return true;
            }
            FString Include;
            return CanUseStaticClass(Class, Include);
        }
        return Object->IsAsset();
    }
    if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
    {
        FScriptArrayHelper ArrayValues(ArrayProperty, PropertyMemory);
        for (int32 ElementIndex = 0; ElementIndex < ArrayValues.Num(); ++ElementIndex)
        {
            if (!CanInitializeValueBeforeEngineStartup(ArrayProperty->Inner, ArrayValues.GetRawPtr(ElementIndex)))
            {
                return false;
            }
        }
        return true;
    }
    if (FSetProperty* SetProperty = CastField<FSetProperty>(Property))
    {
        FScriptSetHelper SetValues(SetProperty, PropertyMemory);
        for (int32 ElementIndex = 0; ElementIndex < SetValues.GetMaxIndex(); ++ElementIndex)
        {
            if (SetValues.IsValidIndex(ElementIndex) &&
                !CanInitializeValueBeforeEngineStartup(SetProperty->ElementProp, SetValues.GetElementPtr(ElementIndex)))
            {
                return false;
            }
        }
        return true;
    }
    if (FMapProperty* MapProperty = CastField<FMapProperty>(Property))
    {
        FScriptMapHelper MapValues(MapProperty, PropertyMemory);
        for (int32 ElementIndex = 0; ElementIndex < MapValues.GetMaxIndex(); ++ElementIndex)
        {
            if (MapValues.IsValidIndex(ElementIndex) &&
                (!CanInitializeValueBeforeEngineStartup(MapProperty->KeyProp, MapValues.GetKeyPtr(ElementIndex)) ||
                    !CanInitializeValueBeforeEngineStartup(MapProperty->ValueProp, MapValues.GetValuePtr(ElementIndex))))
            {
                return false;
            }
        }
        return true;
    }
    if (FTextProperty* TextProperty = CastField<FTextProperty>(Property))
    {
        return !TextProperty->GetPropertyValue(PropertyMemory).IsFromStringTable();
    }
    if (FStructProperty* StructureProperty = CastField<FStructProperty>(Property))
    {
        UScriptStruct* Structure = StructureProperty->Struct;
        if (UStructTranslatorObject* Translator = UBlueprintNativizationSettingsLibrary::FindStructTranslator(Structure))
        {
            FStructTranslationRequest Request;
            Request.Structure = Structure;
            Request.Property = Property;
            Request.PropertyValue = PropertyMemory;
            Request.Operation = EStructTranslationOperation::PropertyValue;
            return Translator->CanInitializeValueBeforeEngineStartup(Request);
        }
        const bool bSynthesizedUserStructure = Structure->IsA<UUserDefinedStruct>() && GeneratedValueTypes.Contains(Structure);
        const bool bAssignExistingNativeFields = bInitializeExistingStructure && Structure->IsNative();
        const FString StructurePath = Structure->GetPathName();
        if (Structure == FActorInstanceHandle::StaticStruct())
        {
            FWeakObjectProperty* ActorProperty = FindFProperty<FWeakObjectProperty>(Structure, TEXT("ReferenceObject"));
            if (!PropertyMemory || !ActorProperty)
            {
                return false;
            }
            const FActorInstanceHandle& ActorInstance = *static_cast<const FActorInstanceHandle*>(PropertyMemory);
            return ActorProperty->GetPropertyValue_InContainer(PropertyMemory).IsExplicitlyNull() &&
                ActorInstance.GetInstanceIndex() == INDEX_NONE &&
                ActorInstance.GetManagerInterface().GetWeakObjectPtr().IsExplicitlyNull();
        }
        if (Structure->IsNative() && StructurePath == TEXT("/Script/CoreUObject.SoftObjectPath"))
        {
            return PropertyMemory && Structure->GetStructureSize() == sizeof(FSoftObjectPath);
        }
        if (Structure->IsNative() && StructurePath == TEXT("/Script/CoreUObject.SoftClassPath"))
        {
            return PropertyMemory && Structure->GetStructureSize() == sizeof(FSoftClassPath);
        }
        const bool bQuantizedVectorStructure = Structure->IsNative() &&
            (StructurePath == TEXT("/Script/Engine.Vector_NetQuantize") ||
                StructurePath == TEXT("/Script/Engine.Vector_NetQuantize10") ||
                StructurePath == TEXT("/Script/Engine.Vector_NetQuantize100") ||
                StructurePath == TEXT("/Script/Engine.Vector_NetQuantizeNormal"));
        if (bQuantizedVectorStructure)
        {
            return PropertyMemory && Structure->GetStructureSize() == sizeof(FVector);
        }
        const bool bKnownNativeDataStructure = Structure->IsNative() &&
            (StructurePath == TEXT("/Script/Water.SphericalPontoon") || StructurePath == TEXT("/Script/Water.BuoyancyData") ||
                StructurePath == TEXT("/Script/Engine.EngineShowFlagsSetting") ||
                StructurePath == TEXT("/Script/Engine.LightingChannels") ||
                StructurePath == TEXT("/Script/Engine.SingleAnimationPlayData") ||
                StructurePath == TEXT("/Script/Engine.WeightedBlendable") ||
                StructurePath == TEXT("/Script/Engine.WeightedBlendables"));
        if (!bSynthesizedUserStructure && !bAssignExistingNativeFields && !bKnownNativeDataStructure &&
            (!Structure->IsNative() || Structure->GetOutermost()->GetName() != TEXT("/Script/CoreUObject")))
        {
            return false;
        }
        bool bHasReflectedProperties = false;
        for (TFieldIterator<FProperty> PropertyIterator(Structure); PropertyIterator; ++PropertyIterator)
        {
            if ((bAssignExistingNativeFields || bKnownNativeDataStructure) &&
                PropertyIterator->HasAnyPropertyFlags(CPF_NativeAccessSpecifierPrivate | CPF_NativeAccessSpecifierProtected))
            {
                return false;
            }
            bHasReflectedProperties = true;
            for (int32 ElementIndex = 0; ElementIndex < PropertyIterator->ArrayDim; ++ElementIndex)
            {
                if (!CanInitializeValueBeforeEngineStartup(*PropertyIterator,
                    PropertyIterator->ContainerPtrToValuePtr<void>(PropertyMemory, ElementIndex)))
                {
                    return false;
                }
            }
        }
        return bSynthesizedUserStructure || bHasReflectedProperties;
    }
    return Property->IsA<FNumericProperty>() || Property->IsA<FBoolProperty>() || Property->IsA<FNameProperty>() ||
        Property->IsA<FStrProperty>() || Property->IsA<FEnumProperty>();
}

bool FRetainedBlueprintGeneration::CanInitializeDefaultSetterBeforeEngineStartup(FProperty* Property, UObject* DefaultOwner, const FString& SetterName)
{
    UActorComponent* Component = Cast<UActorComponent>(DefaultOwner);
    if (!Property || !Component || !Component->IsTemplate() || Component->IsRegistered() ||
        Component->IsActive() || Component->IsRenderStateCreated() || Component->IsPhysicsStateCreated())
    {
        return false;
    }
    UClass* ComponentClass = Component->GetClass();
    while (ComponentClass && !ComponentClass->HasAnyClassFlags(CLASS_Native))
    {
        UBlueprint* SourceBlueprint = Cast<UBlueprint>(ComponentClass->ClassGeneratedBy);
        if (!SourceBlueprint || !SourceBlueprint->GeneratedClass)
        {
            return false;
        }
        UClass* SourceClass = SourceBlueprint->GeneratedClass;
        if (!GeneratedBlueprintClasses.Contains(SourceClass) &&
            !FNativeGenerationContext::GetClassReplacements().Contains(SourceClass))
        {
            return false;
        }
        ComponentClass = SourceClass->GetSuperClass();
    }
    const FString PropertyName = Property->GetName();
    const void* PropertyMemory = Property->ContainerPtrToValuePtr<void>(DefaultOwner);
    if (SetterName == TEXT("SetDominantSense") && ComponentClass == UAIPerceptionComponent::StaticClass() &&
        PropertyName == TEXT("DominantSense"))
    {
        FClassProperty* ClassProperty = CastField<FClassProperty>(Property);
        if (!ClassProperty)
        {
            return false;
        }
        UClass* SenseClass = Cast<UClass>(ClassProperty->GetObjectPropertyValue(PropertyMemory));
        if (!SenseClass)
        {
            return true;
        }
        return SenseClass->HasAnyClassFlags(CLASS_Native) && SenseClass->IsChildOf(UAISense::StaticClass()) &&
            SenseClass->GetOutermost()->GetName() == TEXT("/Script/AIModule") &&
            CanInitializeValueBeforeEngineStartup(Property, PropertyMemory);
    }
    const bool bKnownSceneClass = ComponentClass == USceneComponent::StaticClass() ||
        ComponentClass == UStaticMeshComponent::StaticClass() || ComponentClass == USkinnedMeshComponent::StaticClass() ||
        ComponentClass == USkeletalMeshComponent::StaticClass() || ComponentClass == UBoxComponent::StaticClass() ||
        ComponentClass == USphereComponent::StaticClass() || ComponentClass == UCapsuleComponent::StaticClass() ||
        ComponentClass == UNiagaraComponent::StaticClass() || ComponentClass == UDecalComponent::StaticClass() ||
        ComponentClass == UBillboardComponent::StaticClass() || ComponentClass == UAudioComponent::StaticClass() ||
        ComponentClass == UParticleSystemComponent::StaticClass() || ComponentClass == UCameraComponent::StaticClass() ||
        ComponentClass == USpringArmComponent::StaticClass() || ComponentClass == UArrowComponent::StaticClass() ||
        ComponentClass == UDirectionalLightComponent::StaticClass() || ComponentClass == UPointLightComponent::StaticClass() ||
        ComponentClass == USpotLightComponent::StaticClass() || ComponentClass == URectLightComponent::StaticClass() ||
        ComponentClass == USkyLightComponent::StaticClass() || ComponentClass == UTextRenderComponent::StaticClass() ||
        ComponentClass == UPostProcessComponent::StaticClass() || ComponentClass == USceneCaptureComponent2D::StaticClass() ||
        ComponentClass == USceneCaptureComponentCube::StaticClass() || ComponentClass == USplineComponent::StaticClass() ||
        ComponentClass == USplineMeshComponent::StaticClass() || ComponentClass == UPoseableMeshComponent::StaticClass() ||
        ComponentClass == UWidgetComponent::StaticClass() ||
        ((ComponentClass == UInstancedStaticMeshComponent::StaticClass() ||
            ComponentClass == UHierarchicalInstancedStaticMeshComponent::StaticClass()) && !Component->GetWorld());
    if (!bKnownSceneClass)
    {
        return false;
    }
    const bool bRelativeTransformSetter =
        (SetterName == TEXT("SetRelativeLocation") && PropertyName == TEXT("RelativeLocation")) ||
        (SetterName == TEXT("SetRelativeRotation") && PropertyName == TEXT("RelativeRotation")) ||
        (SetterName == TEXT("SetRelativeScale3D") && PropertyName == TEXT("RelativeScale3D")) ||
        (SetterName == TEXT("SetRelativeTransform") && PropertyName == TEXT("RelativeTransform"));
    if (bRelativeTransformSetter)
    {
        return CanInitializeValueBeforeEngineStartup(Property, PropertyMemory);
    }
    const bool bNullAssetSetter =
        (SetterName == TEXT("SetDecalMaterial") && ComponentClass == UDecalComponent::StaticClass() && PropertyName == TEXT("DecalMaterial")) ||
        (SetterName == TEXT("SetSprite") && ComponentClass == UBillboardComponent::StaticClass() && PropertyName == TEXT("Sprite")) ||
        (SetterName == TEXT("SetSound") && ComponentClass == UAudioComponent::StaticClass() && PropertyName == TEXT("Sound")) ||
        (SetterName == TEXT("SetTemplate") && ComponentClass == UParticleSystemComponent::StaticClass() && PropertyName == TEXT("Template")) ||
        (SetterName == TEXT("SetAsset") && ComponentClass == UNiagaraComponent::StaticClass() && PropertyName == TEXT("Asset")) ||
        (SetterName == TEXT("SetStaticMesh") && ComponentClass == UStaticMeshComponent::StaticClass() && PropertyName == TEXT("StaticMesh")) ||
        (SetterName == TEXT("SetSkinnedAssetAndUpdate") &&
            (ComponentClass == USkinnedMeshComponent::StaticClass() || ComponentClass == USkeletalMeshComponent::StaticClass()) && PropertyName == TEXT("SkinnedAsset")) ||
        (SetterName == TEXT("SetSkeletalMesh") && ComponentClass == USkeletalMeshComponent::StaticClass() &&
            (PropertyName == TEXT("SkeletalMesh") || PropertyName == TEXT("SkeletalMeshAsset") || PropertyName == TEXT("SkinnedAsset")));
    if (bNullAssetSetter)
    {
        FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property);
        if (!ObjectProperty)
        {
            return false;
        }
        return true;
    }
    return false;
}

FString FRetainedBlueprintGeneration::RegisterInstanceTemplate(UObject* Object, FProperty* SourceProperty)
{
    if (!OwningBlueprint || !SourceProperty)
    {
        FNativeApiCatalog::AddError(TEXT("Cannot preserve an instance template without an owning Blueprint and source property."));
        return TEXT("nullptr");
    }
    FString& MemberName = InstanceTemplates.FindOrAdd(SourceProperty);
    if (MemberName.IsEmpty())
    {
        UClass* DeclaringClass = OwningBlueprint->GeneratedClass;
        UClass* PropertyOwnerClass = SourceProperty->GetOwnerClass();
        if (PropertyOwnerClass && GeneratedBlueprintClasses.Contains(PropertyOwnerClass))
        {
            DeclaringClass = PropertyOwnerClass;
            if (DeclaringClass != OwningBlueprint->GeneratedClass)
            {
                InheritedInstanceTemplates.Add(SourceProperty);
            }
        }
        MemberName = UBlueprintNativizationLibrary::GetLambdaUniqueVariableNameByClass(
            TEXT("InstanceTemplate") + SourceProperty->GetName(), DeclaringClass);
        MigrationClassMembers.Add(OwningBlueprint->GeneratedClass->GetPathName() + TEXT(":InstancePropertyTemplate:") +
            SourceProperty->GetPathName(), MemberName);
    }
    return MemberName;
}
FString FRetainedBlueprintGeneration::RegisterComponentInstanceTemplate(UActorComponent* SourceComponent, FProperty* Property, UObject*)
{
    if (!OwningBlueprint || !SourceComponent || !Property)
    {
        FNativeApiCatalog::AddError(TEXT("Cannot preserve a component instance template without an owning Blueprint, source component and property."));
        return TEXT("nullptr");
    }
    const FString TemplateKey = SourceComponent->GetPathName() + TEXT(":") + Property->GetName();
    FString& MemberName = ComponentInstanceTemplates.FindOrAdd(TemplateKey);
    if (MemberName.IsEmpty())
    {
        UClass* DeclaringClass = OwningBlueprint->GeneratedClass;
        FString ComponentName = SourceComponent->GetName();
        bool bFoundDeclaringClass = false;
        if (OwningBlueprint->GeneratedClass->IsChildOf(AActor::StaticClass()))
        {
            TArray<FName> ComponentNames;
            const TArray<UActorComponent*> Components = UBlueprintNativizationLibrary::GetClassComponentTemplates(
                ComponentNames, OwningBlueprint->GeneratedClass.Get(), UActorComponent::StaticClass());
            const int32 ComponentIndex = Components.IndexOfByKey(SourceComponent);
            if (ComponentIndex != INDEX_NONE && ComponentNames.IsValidIndex(ComponentIndex))
            {
                ComponentName = ComponentNames[ComponentIndex].ToString();
                FProperty* ComponentProperty = FindFProperty<FProperty>(OwningBlueprint->GeneratedClass, ComponentNames[ComponentIndex]);
                if (ComponentProperty && GeneratedBlueprintClasses.Contains(ComponentProperty->GetOwnerClass()))
                {
                    DeclaringClass = ComponentProperty->GetOwnerClass();
                    bFoundDeclaringClass = true;
                }
            }
        }
        if (!bFoundDeclaringClass)
        {
            UClass* ComponentOwnerClass = SourceComponent->GetTypedOuter<UClass>();
            if (ComponentOwnerClass && GeneratedBlueprintClasses.Contains(ComponentOwnerClass) &&
                OwningBlueprint->GeneratedClass->IsChildOf(ComponentOwnerClass))
            {
                DeclaringClass = ComponentOwnerClass;
            }
        }
        if (DeclaringClass != OwningBlueprint->GeneratedClass)
        {
            InheritedComponentInstanceTemplates.Add(TemplateKey);
        }
        MemberName = UBlueprintNativizationLibrary::GetLambdaUniqueVariableNameByClass(
            TEXT("ComponentInstanceTemplate") + ComponentName + Property->GetName(), DeclaringClass);
        MigrationClassMembers.Add(OwningBlueprint->GeneratedClass->GetPathName() + TEXT(":ComponentInstanceTemplate:") + TemplateKey, MemberName);
    }
    return MemberName;
}

FString FRetainedBlueprintGeneration::RegisterRuntimeComponentTemplate(UActorComponent* Component, FProperty* SourceProperty)
{
    if (!OwningBlueprint)
    {
        FNativeApiCatalog::AddError(TEXT("Cannot preserve a runtime component template without an owning Blueprint: ") + Component->GetPathName());
        return TEXT("nullptr");
    }
    FString& MemberName = RuntimeComponentTemplates.FindOrAdd(Component);
    if (MemberName.IsEmpty())
    {
        UClass* DeclaringClass = OwningBlueprint->GeneratedClass;
        FString ComponentName = Component->GetName();
        if (SourceProperty)
        {
            ComponentName = SourceProperty->GetName();
            UClass* PropertyOwnerClass = SourceProperty->GetOwnerClass();
            if (PropertyOwnerClass && GeneratedBlueprintClasses.Contains(PropertyOwnerClass))
            {
                DeclaringClass = PropertyOwnerClass;
                if (DeclaringClass != OwningBlueprint->GeneratedClass)
                {
                    InheritedRuntimeComponentTemplates.Add(Component);
                }
            }
        }
        MemberName = UBlueprintNativizationLibrary::GetLambdaUniqueVariableNameByClass(
            TEXT("RuntimeComponentTemplate") + ComponentName, DeclaringClass);
        MigrationClassMembers.Add(OwningBlueprint->GeneratedClass->GetPathName() + TEXT(":RuntimeComponentTemplate:") + Component->GetPathName(), MemberName);
    }
    return MemberName;
}

static FString GetFunctionLibraryAssetDefaults(UBlueprint* Blueprint)
{
    FString Expression = TEXT("GetDefault<") + UBlueprintNativizationLibrary::GetUniqueFieldName(Blueprint->GeneratedClass) + TEXT(">()");
    Expression += TEXT("->") + UBlueprintNativizationLibrary::GetLambdaUniqueVariableNameByClass(
        TEXT("BlueprintLibraryAssetDefaults"), Blueprint->GeneratedClass) + TEXT(".Get()");
    return Expression;
}

FString FRetainedBlueprintGeneration::GetAssetValue(UObject* Asset, UClass* ConstraintClass)
{
    if (!Asset)
    {
        return TEXT("nullptr");
    }
    if (UClass* Class = Cast<UClass>(Asset))
    {
        return GetClassValue(Class);
    }
    if (!OwningBlueprint)
    {
        FNativeApiCatalog::AddError(TEXT("Cannot cache an asset without an owning Blueprint: ") + Asset->GetPathName());
        return TEXT("nullptr");
    }
    if (!ConstraintClass)
    {
        ConstraintClass = Asset->GetClass();
        while (!ConstraintClass->HasAnyClassFlags(CLASS_Native))
        {
            ConstraintClass = ConstraintClass->GetSuperClass();
        }
    }
    FString& MemberName = AssetObjectMembers.FindOrAdd(Asset);
    if (MemberName.IsEmpty())
    {
        MemberName = UBlueprintNativizationLibrary::GetLambdaUniqueVariableNameByClass(
            TEXT("Asset") + Asset->GetName(), OwningBlueprint->GeneratedClass, Asset->GetPathName());
        AssetObjectConstraints.Add(Asset, ConstraintClass);
        MigrationClassMembers.Add(OwningBlueprint->GeneratedClass->GetPathName() + TEXT(":AssetObject:") + Asset->GetPathName(), MemberName);
    }
    if (OwningBlueprint->BlueprintType == BPTYPE_FunctionLibrary)
    {
        return GetFunctionLibraryAssetDefaults(OwningBlueprint) + TEXT("->") + MemberName + TEXT(".Get()");
    }
    return MemberName + TEXT(".Get()");
}

void FRetainedBlueprintGeneration::Begin(const TArray<UObject*>& Objects)
{
    MigrationClassMembers.Reset();
    EndObject();
    GeneratedBlueprintClasses.Reset();
    GeneratedValueTypes.Reset();
    for (UObject* Object : Objects)
    {
        if (UBlueprint* Blueprint = Cast<UBlueprint>(Object))
        {
            GeneratedBlueprintClasses.Add(Blueprint->GeneratedClass);
            GeneratedBlueprintClasses.Add(Blueprint->SkeletonGeneratedClass);
        }
        else if (UClass* Class = Cast<UClass>(Object))
        {
            GeneratedBlueprintClasses.Add(Class);
        }
        else if (UField* Field = Cast<UField>(Object))
        {
            GeneratedValueTypes.Add(Field);
        }
    }
}

bool FRetainedBlueprintGeneration::IsRetained(UClass* Class)
{
    Class = FNativeGenerationContext::ResolveClass(Class);
    return Class && !Class->HasAnyClassFlags(CLASS_Native) && !GeneratedBlueprintClasses.Contains(Class);
}

bool FRetainedBlueprintGeneration::ShouldPreserveStructureAsset(const UScriptStruct* Structure)
{
    const UUserDefinedStruct* BlueprintStructure = Cast<UUserDefinedStruct>(Structure);
    if (!BlueprintStructure)
    {
        return false;
    }
    const uint8* DefaultMemory = BlueprintStructure->GetDefaultInstance();
    for (TFieldIterator<FProperty> Property(Structure); Property; ++Property)
    {
        for (int32 ElementIndex = 0; ElementIndex < Property->ArrayDim; ++ElementIndex)
        {
            if (UBlueprintNativizationLibrary::RequiresPostCDOInitialization(*Property,
                Property->ContainerPtrToValuePtr<void>(DefaultMemory, ElementIndex), true))
            {
                return true;
            }
        }
    }
    return false;
}

bool FRetainedBlueprintGeneration::IsExternalStructure(const UScriptStruct* Structure)
{
    return Structure && Structure->IsA<UUserDefinedStruct>() &&
        !GeneratedValueTypes.Contains(const_cast<UScriptStruct*>(Structure)) &&
        (ShouldPreserveStructureAsset(Structure) || !FEngineAssetPolicy::GetExclusionReason(Structure,
            GetDefault<UBlueprintNativizationV2EditorSettings>()->bExcludeEngineAssetsFromConversion).IsEmpty());
}

UClass* FRetainedBlueprintGeneration::GetStorageClass(UClass* Class)
{
    Class = FNativeGenerationContext::ResolveClass(Class);
    if (IsRetained(Class))
    {
        while (Class && !Class->HasAnyClassFlags(CLASS_Native) && IsRetained(Class))
        {
            Class = Class->GetSuperClass();
        }
    }
    while (Class && Class->HasAnyClassFlags(CLASS_Native) &&
        Class->GetMetaData(TEXT("ModuleRelativePath")).Replace(TEXT("\\"), TEXT("/")).StartsWith(TEXT("Private/")))
    {
        Class = Class->GetSuperClass();
    }
    return Class;
}

FString FRetainedBlueprintGeneration::GetClassName(UClass* Class)
{
    Class = FNativeGenerationContext::ResolveClass(Class);
    UClass* StorageClass = GetStorageClass(Class);
    if (StorageClass && StorageClass->HasAnyClassFlags(CLASS_Interface))
    {
        return UBlueprintNativizationLibrary::GetUniqueUInterfaceFieldName(StorageClass);
    }
    return UBlueprintNativizationLibrary::GetUniqueFieldName(StorageClass);
}

FString FRetainedBlueprintGeneration::Quote(const FString& Value)
{
    FString Escaped = Value.Replace(TEXT("\\"), TEXT("\\\\"));
    Escaped.ReplaceInline(TEXT("\""), TEXT("\\\""));
    Escaped.ReplaceInline(TEXT("\n"), TEXT("\\n"));
    Escaped.ReplaceInline(TEXT("\r"), TEXT("\\r"));
    return TEXT("TEXT(\"") + Escaped + TEXT("\")");
}

static bool RequiresClassPathValue(UClass* Class)
{
    return FRetainedBlueprintGeneration::IsRetained(Class) ||
        (Class && Class->GetMetaData(TEXT("ModuleRelativePath")).Contains(TEXT("Private/")));
}

bool FRetainedBlueprintGeneration::CanUseStaticClass(UClass* Class, FString& Include)
{
    Include.Reset();
    if (!Class)
    {
        return false;
    }
    if (UBlueprint* Blueprint = Cast<UBlueprint>(Class->ClassGeneratedBy))
    {
        if (Blueprint->GeneratedClass)
        {
            Class = Blueprint->GeneratedClass;
        }
    }
    if (!Class->HasAnyClassFlags(CLASS_Native) && Class->GetOutermost() == GetTransientPackage())
    {
        return false;
    }
    Class = FNativeGenerationContext::ResolveClass(Class);
    if (RequiresClassPathValue(Class) || FNativeGenerationContext::bGeneratingInlineDefaults)
    {
        return false;
    }
    if (GEngine->GetEngineSubsystem<UNativizationV2Subsystem>()->GetIncludeFromField(Class, Include))
    {
        return true;
    }
    return !Class->HasAnyClassFlags(CLASS_Native);
}

FString FRetainedBlueprintGeneration::ResolveClassFilterValue(const FString& Value, UClass* ConstraintClass)
{
    if (!ConstraintClass)
    {
        ConstraintClass = UObject::StaticClass();
    }
    TMap<FString, FString> ClassPaths;
    for (UClass* OriginalClass : GeneratedBlueprintClasses)
    {
        UBlueprint* Blueprint = Cast<UBlueprint>(OriginalClass->ClassGeneratedBy);
        if (!Blueprint || OriginalClass != Blueprint->GeneratedClass ||
            (ConstraintClass && !OriginalClass->IsChildOf(ConstraintClass)))
        {
            continue;
        }
        UClass* ResolvedClass = FNativeGenerationContext::ResolveClass(OriginalClass);
        if (IsRetained(ResolvedClass))
        {
            continue;
        }
        FString NativePath = ResolvedClass->GetPathName();
        if (!ResolvedClass->HasAnyClassFlags(CLASS_Native))
        {
            NativePath = TEXT("/Script/") + UBlueprintNativizationSettingsLibrary::GetModuleName() + TEXT(".") +
                GetClassName(ResolvedClass).RightChop(FCString::Strlen(ResolvedClass->GetPrefixCPP()));
        }
        ClassPaths.Add(OriginalClass->GetPathName(), NativePath);
    }
    if (ClassPaths.IsEmpty())
    {
        return Value;
    }
    const FString HelperName = UBlueprintNativizationLibrary::GetUniqueFieldName(OwningBlueprint->GeneratedClass) +
        TEXT("ClassFilter") + UBlueprintNativizationLibrary::GetUniqueFieldName(ConstraintClass);
    TArray<FString> SourcePaths;
    ClassPaths.GetKeys(SourcePaths);
    SourcePaths.Sort();
    FString Declaration = TEXT("struct ") + HelperName +
        TEXT("\n{\nstatic UClass* Resolve(UClass* Class)\n{\nif (!Class)\n{\nreturn nullptr;\n}\n") +
        TEXT("static const TMap<FString, FString> ConvertedClasses =\n{\n");
    for (const FString& SourcePath : SourcePaths)
    {
        Declaration += TEXT("{") + Quote(SourcePath) + TEXT(", ") + Quote(ClassPaths.FindChecked(SourcePath)) + TEXT("},\n");
    }
    Declaration += FString(TEXT("};\nif (const FString* NativePath = ConvertedClasses.Find(Class->GetPathName()))\n{\n")) +
        TEXT("return FindObject<UClass>(nullptr, **NativePath);\n}\nreturn Class;\n}\n};");
    FStructTranslationResult Requirements;
    Requirements.HeaderIncludes.Add(TEXT("UObject/UObjectGlobals.h"));
    Requirements.ForwardDeclarations.Add(Declaration);
    FNativeGenerationContext::AddRequirements(Requirements);
    return HelperName + TEXT("::Resolve(") + Value + TEXT(")");
}

FString FRetainedBlueprintGeneration::GetClassValue(UClass* Class)
{
    if (Class)
    {
        if (UBlueprint* Blueprint = Cast<UBlueprint>(Class->ClassGeneratedBy))
        {
            if (Blueprint->GeneratedClass)
            {
                Class = Blueprint->GeneratedClass;
            }
        }
    }
    if (Class && !Class->HasAnyClassFlags(CLASS_Native) && Class->GetOutermost() == GetTransientPackage())
    {
        FNativeApiCatalog::AddError(TEXT("Cannot export a transient Blueprint class without its authoritative asset class: ") + Class->GetPathName());
        return TEXT("nullptr");
    }
    UNativizationV2Subsystem* Subsystem = GEngine->GetEngineSubsystem<UNativizationV2Subsystem>();
    const bool bTypeIdentity = Class && (FNativeGenerationContext::bGeneratingClassFilter ||
        Class->HasAnyClassFlags(CLASS_Interface));
    if (Class && !Class->HasAnyClassFlags(CLASS_Native) && !(bTypeIdentity && !IsRetained(FNativeGenerationContext::ResolveClass(Class))))
    {
        if (!OwningBlueprint ||
            FNativeGenerationContext::bGeneratingInlineDefaults || bGeneratingConstructor)
        {
            FNativeApiCatalog::AddError(TEXT("Cannot preserve the asset class in Blueprint without an instance property available after migration: ") + Class->GetPathName());
            return TEXT("nullptr");
        }
        FString& MemberName = AssetClassMembers.FindOrAdd(Class);
        if (MemberName.IsEmpty())
        {
            MemberName = UBlueprintNativizationLibrary::GetLambdaUniqueVariableNameByClass(
                TEXT("AssetClass") + Class->GetName(), OwningBlueprint->GeneratedClass, Class->GetPathName());
            MigrationClassMembers.Add(OwningBlueprint->GeneratedClass->GetPathName() + TEXT(":AssetClass:") +
                Class->GetPathName(), MemberName);
        }
        if (OwningBlueprint->BlueprintType == BPTYPE_FunctionLibrary)
        {
            return GetFunctionLibraryAssetDefaults(OwningBlueprint) + TEXT("->") + MemberName + TEXT(".Get()");
        }
        return MemberName + TEXT(".Get()");
    }
    Class = FNativeGenerationContext::ResolveClass(Class);
    if (RequiresClassPathValue(Class))
    {
        if (Class->HasAnyClassFlags(CLASS_Native))
        {
            return TEXT("LoadClass<UObject>(nullptr, ") + Quote(Class->GetPathName()) + TEXT(")");
        }
        FNativeApiCatalog::AddError(TEXT("Cannot emit an asset class without its serialized Blueprint property: ") + Class->GetPathName());
        return TEXT("nullptr");
    }
    if (Class && FNativeGenerationContext::bGeneratingInlineDefaults)
    {
        FString ClassPath = Class->GetPathName();
        if (!Class->HasAnyClassFlags(CLASS_Native))
        {
            ClassPath = TEXT("/Script/") + UBlueprintNativizationSettingsLibrary::GetModuleName() + TEXT(".") +
                GetClassName(Class).RightChop(FCString::Strlen(Class->GetPrefixCPP()));
        }
        return TEXT("LoadClass<UObject>(nullptr, ") + Quote(ClassPath) + TEXT(")");
    }
    FString Include;
    if (CanUseStaticClass(Class, Include))
    {
        if (Class->HasAnyClassFlags(CLASS_Native))
        {
            FStructTranslationResult Requirements;
            Requirements.ModuleDependencies.Add(Class->GetOutermost()->GetName().RightChop(8));
            FNativeGenerationContext::AddRequirements(Requirements);
        }
        if (!Include.IsEmpty())
        {
            FNativeGenerationContext::AddImplementationInclude(Include);
        }
    }
    else if (Class && Class->HasAnyClassFlags(CLASS_Native))
    {
        return TEXT("LoadClass<UObject>(nullptr, ") + Quote(Class->GetPathName()) + TEXT(")");
    }
    return GetClassName(Class) + TEXT("::StaticClass()");
}

void FRetainedBlueprintGeneration::RegisterFunctionLibraryDependency(UClass* Class)
{
    UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(Class);
    if (Blueprint)
    {
        Class = Blueprint->GeneratedClass;
    }
    if (!Class || !GeneratedBlueprintClasses.Contains(Class))
    {
        return;
    }
    if (Blueprint && Blueprint->BlueprintType == BPTYPE_FunctionLibrary)
    {
        GetClassValue(Blueprint->GeneratedClass);
    }
}

void FRetainedBlueprintGeneration::BeginObject(UBlueprint* Blueprint)
{
    OwningBlueprint = Blueprint;
    ComponentTemplateMembers.Reset();
    AssetClassMembers.Reset();
    InstanceInitialization.Reset();
    AssetObjectMembers.Reset();
    AssetObjectConstraints.Reset();
    RuntimeComponentTemplates.Reset();
    InheritedRuntimeComponentTemplates.Reset();
    InstanceTemplates.Reset();
    InheritedInstanceTemplates.Reset();
    ComponentInstanceTemplates.Reset();
    InheritedComponentInstanceTemplates.Reset();
    bRetainedReflectionUsed = false;
    InstancedObjectNames.Reset();
    ConstructingObjects.Reset();
    ConstructedClasses.Reset();
    TransferSequence = 0;
}

void FRetainedBlueprintGeneration::EndObject()
{
    OwningBlueprint = nullptr;
    AssetClassMembers.Reset();
    bGeneratingConstructor = false;
}

TMap<FString, FString> FRetainedBlueprintGeneration::GetMigrationClassMembers()
{
    return MigrationClassMembers;
}

FString FRetainedBlueprintGeneration::RegisterComponentTemplate(UActorComponent* Component)
{
    FString& MemberName = ComponentTemplateMembers.FindOrAdd(Component);
    if (MemberName.IsEmpty())
    {
        MemberName = UBlueprintNativizationLibrary::GetLambdaUniqueVariableNameByClass(
            TEXT("ComponentTemplate") + Component->GetName(), OwningBlueprint->GeneratedClass);
        MigrationClassMembers.Add(OwningBlueprint->GeneratedClass->GetPathName() + TEXT(":ComponentTemplate:") +
            Component->GetName(), MemberName);
        ConstructedClasses.Add(GetStorageClass(Component->GetClass()));
    }
    return MemberName;
}


FString FRetainedBlueprintGeneration::ConstructInstancedObject(FObjectPropertyBase* Property, UObject* Object,
    const FString& Outer, bool bArchetypeTemplate)
{
    if (const FString* ExistingInstance = ConstructingObjects.Find(Object))
    {
        return *ExistingInstance;
    }
    FString& ObjectName = InstancedObjectNames.FindOrAdd(Object);
    if (ObjectName.IsEmpty())
    {
        ObjectName = TEXT("Default") + FNativeGenerationContext::ResolveClass(Object->GetClass())->GetName().Replace(TEXT("_C"), TEXT("")) + FString::FromInt(InstancedObjectNames.Num());
    }
    FString ConstructionName = ObjectName;
    if (bArchetypeTemplate && OwningBlueprint)
    {
        ConstructionName = UBlueprintNativizationLibrary::GetUniqueFieldName(OwningBlueprint->GeneratedClass) + ConstructionName;
    }
    const FString InstanceName = ConstructionName + TEXT("Instance");
    ConstructingObjects.Add(Object, InstanceName);
    ON_SCOPE_EXIT
    {
        ConstructingObjects.Remove(Object);
    };
    UClass* InstanceClass = Object->GetClass();
    UClass* StorageClass = GetStorageClass(InstanceClass);
    ConstructedClasses.Add(StorageClass);
    FStructTranslationResult Requirements;
    if (StorageClass->GetOutermost()->GetName().StartsWith(TEXT("/Script/")))
    {
        Requirements.ModuleDependencies.Add(StorageClass->GetOutermost()->GetName().RightChop(8));
        FNativeGenerationContext::AddRequirements(Requirements);
    }
    const FString Type = GetClassName(Property->PropertyClass);
    const FString InstanceType = GetClassName(InstanceClass);
    FString Code = TEXT("([&]() -> ") + Type + TEXT("*\n{\nUObject* ObjectOuter = ") + Outer + TEXT(";\n");
    Code += TEXT("if (auto* ExistingObject = FindObjectFast<") + InstanceType + TEXT(">(ObjectOuter, FName(") + Quote(ConstructionName) + TEXT(")))\n{\nreturn ExistingObject;\n}\n");
    Code += TEXT("UClass* ObjectClass = ") + GetClassValue(InstanceClass) + TEXT(";\n");
    if (bArchetypeTemplate || (Property->GetOwnerClass() == UChildActorComponent::StaticClass() && Property->GetFName() == TEXT("ChildActorTemplate")))
    {
        Code += TEXT("EObjectFlags ObjectFlags = RF_ArchetypeObject | RF_Public | RF_Transactional;\n");
    }
    else
    {
        Code += TEXT("EObjectFlags ObjectFlags = RF_NoFlags;\nif (ObjectOuter->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))\n{\nObjectFlags |= RF_DefaultSubObject;\n}\n");
    }
    Code += TEXT("auto* ") + InstanceName + TEXT(" = NewObject<") + InstanceType + TEXT(">(ObjectOuter, ObjectClass, FName(") + Quote(ConstructionName) + TEXT("), ObjectFlags);\n");
    UObject* DefaultObject = InstanceClass->GetDefaultObject();
    UNativizationV2Subsystem* Subsystem = GEngine->GetEngineSubsystem<UNativizationV2Subsystem>();
    for (TFieldIterator<FProperty> PropertyIterator(InstanceClass); PropertyIterator; ++PropertyIterator)
    {
        FProperty* DefaultProperty = *PropertyIterator;
        if (!DefaultProperty->HasAnyPropertyFlags(CPF_Edit) || DefaultProperty->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated) ||
            DefaultProperty->Identical_InContainer(Object, DefaultObject))
        {
            continue;
        }
        FString Value;
        void* ValueAddress = DefaultProperty->ContainerPtrToValuePtr<void>(Object);
        if (UBlueprintNativizationLibrary::IsAvailibleOneLineDefaultConstructorForProperty(DefaultProperty))
        {
            Value = UBlueprintNativizationLibrary::GenerateOneLineDefaultConstructorForProperty(DefaultProperty, ValueAddress, InstanceName);
        }
        else
        {
            Code += UBlueprintNativizationLibrary::GenerateManyLinesDefaultConstructorForProperty(DefaultProperty,
                ValueAddress, nullptr, Subsystem->EntryNodes, InstanceClass, true, TEXT(""), Value, InstanceName);
        }
        FGetterAndSetterPropertyDescriptor Descriptor;
        UBlueprintNativizationSettingsLibrary::FindGetterAndSetterDescriptorDescriptorByProperty(DefaultProperty, Descriptor);
        if (!Descriptor.SetterPropertyFunctionName.IsEmpty())
        {
            Code += InstanceName + TEXT("->") + Descriptor.SetterPropertyFunctionName + TEXT("(") + Value + TEXT(");\n");
        }
        else if (IsRetained(DefaultProperty->GetOwnerClass()) ||
            DefaultProperty->HasAnyPropertyFlags(CPF_NativeAccessSpecifierProtected))
        {
            Code += WriteProperty(DefaultProperty, InstanceName, Value);
        }
        else if (DefaultProperty->HasAnyPropertyFlags(CPF_NativeAccessSpecifierPrivate))
        {
            FNativeApiCatalog::AddError(TEXT("Instanced object default requires an accessible native setter: ") + DefaultProperty->GetPathName());
        }
        else
        {
            Code += InstanceName + TEXT("->") + UBlueprintNativizationLibrary::GetUniquePropertyName(DefaultProperty) +
                TEXT(" = ") + Value + TEXT(";\n");
        }
    }
    return Code + TEXT("return ") + InstanceName + TEXT(";\n}())");
}

void FRetainedBlueprintGeneration::Complete(UBlueprint* Blueprint, FString& Header, FString& Source)
{
    UNativizationV2Subsystem* Subsystem = GEngine->GetEngineSubsystem<UNativizationV2Subsystem>();
    FString AdditionalIncludes;
    const FString RuntimeModuleName = UBlueprintNativizationSettingsLibrary::GetModuleName();
    const FString SubsystemRuntimeClass = TEXT("F") + RuntimeModuleName + TEXT("SubsystemRuntime");
    const bool bLibraryAssetBridge = Blueprint->BlueprintType == BPTYPE_FunctionLibrary &&
        (!AssetClassMembers.IsEmpty() || !AssetObjectMembers.IsEmpty());
    if (bLibraryAssetBridge)
    {
        GetClassValue(Blueprint->GeneratedClass);
    }
    if (!ComponentTemplateMembers.IsEmpty())
    {
        const int32 Body = Header.Find(TEXT("GENERATED_BODY()"));
        const FString ClassName = GetClassName(Blueprint->GeneratedClass);
        const int32 ConstructorSignature = Source.Find(ClassName + TEXT("::") + ClassName + TEXT("("));
        const int32 ConstructorBody = Source.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, ConstructorSignature);
        if (Body == INDEX_NONE || ConstructorSignature == INDEX_NONE || ConstructorBody == INDEX_NONE)
        {
            FNativeApiCatalog::AddError(TEXT("Cannot initialize construction component templates without a reflected class constructor: ") + Blueprint->GetPathName());
        }
        else
        {
            FString Declarations;
            TArray<UActorComponent*> Templates;
            ComponentTemplateMembers.GetKeys(Templates);
            Templates.Sort([](const UActorComponent& First, const UActorComponent& Second)
            {
                return First.GetName() < Second.GetName();
            });
            for (UActorComponent* Component : Templates)
            {
                const FString& MemberName = ComponentTemplateMembers.FindChecked(Component);
                Declarations += TEXT("\nUPROPERTY()\nTObjectPtr<UObject> ") + MemberName + TEXT(";\n");

            }
            Header.InsertAt(Body + FString(TEXT("GENERATED_BODY()")).Len(), Declarations);
            const int32 HeaderInclude = Header.Find(TEXT("#include"));
            Header.InsertAt(HeaderInclude, TEXT("#include \"Components/ActorComponent.h\"\n"));
            AdditionalIncludes += TEXT("#include \"UObject/UObjectGlobals.h\"\n");
        }
    }
    for (UClass* Class : ConstructedClasses)
    {
        FString Include;
        if (Class != Blueprint->GeneratedClass && Subsystem->GetIncludeFromField(Class, Include) && !Source.Contains(Include))
        {
            AdditionalIncludes += Include + TEXT("\n");
        }
    }
    FString CachedClassDeclarations;
    TArray<FString> InstanceTemplateMembers;
    for (const TPair<FString, FString>& Template : ComponentInstanceTemplates)
    {
        if (!InheritedComponentInstanceTemplates.Contains(Template.Key))
        {
            InstanceTemplateMembers.Add(Template.Value);
        }
    }
    for (const TPair<FProperty*, FString>& Template : InstanceTemplates)
    {
        if (!InheritedInstanceTemplates.Contains(Template.Key))
        {
            InstanceTemplateMembers.Add(Template.Value);
        }
    }
    InstanceTemplateMembers.Sort();
    for (const FString& MemberName : InstanceTemplateMembers)
    {
        CachedClassDeclarations += TEXT("\nprotected:\nUPROPERTY()\nTObjectPtr<UObject> ") + MemberName + TEXT(";\nprivate:\n");
    }
    TArray<UObject*> Assets;
    AssetObjectMembers.GetKeys(Assets);
    Assets.Sort([](const UObject& FirstAsset, const UObject& SecondAsset)
    {
        return FirstAsset.GetPathName() < SecondAsset.GetPathName();
    });
    for (UObject* Asset : Assets)
    {
        UClass* ConstraintClass = AssetObjectConstraints.FindChecked(Asset);
        const FString Type = UBlueprintNativizationLibrary::GetUniqueFieldName(ConstraintClass);
        const FString& MemberName = AssetObjectMembers.FindChecked(Asset);
        FString Metadata;
        if (bLibraryAssetBridge)
        {
            Metadata = TEXT(", meta = (LibraryAssetField)");
            MigrationClassMembers.Add(Blueprint->GeneratedClass->GetPathName() + TEXT(":LibraryAssetField:") + MemberName, MemberName);
        }
        CachedClassDeclarations += TEXT("\nUPROPERTY(EditDefaultsOnly, Category = \"Assets\"") + Metadata + TEXT(")\nTObjectPtr<") + Type + TEXT("> ") + MemberName + TEXT(";\n");
        FString Include;
        if (Subsystem->GetIncludeFromField(ConstraintClass, Include) && !Header.Contains(Include))
        {
            Header.InsertAt(Header.Find(TEXT("#include")), Include + TEXT("\n"));
        }
        FStructTranslationResult Requirements;
        Requirements.ModuleDependencies.Add(ConstraintClass->GetOutermost()->GetName().RightChop(8));
        FNativeGenerationContext::AddRequirements(Requirements);

    }
    TArray<UActorComponent*> RuntimeTemplates;
    RuntimeComponentTemplates.GetKeys(RuntimeTemplates);
    RuntimeTemplates.Sort([](const UActorComponent& FirstComponent, const UActorComponent& SecondComponent)
    {
        return FirstComponent.GetPathName() < SecondComponent.GetPathName();
    });
    for (UActorComponent* Component : RuntimeTemplates)
    {
        if (!InheritedRuntimeComponentTemplates.Contains(Component))
        {
            CachedClassDeclarations += TEXT("\nprotected:\nUPROPERTY()\nTObjectPtr<UObject> ") + RuntimeComponentTemplates.FindChecked(Component) + TEXT(";\nprivate:\n");
        }
    }

    if (!CachedClassDeclarations.IsEmpty())
    {
        const int32 Body = Header.Find(TEXT("GENERATED_BODY()"));
        if (Body != INDEX_NONE)
        {
            Header.InsertAt(Body + FString(TEXT("GENERATED_BODY()")).Len(), CachedClassDeclarations);
        }
    }
    if (!InstanceInitialization.IsEmpty())
    {
        const FString ClassName = UBlueprintNativizationLibrary::GetUniqueFieldName(Blueprint->GeneratedClass);
        const FString MethodName = UBlueprintNativizationLibrary::GetLambdaUniqueVariableNameByClass(
            TEXT("InitializeBlueprintInstance"), Blueprint->GeneratedClass);
        MigrationClassMembers.Add(Blueprint->GeneratedClass->GetPathName() + TEXT(":InstanceInitializer:"), MethodName);
        FString Declarations = TEXT("\nUFUNCTION()\nvoid ") + MethodName + TEXT("();\npublic:\nvirtual void PostInitProperties() override;\n");
        Source += TEXT("\nvoid ") + ClassName + TEXT("::") + MethodName + TEXT("()\n{\nif (IsTemplate() || HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad))\n{\nreturn;\n}\n") + InstanceInitialization + TEXT("\n}\n");
        Source += TEXT("\nvoid ") + ClassName + TEXT("::PostInitProperties()\n{\nSuper::PostInitProperties();\n") + MethodName + TEXT("();\n}\n");
        if (Blueprint->GeneratedClass->IsChildOf(AActor::StaticClass()))
        {
            Declarations += TEXT("virtual void PreRegisterAllComponents() override;\n");
            Source += TEXT("\nvoid ") + ClassName + TEXT("::PreRegisterAllComponents()\n{\nSuper::PreRegisterAllComponents();\n") + MethodName + TEXT("();\n}\n");
        }
        else if (Blueprint->GeneratedClass->IsChildOf(UActorComponent::StaticClass()))
        {
            Declarations += TEXT("virtual void OnRegister() override;\n");
            Source += TEXT("\nvoid ") + ClassName + TEXT("::OnRegister()\n{\n") + MethodName + TEXT("();\nSuper::OnRegister();\n}\n");
        }
        else
        {
            Declarations += TEXT("virtual void PostLoad() override;\n");
            Source += TEXT("\nvoid ") + ClassName + TEXT("::PostLoad()\n{\nSuper::PostLoad();\n") + MethodName + TEXT("();\n}\n");
        }
        Declarations += TEXT("private:\n");
        Header.InsertAt(Header.Find(TEXT("GENERATED_BODY()")) + FString(TEXT("GENERATED_BODY()")).Len(), Declarations);
    }
    if (Blueprint->GeneratedClass->IsChildOf(USubsystem::StaticClass()))
    {
        AdditionalIncludes += TEXT("#include \"") + RuntimeModuleName + TEXT(".h\"\n");
        const FString ClassName = UBlueprintNativizationLibrary::GetUniqueFieldName(Blueprint->GeneratedClass);
        Header.InsertAt(Header.Find(TEXT("GENERATED_BODY()")) + FString(TEXT("GENERATED_BODY()")).Len(),
            TEXT("\npublic:\nvirtual bool ShouldCreateSubsystem(UObject* Outer) const override;\nprivate:\n"));
        Source += TEXT("\nbool ") + ClassName + TEXT("::ShouldCreateSubsystem(UObject* Outer) const\n{\n");
        Source += TEXT("return ") + SubsystemRuntimeClass + TEXT("::ShouldCreateSubsystem(this, Outer, ") +
            ClassName + TEXT("::StaticClass()) && Super::ShouldCreateSubsystem(Outer);\n}\n");
        {
            const FString PostLoadPrefix = TEXT("\nvoid ") + ClassName + TEXT("::PostLoad()\n{\nSuper::PostLoad();\n");
            const int32 PostLoadPosition = Source.Find(PostLoadPrefix, ESearchCase::CaseSensitive);
            const FString Registration = TEXT("if (HasAnyFlags(RF_ClassDefaultObject) && !UObject::GetClass()->HasAnyClassFlags(CLASS_Native | CLASS_Abstract))\n{\n") +
                SubsystemRuntimeClass + TEXT("::RegisterSubsystemClass(UObject::GetClass());\n}\n");
            if (PostLoadPosition == INDEX_NONE)
            {
                Header.InsertAt(Header.Find(TEXT("GENERATED_BODY()")) + FString(TEXT("GENERATED_BODY()")).Len(),
                    TEXT("\npublic:\nvirtual void PostLoad() override;\nprivate:\n"));
                Source += PostLoadPrefix + Registration + TEXT("}\n");
            }
            else
            {
                Source.InsertAt(PostLoadPosition + PostLoadPrefix.Len(), Registration);
            }
        }
    }
    FString AssetClassDeclarations;
    TArray<UClass*> AssetClasses;
    AssetClassMembers.GetKeys(AssetClasses);
    AssetClasses.Sort([](const UClass& FirstClass, const UClass& SecondClass)
    {
        return FirstClass.GetPathName() < SecondClass.GetPathName();
    });
    for (UClass* AssetClass : AssetClasses)
    {
        UClass* ConstraintClass = AssetClass;
        FString ConstraintInclude;
        while (ConstraintClass && (!ConstraintClass->HasAnyClassFlags(CLASS_Native) ||
            !Subsystem->GetIncludeFromField(ConstraintClass, ConstraintInclude)))
        {
            ConstraintClass = ConstraintClass->GetSuperClass();
        }
        if (!ConstraintClass)
        {
            ConstraintClass = UObject::StaticClass();
        }
        if (!ConstraintInclude.IsEmpty() && !Header.Contains(ConstraintInclude))
        {
            const int32 IncludePosition = Header.Find(TEXT("#include"));
            if (IncludePosition != INDEX_NONE)
            {
                Header.InsertAt(IncludePosition, ConstraintInclude + TEXT("\n"));
            }
        }
        FStructTranslationResult Requirements;
        Requirements.ModuleDependencies.Add(ConstraintClass->GetOutermost()->GetName().RightChop(8));
        FNativeGenerationContext::AddRequirements(Requirements);
        FString Metadata;
        if (bLibraryAssetBridge)
        {
            const FString& MemberName = AssetClassMembers.FindChecked(AssetClass);
            Metadata = TEXT(", meta = (LibraryAssetField)");
            MigrationClassMembers.Add(Blueprint->GeneratedClass->GetPathName() + TEXT(":LibraryAssetField:") + MemberName, MemberName);
        }
        AssetClassDeclarations += FString::Format(
            TEXT("\nUPROPERTY(EditDefaultsOnly, Category = \"Asset Classes\"{2})\nTSubclassOf<{0}> {1};\n"),
            {GetClassName(ConstraintClass), AssetClassMembers.FindChecked(AssetClass), Metadata});
    }
    if (!AssetClassDeclarations.IsEmpty())
    {
        const FString TemplateInclude = TEXT("#include \"Templates/SubclassOf.h\"");
        if (!Header.Contains(TemplateInclude))
        {
            Header.InsertAt(Header.Find(TEXT("#include")), TemplateInclude + TEXT("\n"));
        }
        const int32 Body = Header.Find(TEXT("GENERATED_BODY()"));
        if (Body == INDEX_NONE)
        {
            FNativeApiCatalog::AddError(TEXT("Cannot declare migrated asset classes without a reflected class body: ") + Blueprint->GetPathName());
        }
        else
        {
            Header.InsertAt(Body + FString(TEXT("GENERATED_BODY()")).Len(), AssetClassDeclarations);
        }
    }
    if (bLibraryAssetBridge)
    {
        const FString ClassName = UBlueprintNativizationLibrary::GetUniqueFieldName(Blueprint->GeneratedClass);
        const FString MethodName = UBlueprintNativizationLibrary::GetLambdaUniqueVariableNameByClass(
            TEXT("RefreshBlueprintLibraryAssets"), Blueprint->GeneratedClass);
        const FString DefaultsMember = UBlueprintNativizationLibrary::GetLambdaUniqueVariableNameByClass(
            TEXT("BlueprintLibraryAssetDefaults"), Blueprint->GeneratedClass);
        const FString& DataClassMember = AssetClassMembers.FindChecked(Blueprint->GeneratedClass);
        const int32 Body = Header.Find(TEXT("GENERATED_BODY()"));
        const FString PostLoadPrefix = TEXT("\nvoid ") + ClassName + TEXT("::PostLoad()\n{\nSuper::PostLoad();\n");
        const int32 PostLoadPosition = Source.Find(PostLoadPrefix, ESearchCase::CaseSensitive);
        if (Body == INDEX_NONE)
        {
            FNativeApiCatalog::AddError(TEXT("Cannot establish a Blueprint function library asset bridge without a reflected class and an available PostLoad override: ") + Blueprint->GetPathName());
        }
        else
        {
            MigrationClassMembers.Add(Blueprint->GeneratedClass->GetPathName() + TEXT(":LibraryAssetBridge:"), MethodName);
            MigrationClassMembers.Add(Blueprint->GeneratedClass->GetPathName() + TEXT(":LibraryDataClass:"), DataClassMember);
            FString Declarations = TEXT("\npublic:\nUFUNCTION()\nvoid ") + MethodName + TEXT("();\n");
            if (PostLoadPosition == INDEX_NONE)
            {
                Declarations += TEXT("virtual void PostLoad() override;\n");
            }
            Header.InsertAt(Body + FString(TEXT("GENERATED_BODY()")).Len(), Declarations +
                TEXT("private:\nUPROPERTY(Transient)\nTObjectPtr<") + ClassName + TEXT("> ") + DefaultsMember + TEXT(";\n"));
            Source += TEXT("\nvoid ") + ClassName + TEXT("::") + MethodName +
                TEXT("()\n{\nif (!HasAnyFlags(RF_ClassDefaultObject) || UObject::GetClass() != ") + DataClassMember +
                TEXT(".Get() || UObject::GetClass() == StaticClass())\n{\nreturn;\n}\n") +
                TEXT("GetMutableDefault<") + ClassName + TEXT(">()->") + DefaultsMember + TEXT(" = this;\n}\n");
            if (PostLoadPosition == INDEX_NONE)
            {
                Source += PostLoadPrefix + MethodName + TEXT("();\n}\n");
            }
            else
            {
                Source.InsertAt(PostLoadPosition + PostLoadPrefix.Len(), MethodName + TEXT("();\n"));
            }
            AdditionalIncludes += TEXT("#include \"UObject/UObjectGlobals.h\"\n");
        }
    }
    if (bRetainedReflectionUsed)
    {
        AdditionalIncludes += TEXT("#include \"UObject/UnrealType.h\"\n#include \"UObject/StructOnScope.h\"\n");
    }
    const int32 FirstIncludeEnd = Source.Find(TEXT("\n"));
    if (FirstIncludeEnd != INDEX_NONE)
    {
        Source.InsertAt(FirstIncludeEnd + 1, AdditionalIncludes);
    }
}

bool FRetainedBlueprintGeneration::UsesReflection(UFunction* Function, UClass* CallingClass)
{
    if (!Function)
    {
        return false;
    }
    if (UCallFunctionTranslatorObject::HasContainerFunctionTranslation(Function))
    {
        return false;
    }
    if (IsRetained(Function->GetOwnerClass()))
    {
        return true;
    }
    if (Function->GetOwnerClass()->HasAnyClassFlags(CLASS_Native))
    {
        FString Include;
        if (!GEngine->GetEngineSubsystem<UNativizationV2Subsystem>()->GetIncludeFromField(Function->GetOwnerClass(), Include) ||
            !FNativeApiCatalog::CanCallFunctionDirectly(Function, CallingClass))
        {
            FString Expression;
            TSet<FString> Includes;
            TSet<FString> Modules;
            return !FNativeApiCatalog::FindPublicForwarding(Function, Expression, Includes, Modules);
        }
    }
    return false;
}

bool FRetainedBlueprintGeneration::TryGenerateNativeCall(UFunction* Function, const FString& Target,
    const TArray<FString>& Arguments, FString& Invocation)
{
    if (!Function->GetOwnerClass()->HasAnyClassFlags(CLASS_Native) || !FNativeApiCatalog::CanCallFunctionDirectly(Function))
    {
        return false;
    }
    const auto Callable = FNativeApiCatalog::ResolveCallable(Function);
    FStructTranslationResult Requirements;
    FNativeGenerationContext::AddImplementationInclude(TEXT("#include \"") + Callable->GetStringField(TEXT("Include")) + TEXT("\""));
    Requirements.ModuleDependencies.Add(Callable->GetStringField(TEXT("Module")));
    FNativeGenerationContext::AddRequirements(Requirements);
    FString Receiver = Target + TEXT("->");
    FString MethodName = Function->GetName();
    TArray<FString> CallArguments = Arguments;
    if (Function->GetOwnerClass()->HasAnyClassFlags(CLASS_Interface))
    {
        const FString InterfaceName = UBlueprintNativizationLibrary::GetUniqueIInterfaceFieldName(Function->GetOwnerClass());
        if (Function->HasAnyFunctionFlags(FUNC_Event))
        {
            Receiver = InterfaceName + TEXT("::");
            MethodName = TEXT("Execute_") + MethodName;
            CallArguments.Insert(Target, 0);
        }
        else if (Function->HasAnyFunctionFlags(FUNC_Static))
        {
            Receiver = InterfaceName + TEXT("::");
        }
        else
        {
            Receiver = TEXT("Cast<") + InterfaceName + TEXT(">(") + Target + TEXT(")->");
        }
    }
    else if (Function->HasAnyFunctionFlags(FUNC_Static))
    {
        Receiver = GetClassName(Function->GetOwnerClass()) + TEXT("::");
    }
    Invocation = Receiver + MethodName + TEXT("(") + FString::Join(CallArguments, TEXT(", ")) + TEXT(")");
    return true;
}

static FString GetReflectedValueType(FProperty* Property)
{
    if (FInterfaceProperty* Interface = CastField<FInterfaceProperty>(Property))
    {
        if (Interface->InterfaceClass == UInterface::StaticClass())
        {
            return TEXT("FScriptInterface");
        }
    }
    FEdGraphPinType Type;
    GetDefault<UEdGraphSchema_K2>()->ConvertPropertyToPinType(Property, Type);
    Type.bIsReference = false;
    Type.bIsConst = false;
    if (Type.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
    {
        FString ValueType = UBlueprintNativizationLibrary::GetPropertyType(Property);
        ValueType.RemoveFromStart(TEXT("const "));
        ValueType.RemoveFromEnd(TEXT("&"));
        return ValueType.TrimStartAndEnd();
    }
    return UBlueprintNativizationLibrary::GetPinType(Type, false);
}

static FString GetTransferVariableName(const FString& Name)
{
    if (OwningBlueprint)
    {
        return UBlueprintNativizationLibrary::GetLambdaUniqueVariableNameByClass(Name, OwningBlueprint->GeneratedClass);
    }
    return Name;
}

FString FRetainedBlueprintGeneration::Transfer(FProperty* Property, const FString& Descriptor,
    const FString& Address, const FString& Value, bool bWrite)
{
    bRetainedReflectionUsed = true;
    const FString Type = GetReflectedValueType(Property);
    FString Access;
    FString Read;
    if (Property->ArrayDim != 1)
    {
        FNativeApiCatalog::AddError(TEXT("Static reflected arrays are not supported at the retained Blueprint boundary: ") + Property->GetPathName());
        return FString();
    }
    if (CastField<FBoolProperty>(Property))
    {
        Access = TEXT("CastFieldChecked<FBoolProperty>(") + Descriptor + TEXT(")");
        Read = Access + TEXT("->GetPropertyValue(") + Address + TEXT(")");
        if (bWrite)
        {
            return Access + TEXT("->SetPropertyValue(") + Address + TEXT(", ") + Value + TEXT(");\n");
        }
    }
    else if (Property->IsA<FInterfaceProperty>())
    {
        if (bWrite)
        {
            return Descriptor + TEXT("->CopySingleValue(") + Address + TEXT(", static_cast<const FScriptInterface*>(&") + Value + TEXT("));\n");
        }
        return Descriptor + TEXT("->CopySingleValue(static_cast<FScriptInterface*>(&") + Value + TEXT("), ") + Address + TEXT(");\n");
    }
    else if (FSoftObjectProperty* SoftProperty = CastField<FSoftObjectProperty>(Property))
    {
        Access = TEXT("CastFieldChecked<FSoftObjectProperty>(") + Descriptor + TEXT(")");
        Read = TEXT("typename TDecay<decltype(") + Value + TEXT(")>::Type(") + Access + TEXT("->GetPropertyValue(") + Address + TEXT(").ToSoftObjectPath())");
        if (bWrite)
        {
            return Access + TEXT("->SetPropertyValue(") + Address + TEXT(", FSoftObjectPtr(") + Value + TEXT(".ToSoftObjectPath()));\n");
        }
    }
    else if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
    {
        if (Property->IsA<FWeakObjectProperty>() || Property->IsA<FLazyObjectProperty>())
        {
            FNativeApiCatalog::AddError(TEXT("Weak and lazy properties are not supported at the retained Blueprint boundary: ") + Property->GetPathName());
            return FString();
        }
        Access = TEXT("CastFieldChecked<FObjectPropertyBase>(") + Descriptor + TEXT(")");
        FString ObjectType = GetClassName(ObjectProperty->PropertyClass);
        if (FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
        {
            ObjectType = TEXT("UClass");
            FString IncludePath;
            if (GEngine->GetEngineSubsystem<UNativizationV2Subsystem>()->GetIncludeFromField(GetStorageClass(ClassProperty->MetaClass), IncludePath))
            {
                FNativeGenerationContext::AddImplementationInclude(IncludePath);
            }
        }
        if (!CastField<FClassProperty>(Property))
        {
            ObjectType = TEXT("TPointedToType<typename TDecay<decltype(") + Value + TEXT(")>::Type>");
        }
        Read = TEXT("Cast<") + ObjectType + TEXT(">(") + Access + TEXT("->GetObjectPropertyValue(") + Address + TEXT("))");
        if (bWrite)
        {
            FString ObjectValue = Value;
            if (CastField<FClassProperty>(Property))
            {
                ObjectValue += TEXT(".Get()");
            }
            else if (ObjectProperty->PropertyClass->HasAnyClassFlags(CLASS_Const))
            {
                ObjectValue = TEXT("const_cast<UObject*>(static_cast<const UObject*>(ToRawPtr(") + Value + TEXT(")))");
            }
            return Access + TEXT("->SetObjectPropertyValue(") + Address + TEXT(", ") + ObjectValue + TEXT(");\n");
        }
    }
    else if (FArrayProperty* Array = CastField<FArrayProperty>(Property))
    {
        const FString Suffix = FString::FromInt(++TransferSequence);
        const FString Elements = GetTransferVariableName(TEXT("Elements") + Suffix);
        const FString ElementIndex = GetTransferVariableName(TEXT("ElementIndex") + Suffix);
        const FString ElementProperty = GetTransferVariableName(TEXT("ElementProperty") + Suffix);
        const FString ElementAddress = GetTransferVariableName(TEXT("ElementAddress") + Suffix);
        const FString ElementValue = GetTransferVariableName(TEXT("ElementValue") + Suffix);
        FString Code = TEXT("{\nFScriptArrayHelper ") + Elements + TEXT("(CastFieldChecked<FArrayProperty>(") + Descriptor + TEXT("), ") + Address + TEXT(");\n");
        if (bWrite)
        {
            Code += Elements + TEXT(".Resize(") + Value + TEXT(".Num());\n");
        }
        else
        {
            Code += Value + TEXT(".SetNum(") + Elements + TEXT(".Num());\n");
        }
        Code += TEXT("for (int32 ") + ElementIndex + TEXT(" = 0; ") + ElementIndex + TEXT(" < ") + Elements + TEXT(".Num(); ++") + ElementIndex + TEXT(")\n{\n");
        Code += TEXT("FProperty* ") + ElementProperty + TEXT(" = CastFieldChecked<FArrayProperty>(") + Descriptor + TEXT(")->Inner;\nvoid* ") + ElementAddress + TEXT(" = ") + Elements + TEXT(".GetRawPtr(") + ElementIndex + TEXT(");\nauto& ") + ElementValue + TEXT(" = ") + Value + TEXT("[") + ElementIndex + TEXT("];\n");
        Code += Transfer(Array->Inner, ElementProperty, ElementAddress, ElementValue, bWrite);
        return Code + TEXT("}\n}\n");
    }
    else if (Property->IsA<FSetProperty>() || Property->IsA<FMapProperty>())
    {
        FSetProperty* Set = CastField<FSetProperty>(Property);
        FMapProperty* Map = CastField<FMapProperty>(Property);
        FString PropertyType = TEXT("FMapProperty");
        FString HelperType = TEXT("FScriptMapHelper");
        FProperty* KeyProperty = nullptr;
        if (Set)
        {
            PropertyType = TEXT("FSetProperty");
            HelperType = TEXT("FScriptSetHelper");
            KeyProperty = Set->ElementProp;
        }
        else
        {
            KeyProperty = Map->KeyProp;
        }
        const FString Suffix = FString::FromInt(++TransferSequence);
        const FString Elements = GetTransferVariableName(TEXT("Elements") + Suffix);
        const FString ElementIndex = GetTransferVariableName(TEXT("ElementIndex") + Suffix);
        const FString Element = GetTransferVariableName(TEXT("Element") + Suffix);
        const FString Key = GetTransferVariableName(TEXT("KeyValue") + Suffix);
        const FString Mapped = GetTransferVariableName(TEXT("MappedValue") + Suffix);
        const FString KeyDescriptor = GetTransferVariableName(TEXT("KeyProperty") + Suffix);
        const FString ValueDescriptor = GetTransferVariableName(TEXT("ValueProperty") + Suffix);
        FString Code = TEXT("{\n") + HelperType + TEXT(" ") + Elements + TEXT("(CastFieldChecked<") + PropertyType + TEXT(">(") + Descriptor + TEXT("), ") + Address + TEXT(");\n");
        FString KeyMember = TEXT("KeyProp");
        FString KeyAddress = Elements + TEXT(".GetKeyPtr(") + ElementIndex + TEXT(")");
        if (Set)
        {
            KeyMember = TEXT("ElementProp");
            KeyAddress = Elements + TEXT(".GetElementPtr(") + ElementIndex + TEXT(")");
        }
        Code += TEXT("FProperty* ") + KeyDescriptor + TEXT(" = CastFieldChecked<") + PropertyType + TEXT(">(") + Descriptor + TEXT(")->") + KeyMember + TEXT(";\n");
        if (Map)
        {
            Code += TEXT("FProperty* ") + ValueDescriptor + TEXT(" = CastFieldChecked<FMapProperty>(") + Descriptor + TEXT(")->ValueProp;\n");
        }
        if (bWrite)
        {
            if (Set)
            {
                Code += Elements + TEXT(".EmptyElements();\n");
            }
            else
            {
                Code += Elements + TEXT(".EmptyValues();\n");
            }
            Code += TEXT("for (const auto& ") + Element + TEXT(" : ") + Value + TEXT(")\n{\nint32 ") + ElementIndex + TEXT(" = ") + Elements + TEXT(".AddDefaultValue_Invalid_NeedsRehash();\n");
            FString KeyValue = Element;
            if (Map)
            {
                KeyValue += TEXT(".Key");
            }
            Code += Transfer(KeyProperty, KeyDescriptor, KeyAddress, KeyValue, true);
            if (Map)
            {
                Code += Transfer(Map->ValueProp, ValueDescriptor, Elements + TEXT(".GetValuePtr(") + ElementIndex + TEXT(")"), Element + TEXT(".Value"), true);
            }
            Code += TEXT("}\n") + Elements + TEXT(".Rehash();\n");
        }
        else
        {
            Code += Value + TEXT(".Reset();\nfor (int32 ") + ElementIndex + TEXT(" = 0; ") + ElementIndex + TEXT(" < ") + Elements + TEXT(".GetMaxIndex(); ++") + ElementIndex + TEXT(")\n{\nif (!") + Elements + TEXT(".IsValidIndex(") + ElementIndex + TEXT("))\n{\ncontinue;\n}\n");
            FString KeyType = TEXT("typename TDecay<decltype(") + Value + TEXT(")>::Type::KeyType");
            if (Set)
            {
                KeyType = TEXT("typename TDecay<decltype(") + Value + TEXT(")>::Type::ElementType");
            }
            Code += KeyType + TEXT(" ") + Key + TEXT("{};\n");
            Code += Transfer(KeyProperty, KeyDescriptor, KeyAddress, Key, false);
            if (Map)
            {
                Code += TEXT("typename TDecay<decltype(") + Value + TEXT(")>::Type::ValueType ") + Mapped + TEXT("{};\n");
                Code += Transfer(Map->ValueProp, ValueDescriptor, Elements + TEXT(".GetValuePtr(") + ElementIndex + TEXT(")"), Mapped, false);
                Code += Value + TEXT(".Add(MoveTemp(") + Key + TEXT("), MoveTemp(") + Mapped + TEXT("));\n");
            }
            else
            {
                Code += Value + TEXT(".Add(MoveTemp(") + Key + TEXT("));\n");
            }
            Code += TEXT("}\n");
        }
        return Code + TEXT("}\n");
    }
    else if (FStructProperty* Structure = CastField<FStructProperty>(Property))
    {
        if (IsExternalStructure(Structure->Struct))
        {
            FNativeGenerationContext::AddImplementationInclude(TEXT("#include \"StructUtils/InstancedStruct.h\""));
            FNativeGenerationContext::AddImplementationInclude(TEXT("#include \"UObject/StructOnScope.h\""));
            const FString RuntimeStructure = TEXT("CastFieldChecked<FStructProperty>(") + Descriptor + TEXT(")->Struct");
            if (bWrite)
            {
                return TEXT("if ((") + Value + TEXT(").IsValid())\n{\ncheck((") + Value + TEXT(").GetScriptStruct() == ") + RuntimeStructure +
                    TEXT(");\n") + Descriptor + TEXT("->CopySingleValue(") + Address + TEXT(", (") + Value + TEXT(").GetMemory());\n}\nelse\n{\nFStructOnScope DefaultValue(") +
                    RuntimeStructure + TEXT(");\n") + Descriptor + TEXT("->CopySingleValue(") + Address + TEXT(", DefaultValue.GetStructMemory());\n}\n");
            }
            return TEXT("(") + Value + TEXT(").InitializeAs(") + RuntimeStructure + TEXT(", static_cast<const uint8*>(") + Address + TEXT("));\n");
        }
        if (Structure->Struct->IsA<UUserDefinedStruct>())
        {
            if (!GeneratedValueTypes.Contains(Structure->Struct))
            {
                const FString ExclusionReason = FEngineAssetPolicy::GetExclusionReason(Structure->Struct,
                    GetDefault<UBlueprintNativizationV2EditorSettings>()->bExcludeEngineAssetsFromConversion);
                if (!ExclusionReason.IsEmpty())
                {
                    FNativeApiCatalog::AddError(TEXT("Cannot marshal a retained Blueprint API structure excluded by the Engine asset policy: ") +
                        Structure->Struct->GetPathName() + TEXT(". The structure remains an external Blueprint asset and has no generated C++ value type. Property: ") + Property->GetPathName());
                }
                else
                {
                    FNativeApiCatalog::AddError(TEXT("Include the structure used by the retained Blueprint API in generation: ") + Structure->Struct->GetPathName() + TEXT(". Property: ") + Property->GetPathName());
                }
                return FString();
            }
            FString Code = TEXT("{\n");
            for (TFieldIterator<FProperty> Member(Structure->Struct); Member; ++Member)
            {
                const FString Suffix = FString::FromInt(++TransferSequence);
                const FString MemberProperty = TEXT("MemberProperty") + Suffix;
                const FString MemberAddress = TEXT("MemberAddress") + Suffix;
                const FString MemberValue = TEXT("MemberValue") + Suffix;
                const FString NativeMemberName = UBlueprintNativizationLibrary::GetUniquePropertyName(*Member);
                Code += TEXT("{\nFProperty* ") + MemberProperty + TEXT(" = FindFProperty<FProperty>(CastFieldChecked<FStructProperty>(") + Descriptor + TEXT(")->Struct, ") + Quote(NativeMemberName) + TEXT(");\n");
                if (NativeMemberName != Member->GetName())
                {
                    Code += TEXT("if (!") + MemberProperty + TEXT(")\n{\n") + MemberProperty +
                        TEXT(" = FindFProperty<FProperty>(CastFieldChecked<FStructProperty>(") + Descriptor + TEXT(")->Struct, ") +
                        Quote(Member->GetName()) + TEXT(");\n}\n");
                }
                Code += TEXT("check(") + MemberProperty + TEXT(");\nvoid* ") + MemberAddress + TEXT(" = ") + MemberProperty + TEXT("->ContainerPtrToValuePtr<void>(") + Address + TEXT(");\n");
                Code += TEXT("auto& ") + MemberValue + TEXT(" = ") + Value + TEXT(".") + UBlueprintNativizationLibrary::GetUniquePropertyName(*Member) + TEXT(";\n");
                Code += Transfer(*Member, MemberProperty, MemberAddress, MemberValue, bWrite);
                Code += TEXT("}\n");
            }
            return Code + TEXT("}\n");
        }
        Access = Descriptor;
        if (bWrite)
        {
            return Access + TEXT("->CopySingleValue(") + Address + TEXT(", &") + Value + TEXT(");\n");
        }
        return Access + TEXT("->CopySingleValue(&") + Value + TEXT(", ") + Address + TEXT(");\n");
    }
    else if (Property->IsA<FStrProperty>() || Property->IsA<FNameProperty>() || Property->IsA<FTextProperty>())
    {
        if (bWrite)
        {
            const FString TransferValue = GetTransferVariableName(TEXT("TransferredValue"));
            return TEXT("{\nconst ") + Type + TEXT(" ") + TransferValue + TEXT("(") + Value + TEXT(");\n") +
                Descriptor + TEXT("->CopySingleValue(") + Address + TEXT(", &") + TransferValue + TEXT(");\n}\n");
        }
        return Descriptor + TEXT("->CopySingleValue(&") + Value + TEXT(", ") + Address + TEXT(");\n");
    }
    else if (Property->IsA<FNumericProperty>() || Property->IsA<FEnumProperty>())
    {
        FString Numeric = TEXT("CastFieldChecked<FNumericProperty>(") + Descriptor + TEXT(")");
        FString NumericSetup;
        FString NumericEnd;
        const FByteProperty* ByteProperty = CastField<FByteProperty>(Property);
        if (Property->IsA<FEnumProperty>() || (ByteProperty && ByteProperty->Enum))
        {
            Numeric = GetTransferVariableName(TEXT("NumericProperty"));
            const FString Enumeration = GetTransferVariableName(TEXT("EnumProperty"));
            NumericSetup = TEXT("{\nFNumericProperty* ") + Numeric + TEXT(" = CastField<FNumericProperty>(") + Descriptor + TEXT(");\n") +
                TEXT("if (FEnumProperty* ") + Enumeration + TEXT(" = CastField<FEnumProperty>(") + Descriptor + TEXT("))\n{\n") +
                Numeric + TEXT(" = ") + Enumeration + TEXT("->GetUnderlyingProperty();\n}\ncheck(") + Numeric + TEXT(");\n");
            NumericEnd = TEXT("}\n");
        }
        FNumericProperty* Number = CastField<FNumericProperty>(Property);
        const bool bFloating = Number && Number->IsFloatingPoint();
        if (bWrite)
        {
            if (bFloating)
            {
                return NumericSetup + Numeric + TEXT("->SetFloatingPointPropertyValue(") + Address + TEXT(", static_cast<double>(") + Value + TEXT("));\n") + NumericEnd;
            }
            return NumericSetup + Numeric + TEXT("->SetIntPropertyValue(") + Address + TEXT(", static_cast<uint64>(") + Value + TEXT("));\n") + NumericEnd;
        }
        FString Getter = TEXT("GetSignedIntPropertyValue");
        if (bFloating)
        {
            Getter = TEXT("GetFloatingPointPropertyValue");
        }
        else if (Number && (Number->IsA<FByteProperty>() || Number->IsA<FUInt16Property>() || Number->IsA<FUInt32Property>() || Number->IsA<FUInt64Property>()))
        {
            Getter = TEXT("GetUnsignedIntPropertyValue");
        }
        FString NumericValue = Numeric + TEXT("->") + Getter + TEXT("(") + Address + TEXT(")");
        if (Property->IsA<FByteProperty>() && Type.StartsWith(TEXT("TEnumAsByte<")))
        {
            NumericValue = TEXT("static_cast<uint8>(") + NumericValue + TEXT(")");
        }
        Read = TEXT("static_cast<") + Type + TEXT(">(") + NumericValue + TEXT(")");
        return NumericSetup + Value + TEXT(" = ") + Read + TEXT(";\n") + NumericEnd;
    }
    else
    {
        FNativeApiCatalog::AddError(TEXT("Unsupported reflected value at the retained Blueprint boundary: ") + Property->GetPathName());
        return FString();
    }
    return Value + TEXT(" = ") + Read + TEXT(";\n");
}

FString FRetainedBlueprintGeneration::ReadProperty(FProperty* Property, const FString& Target)
{
    const FString Type = GetReflectedValueType(Property);
    FString Code = TEXT("([](UObject* PropertyTarget) -> ") + Type + TEXT("\n{\n") + Type + TEXT(" PropertyValue{};\nif (IsValid(PropertyTarget))\n{\n");
    Code += TEXT("if (FProperty* Property = FindFProperty<FProperty>(PropertyTarget->GetClass(), ") + Quote(Property->GetName()) + TEXT("))\n{\nvoid* PropertyAddress = Property->ContainerPtrToValuePtr<void>(PropertyTarget);\n");
    Code += Transfer(Property, TEXT("Property"), TEXT("PropertyAddress"), TEXT("PropertyValue"), false);
    return Code + TEXT("}\n}\nreturn PropertyValue;\n}(") + Target + TEXT("))");
}

FString FRetainedBlueprintGeneration::PreparePropertyRead(FProperty* Property, const FString& Target, UK2Node* Node,
    UNativizationV2Subsystem* Subsystem, FGenerationPreparations& Preparations)
{
    TGuardValue<int32> TransferScope(TransferSequence, 0);
    const FString Type = GetReflectedValueType(Property);
    FString ResultName = Subsystem->GetNodeStateName(Node, TEXT("Read") + Property->GetName());
    if (Subsystem->DataConsumer)
    {
        ResultName = Subsystem->GetNodeStateName(Subsystem->DataConsumer, Subsystem->InputEvaluationPurpose + ResultName);
    }
    FString Body = Type + TEXT(" PropertyValue{};\nif (IsValid(PropertyTarget))\n{\n");
    Body += TEXT("if (FProperty* Property = FindFProperty<FProperty>(PropertyTarget->GetClass(), ") +
        Quote(Property->GetName()) + TEXT("))\n{\nvoid* PropertyAddress = Property->ContainerPtrToValuePtr<void>(PropertyTarget);\n");
    Body += Transfer(Property, TEXT("Property"), TEXT("PropertyAddress"), TEXT("PropertyValue"), false);
    Body += TEXT("}\n}\n");
    const FString ExpandedCode = TEXT("{\nUObject* PropertyTarget = ") + Target + TEXT(";\n") +
        Body + ResultName + TEXT(" = PropertyValue;\n}\n");
    Preparations.Add(Type + TEXT(" ") + ResultName + TEXT("{};\n") + ExpandedCode);
    return ResultName;
}

FString FRetainedBlueprintGeneration::WriteProperty(FProperty* Property, const FString& Target, const FString& Value)
{
    const FString Type = GetReflectedValueType(Property);
    FString Code = TEXT("{\nUObject* PropertyTarget = ") + Target + TEXT(";\n") + Type +
        TEXT(" const& PropertyValue = ") + Value + TEXT(";\nif (IsValid(PropertyTarget))\n{\n");
    Code += TEXT("if (FProperty* Property = FindFProperty<FProperty>(PropertyTarget->GetClass(), ") + Quote(Property->GetName()) + TEXT("))\n{\nvoid* PropertyAddress = Property->ContainerPtrToValuePtr<void>(PropertyTarget);\n");
    Code += Transfer(Property, TEXT("Property"), TEXT("PropertyAddress"), TEXT("PropertyValue"), true);
    return Code + TEXT("}\n}\n}\n");
}

FString FRetainedBlueprintGeneration::CallPropertyAccessor(FProperty* Property, UFunction* Function,
    const FString& Target, const FString& Value, bool bSetter)
{
    FString Name = UBlueprintNativizationLibrary::GetUniqueFunctionName(Function, TEXT(""));
    if (IsRetained(Function->GetOwnerClass()))
    {
        Name = Function->GetName();
    }
    const FString Type = GetReflectedValueType(Property);
    TArray<FString> Arguments;
    if (bSetter)
    {
        Arguments.Add(Value);
    }
    FString NativeInvocation;
    if (TryGenerateNativeCall(Function, TEXT("AccessorTarget"), Arguments, NativeInvocation))
    {
        FString NativeCode = TEXT("([&]()");
        if (!bSetter)
        {
            NativeCode += TEXT(" -> ") + Type;
        }
        NativeCode += TEXT("\n{\nconst auto AccessorTarget = ") + Target + TEXT(";\nif (IsValid(AccessorTarget))\n{\n");
        if (bSetter)
        {
            NativeCode += NativeInvocation + TEXT(";\n}\n}())");
        }
        else
        {
            NativeCode += TEXT("return ") + NativeInvocation + TEXT(";\n}\nreturn {};\n}())");
        }
        return NativeCode;
    }
    FString Code = TEXT("([&]()");
    if (!bSetter)
    {
        Code += TEXT(" -> ") + Type;
    }
    Code += TEXT("\n{\nUObject* AccessorTarget = ") + Target + TEXT(";\n");
    if (!bSetter)
    {
        Code += Type + TEXT(" AccessorValue{};\n");
    }
    Code += TEXT("if (IsValid(AccessorTarget))\n{\nif (UFunction* AccessorFunction = AccessorTarget->FindFunction(") + Quote(Name) +
        TEXT("))\n{\nFStructOnScope AccessorParameters(AccessorFunction);\n");
    FProperty* Parameter = Function->GetReturnProperty();
    if (bSetter)
    {
        for (TFieldIterator<FProperty> Candidate(Function); Candidate; ++Candidate)
        {
            if (Candidate->HasAnyPropertyFlags(CPF_Parm) && !Candidate->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                Parameter = *Candidate;
                break;
            }
        }
    }
    Code += TEXT("FProperty* AccessorParameter = FindFProperty<FProperty>(AccessorFunction, ") + Quote(Parameter->GetName()) +
        TEXT(");\nvoid* AccessorAddress = AccessorParameter->ContainerPtrToValuePtr<void>(AccessorParameters.GetStructMemory());\n");
    if (bSetter)
    {
        Code += Type + TEXT(" const& AccessorValue = ") + Value + TEXT(";\n") +
            Transfer(Property, TEXT("AccessorParameter"), TEXT("AccessorAddress"), TEXT("AccessorValue"), true);
    }
    Code += TEXT("AccessorTarget->ProcessEvent(AccessorFunction, AccessorParameters.GetStructMemory());\n");
    if (!bSetter)
    {
        Code += Transfer(Property, TEXT("AccessorParameter"), TEXT("AccessorAddress"), TEXT("AccessorValue"), false);
    }
    Code += TEXT("}\n}\n");
    if (!bSetter)
    {
        Code += TEXT("return AccessorValue;\n");
    }
    return Code + TEXT("}())");
}

FGenerateResultStruct FRetainedBlueprintGeneration::Call(UK2Node_CallFunction* Node, const FString& Target,
    TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem)
{
    UFunction* Function = UBlueprintNativizationLibrary::GetFunctionDeclaration(Node->GetTargetFunction());
    FGenerateResultStruct Result;
    const FString ReceiverName = Subsystem->GetNodeStateName(Node, TEXT("BlueprintTarget"));
    const FString FunctionName = Subsystem->GetNodeStateName(Node, TEXT("BlueprintFunction"));
    const FString ParametersName = Subsystem->GetNodeStateName(Node, TEXT("BlueprintParameters"));
    const FString ReturnName = Subsystem->GetNodeStateName(Node, TEXT("BlueprintReturnValue"), true);
    FString ReturnType = TEXT("void");
    FString ReturnDeclaration;
    if (FProperty* ReturnProperty = Function->GetReturnProperty())
    {
        ReturnType = UBlueprintNativizationLibrary::GetPropertyType(ReturnProperty);
        UEdGraphPin* ReturnPin = Node->GetReturnValuePin();
        if (ReturnPin && (ReturnPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class ||
            ReturnPin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass))
        {
            FEdGraphPinType ReturnPinType = ReturnPin->PinType;
            ReturnPinType.bIsReference = false;
            ReturnPinType.bIsConst = false;
            ReturnType = UBlueprintNativizationLibrary::GetPinType(ReturnPinType, false);
        }
        ReturnDeclaration = ReturnType + TEXT(" ") + ReturnName + TEXT("{};\n");
    }
    FString InputValues;
    FString InputTransfers;
    FString OutputTransfers;
    TSet<FProperty*> WritableProperties;
    for (TFieldIterator<FProperty> Parameter(Function); Parameter && Parameter->HasAnyPropertyFlags(CPF_Parm); ++Parameter)
    {
        FProperty* Property = *Parameter;
        const bool bReturn = Property->HasAnyPropertyFlags(CPF_ReturnParm);
        const bool bReadOnlyClassFilter = FNativeFunctionExpressions::IsClassFilterParameter(Function, Property->GetFName());
        const bool bWriteBack = !bReadOnlyClassFilter && !bReturn && Property->HasAnyPropertyFlags(CPF_OutParm) && !Property->HasAnyPropertyFlags(CPF_ConstParm);
        UEdGraphPin* InputPin = Node->FindPin(Property->GetFName(), EGPD_Input);
        UEdGraphPin* OutputPin = Node->FindPin(Property->GetFName(), EGPD_Output);
        const FString LocalName = Subsystem->GetNodeStateName(Node, TEXT("Argument") + Property->GetName());
        const FString DescriptorName = LocalName + TEXT("Property");
        const FString AddressName = LocalName + TEXT("Address");
        FString Lookup = TEXT("FProperty* ") + DescriptorName + TEXT(" = FindFProperty<FProperty>(") + FunctionName + TEXT(", ") + Quote(Property->GetName()) + TEXT(");\n");
        Lookup += TEXT("check(") + DescriptorName + TEXT(");\nvoid* ") + AddressName + TEXT(" = ") + DescriptorName + TEXT("->ContainerPtrToValuePtr<void>(") + ParametersName + TEXT(".GetStructMemory());\n");
        FString LocalType = GetReflectedValueType(Property);
        UEdGraphPin* ValuePin = InputPin;
        if (!ValuePin)
        {
            ValuePin = OutputPin;
        }
        if (ValuePin)
        {
            FEdGraphPinType ValueType = ValuePin->PinType;
            if (InputPin && !InputPin->LinkedTo.IsEmpty())
            {
                UEdGraphPin* SourcePin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(InputPin, 0, true, MacroStack, Subsystem);
                if (SourcePin && SourcePin->PinType.PinCategory != UEdGraphSchema_K2::PC_Wildcard)
                {
                    ValueType = SourcePin->PinType;
                }
            }
            ValueType.bIsReference = false;
            ValueType.bIsConst = false;
            LocalType = UBlueprintNativizationLibrary::GetPinType(ValueType, false);
        }
        if (InputPin && !bWriteBack && CastField<FObjectProperty>(Property) &&
            CastFieldChecked<FObjectProperty>(Property)->PropertyClass->HasAnyClassFlags(CLASS_Const))
        {
            LocalType = TEXT("const ") + LocalType;
        }
        FString WriteBack;
        if (InputPin)
        {
            FString InputCode;
            UEdGraphPin* Source = nullptr;
            if (!InputPin->LinkedTo.IsEmpty())
            {
                Source = UBlueprintNativizationLibrary::GetFirstNonKnotPin(InputPin, 0, true, MacroStack, Subsystem);
            }
            if (bWriteBack && Source && !Source->GetOwningNode()->IsA<UK2Node_MakeContainer>() &&
                (Source->GetOwningNode()->IsA<UK2Node_VariableGet>() || Source->PinType.bIsReference))
            {
                UK2Node_VariableGet* Variable = Cast<UK2Node_VariableGet>(Source->GetOwningNode());
                if (Source->ParentPin)
                {
                    FNativeApiCatalog::AddError(TEXT("Reference writeback requires an unsplit variable at the retained Blueprint boundary: ") + Property->GetPathName());
                    continue;
                }
                if (!Variable)
                {
                    FGenerateResultStruct Destination = Subsystem->GenerateInputParameterCodeForNode(Node, InputPin, 0, MacroStack);
                    Result.Preparations.Append(Destination.Preparations);
                    const FString DestinationName = LocalName + TEXT("Destination");
                    InputValues += TEXT("auto& ") + DestinationName + TEXT(" = ") + Destination.Code + TEXT(";\n");
                    InputCode = DestinationName;
                    WriteBack = DestinationName + TEXT(" = ") + LocalName + TEXT(";\n");
                }
                else
                {
                    FProperty* VariableProperty = Variable->GetPropertyForVariable();
                    if (!VariableProperty)
                    {
                        FNativeApiCatalog::AddError(TEXT("Cannot resolve a reference argument variable: ") + Variable->GetPathName());
                        continue;
                    }
                    if (WritableProperties.Contains(VariableProperty))
                    {
                        FNativeApiCatalog::AddError(TEXT("Aliased reference parameters require an explicit temporary: ") + VariableProperty->GetPathName());
                        continue;
                    }
                    WritableProperties.Add(VariableProperty);
                    if (IsRetained(VariableProperty->GetOwnerClass()))
                    {
                        FString Receiver = TEXT("this");
                        UEdGraphPin* ReceiverPin = Variable->FindPin(UEdGraphSchema_K2::PN_Self);
                        if (ReceiverPin && (!ReceiverPin->LinkedTo.IsEmpty() || ReceiverPin->DefaultObject))
                        {
                            FGenerateResultStruct ReceiverValue = Subsystem->GenerateInputParameterCodeForNode(Variable, ReceiverPin, 0, MacroStack);
                            Result.Preparations.Append(ReceiverValue.Preparations);
                            Receiver = ReceiverValue.Code;
                        }
                        const FString OwnerName = LocalName + TEXT("Owner");
                        InputValues += TEXT("auto* ") + OwnerName + TEXT(" = ") + Receiver + TEXT(";\n");
                        InputCode = ReadProperty(VariableProperty, OwnerName);
                        WriteBack = WriteProperty(VariableProperty, OwnerName, LocalName);
                    }
                    else
                    {
                        FGenerateResultStruct Destination = UBlueprintNativizationLibrary::GenerateVariableAccess(Variable, MacroStack, Subsystem);
                        Result.Preparations.Append(Destination.Preparations);
                        const FString DestinationName = LocalName + TEXT("Destination");
                        InputValues += TEXT("auto& ") + DestinationName + TEXT(" = ") + Destination.Code + TEXT(";\n");
                        InputCode = DestinationName;
                        WriteBack = DestinationName + TEXT(" = ") + LocalName + TEXT(";\n");
                    }
                }
            }
            else
            {
                if (Function->HasAnyFunctionFlags(FUNC_Static) && InputPin->LinkedTo.IsEmpty() &&
                    Property->GetName() == Function->GetMetaData(TEXT("WorldContext")))
                {
                    InputCode = TEXT("this");
                    if (Node->GetBlueprint()->BlueprintType == BPTYPE_FunctionLibrary)
                    {
                        TArray<UK2Node_FunctionEntry*> Entries;
                        Node->GetGraph()->GetNodesOfClass(Entries);
                        for (UK2Node_FunctionEntry* Entry : Entries)
                        {
                            if (UEdGraphPin* ContextPin = Entry->FindPin(TEXT("__WorldContext")))
                            {
                                InputCode = UBlueprintNativizationLibrary::GetUniquePinName(ContextPin, Subsystem->EntryNodes);
                                break;
                            }
                        }
                    }
                }
                else
                {
                    FGenerateResultStruct Input = Subsystem->GenerateInputParameterCodeForNode(Node, InputPin, 0, MacroStack);
                    Result.Preparations.Append(Input.Preparations);
                    InputCode = Input.Code;
                }
            }
            InputValues += LocalType + TEXT(" ") + LocalName + TEXT(" = ") + InputCode + TEXT(";\n");
            if (bReadOnlyClassFilter && InputPin->PinType.IsArray())
            {
                const FString ElementName = LocalName + TEXT("FilterClass");
                const FString Resolved = ResolveClassFilterValue(ElementName,
                    Cast<UClass>(InputPin->PinType.PinSubCategoryObject.Get()));
                InputValues += TEXT("for (auto& ") + ElementName + TEXT(" : ") + LocalName + TEXT(")\n{\n") +
                    ElementName + TEXT(" = ") + Resolved + TEXT(";\n}\n");
            }
            InputTransfers += TEXT("{\n") + Lookup + Transfer(Property, DescriptorName, AddressName, LocalName, true) + TEXT("}\n");
        }
        else if (!bReturn && OutputPin)
        {
            InputValues += LocalType + TEXT(" ") + LocalName + TEXT("{};\n");
            InputValues += UBlueprintNativizationLibrary::GetUniquePinName(OutputPin, Subsystem->EntryNodes) + TEXT(" = {};\n");
        }
        if (bReturn)
        {
            OutputTransfers += TEXT("{\n") + Lookup + Transfer(Property, DescriptorName, AddressName, ReturnName, false) + TEXT("}\n");
        }
        else if (bWriteBack)
        {
            if (!InputPin && !OutputPin)
            {
                FNativeApiCatalog::AddError(TEXT("Missing output pin for retained Blueprint function: ") + Property->GetPathName());
                continue;
            }
            OutputTransfers += TEXT("{\n") + Lookup + Transfer(Property, DescriptorName, AddressName, LocalName, false) + TEXT("}\n");
            if (OutputPin)
            {
                OutputTransfers += UBlueprintNativizationLibrary::GetUniquePinName(OutputPin, Subsystem->EntryNodes) + TEXT(" = ") + LocalName + TEXT(";\n");
            }
            OutputTransfers += WriteBack;
        }
    }
    FString Receiver = Target;
    if (Receiver.IsEmpty())
    {
        Receiver = TEXT("this");
    }
    if (Function->GetOwnerClass()->HasAnyClassFlags(CLASS_Interface))
    {
        UEdGraphPin* SelfPin = Node->FindPin(UEdGraphSchema_K2::PN_Self);
        UEdGraphPin* SourcePin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(SelfPin, 0, true, MacroStack, Subsystem);
        if ((SourcePin && SourcePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Interface) ||
            (!SourcePin && SelfPin && Receiver != TEXT("this") && SelfPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Interface))
        {
            Receiver = TEXT("(") + Receiver + TEXT(").GetObject()");
        }
    }
    FString ReceiverPreparation;
    if (Function->HasAnyFunctionFlags(FUNC_Static))
    {
        const FString ReceiverClassName = ReceiverName + TEXT("Class");
        FString ReceiverClass;
        if (Function->GetOwnerClass()->HasAnyClassFlags(CLASS_Native))
        {
            ReceiverClass = TEXT("LoadClass<UObject>(nullptr, ") + Quote(Function->GetOwnerClass()->GetPathName()) + TEXT(")");
        }
        else
        {
            ReceiverClass = GetClassValue(Function->GetOwnerClass());
        }
        ReceiverPreparation = TEXT("UClass* ") + ReceiverClassName + TEXT(" = ") + ReceiverClass +
            TEXT(";\nUObject* ") + ReceiverName + TEXT("{};\nif (") + ReceiverClassName + TEXT(")\n{\n") +
            ReceiverName + TEXT(" = ") + ReceiverClassName + TEXT("->GetDefaultObject();\n}\n");
    }
    else
    {
        ReceiverPreparation = TEXT("UObject* ") + ReceiverName + TEXT(" = ") + Receiver + TEXT(";\n");
    }
    bRetainedReflectionUsed = true;
    Result.Code = TEXT("{\n") + ReceiverPreparation + InputValues;
    Result.Code += TEXT("if (IsValid(") + ReceiverName + TEXT("))\n{\nif (UFunction* ") + FunctionName + TEXT(" = ") + ReceiverName + TEXT("->FindFunction(") + Quote(Function->GetName()) + TEXT("))\n{\nFStructOnScope ") + ParametersName + TEXT("(") + FunctionName + TEXT(");\n");
    Result.Code += InputTransfers + ReceiverName + TEXT("->ProcessEvent(") + FunctionName + TEXT(", ") + ParametersName + TEXT(".GetStructMemory());\n") + OutputTransfers;
    Result.Code += TEXT("}\n}\n}\n");
    if (ReturnType == TEXT("void"))
    {
        Result.Code = ReturnDeclaration + Result.Code;
    }
    else
    {
        Result.Preparations.Add(ReturnDeclaration + Result.Code);
        Result.Code = ReturnName;
    }
    return Result;
}

void FRetainedBlueprintGeneration::PrepareNativeArguments(UK2Node_CallFunction* Node,
    TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem, TMap<FName, FString>& Overrides,
    FGenerationPreparations& Preparations, FString& Before, FString& After)
{
    UFunction* Function = UBlueprintNativizationLibrary::GetFunctionDeclaration(Node->GetTargetFunction());
    TSet<FProperty*> WritableProperties;
    const TSet<FName> ContainerWriters = { TEXT("Map_Add"), TEXT("Map_Remove"), TEXT("Map_Clear"),
        TEXT("Set_Add"), TEXT("Set_Remove"), TEXT("Set_Clear"), TEXT("Set_AddItems"), TEXT("Set_RemoveItems") };
    for (TFieldIterator<FProperty> Parameter(Function); Parameter && Parameter->HasAnyPropertyFlags(CPF_Parm); ++Parameter)
    {
        UEdGraphPin* Input = Node->FindPin(Parameter->GetFName(), EGPD_Input);
        UEdGraphPin* Output = Node->FindPin(Parameter->GetFName(), EGPD_Output);
        if (!Input && Output && Parameter->HasAnyPropertyFlags(CPF_OutParm) &&
            !Parameter->HasAnyPropertyFlags(CPF_ReturnParm | CPF_ConstParm) &&
            Output->PinType.PinCategory == UEdGraphSchema_K2::PC_Object)
        {
            FProperty* ValueProperty = *Parameter;
            if (FArrayProperty* OutputArray = CastField<FArrayProperty>(ValueProperty))
            {
                ValueProperty = OutputArray->Inner;
            }
            FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(ValueProperty);
            UClass* OutputClass = Cast<UClass>(Output->PinType.PinSubCategoryObject.Get());
            if (ObjectProperty && !ValueProperty->IsA<FClassProperty>() && OutputClass &&
                OutputClass != ObjectProperty->PropertyClass && OutputClass->IsChildOf(ObjectProperty->PropertyClass) &&
                (Output->PinType.IsArray() || !Output->PinType.IsContainer()))
            {
                const FString ArgumentName = Subsystem->GetNodeStateName(Node, Parameter->GetName() + TEXT("NativeOutput"), true);
                const FString OutputName = UBlueprintNativizationLibrary::GetUniquePinName(Output, Subsystem->EntryNodes);
                Before += GetReflectedValueType(*Parameter) + TEXT(" ") + ArgumentName + TEXT("{};\n");
                Overrides.Add(Parameter->GetFName(), ArgumentName);
                if (Output->PinType.IsArray())
                {
                    After += OutputName + TEXT(".Reset();\nfor (auto* Element : ") + ArgumentName + TEXT(")\n{\n") +
                        OutputName + TEXT(".Add(Cast<") + GetClassName(OutputClass) + TEXT(">(Element));\n}\n");
                }
                else
                {
                    After += OutputName + TEXT(" = Cast<") + GetClassName(OutputClass) + TEXT(">(") + ArgumentName + TEXT(");\n");
                }
                continue;
            }
        }
        if (!Input || Input->LinkedTo.IsEmpty())
        {
            continue;
        }
        UEdGraphPin* SourcePin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Input, 0, true, MacroStack, Subsystem);
        FArrayProperty* ArrayParameter = CastField<FArrayProperty>(*Parameter);
        FObjectPropertyBase* ElementProperty = nullptr;
        if (ArrayParameter)
        {
            ElementProperty = CastField<FObjectPropertyBase>(ArrayParameter->Inner);
        }
        UClass* SourceClass = nullptr;
        if (SourcePin && SourcePin->PinType.IsArray() && SourcePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object)
        {
            SourceClass = Cast<UClass>(SourcePin->PinType.PinSubCategoryObject.Get());
        }
        if (SourceClass && ElementProperty && !ArrayParameter->Inner->IsA<FClassProperty>() &&
            SourceClass != ElementProperty->PropertyClass && SourceClass->IsChildOf(ElementProperty->PropertyClass) &&
            Function->GetOwnerClass()->GetName() != TEXT("KismetArrayLibrary"))
        {
            FGenerateResultStruct Source = Subsystem->GenerateInputParameterCodeForNode(Node, Input, 0, MacroStack);
            Preparations.Append(Source.Preparations);
            const FString SourceName = Subsystem->GetNodeStateName(Node, Parameter->GetName() + TEXT("Source"), true);
            const FString ArgumentName = Subsystem->GetNodeStateName(Node, Parameter->GetName() + TEXT("Converted"), true);
            Before += TEXT("auto&& ") + SourceName + TEXT(" = ") + Source.Code + TEXT(";\n") +
                GetReflectedValueType(*Parameter) + TEXT(" ") + ArgumentName + TEXT(";\n") + ArgumentName + TEXT(".Reserve(") + SourceName +
                TEXT(".Num());\nfor (auto* Element : ") + SourceName + TEXT(")\n{\n") + ArgumentName + TEXT(".Add(Element);\n}\n");
            Overrides.Add(Parameter->GetFName(), ArgumentName);
            if (Parameter->HasAnyPropertyFlags(CPF_OutParm | CPF_ReferenceParm) && !Parameter->HasAnyPropertyFlags(CPF_ConstParm))
            {
                After += SourceName + TEXT(".Reset();\nfor (auto* Element : ") + ArgumentName + TEXT(")\n{\n") + SourceName +
                    TEXT(".Add(Cast<") + GetClassName(SourceClass) + TEXT(">(Element));\n}\n");
            }
            continue;
        }
        UK2Node_VariableGet* Variable = Cast<UK2Node_VariableGet>(Input->LinkedTo[0]->GetOwningNode());
        if (!Variable)
        {
            continue;
        }
        FProperty* Property = Variable->GetPropertyForVariable();
        if (!Property || !IsRetained(Property->GetOwnerClass()))
        {
            continue;
        }
        const bool bWrite = (Parameter->HasAnyPropertyFlags(CPF_OutParm | CPF_ReferenceParm) && !Parameter->HasAnyPropertyFlags(CPF_ConstParm)) ||
            (ContainerWriters.Contains(Function->GetFName()) &&
                (Input->PinName == TEXT("TargetMap") || Input->PinName == TEXT("TargetSet")));
        if (!bWrite)
        {
            continue;
        }
        if (WritableProperties.Contains(Property))
        {
            FNativeApiCatalog::AddError(TEXT("Aliased writable Blueprint properties require an explicit temporary: ") + Property->GetPathName());
            continue;
        }
        WritableProperties.Add(Property);
        if (Input->LinkedTo[0]->ParentPin)
        {
            FNativeApiCatalog::AddError(TEXT("Split property reference writeback is not supported: ") + Property->GetPathName());
            continue;
        }
        FString Receiver = TEXT("this");
        UEdGraphPin* ReceiverPin = Variable->FindPin(UEdGraphSchema_K2::PN_Self);
        if (ReceiverPin && (!ReceiverPin->LinkedTo.IsEmpty() || ReceiverPin->DefaultObject))
        {
            FGenerateResultStruct ReceiverValue = Subsystem->GenerateInputParameterCodeForNode(Variable, ReceiverPin, 0, MacroStack);
            Preparations.Append(ReceiverValue.Preparations);
            Receiver = ReceiverValue.Code;
        }
        const FString OwnerName = Subsystem->GetNodeStateName(Node, Parameter->GetName() + TEXT("Owner"), true);
        const FString ValueName = Subsystem->GetNodeStateName(Node, Parameter->GetName() + TEXT("Value"), true);
        Before += TEXT("auto* ") + OwnerName + TEXT(" = ") + Receiver + TEXT(";\n");
        Before += TEXT("auto ") + ValueName + TEXT(" = ") + ReadProperty(Property, OwnerName) + TEXT(";\n");
        Overrides.Add(Parameter->GetFName(), ValueName);
        After += WriteProperty(Property, OwnerName, ValueName);
    }
}

FString FRetainedBlueprintGeneration::WrapNativeCall(UFunction* Function, const FString& Invocation,
    const FString& Before, const FString& After)
{
    if (Before.IsEmpty())
    {
        return Invocation;
    }
    if (Function->GetReturnProperty())
    {
        return TEXT("([&]()\n{\n") + Before + TEXT("auto NativeResult = ") + Invocation + TEXT(";\n") + After + TEXT("return NativeResult;\n}())");
    }
    return TEXT("([&]()\n{\n") + Before + Invocation + TEXT(";\n") + After + TEXT("}())");
}
