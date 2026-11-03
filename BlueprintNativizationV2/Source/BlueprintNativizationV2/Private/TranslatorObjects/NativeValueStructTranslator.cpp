/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/NativeValueStructTranslator.h"
#include "Misc/FeedbackContext.h"
#include "InputCoreTypes.h"
#include "GameplayTagContainer.h"
#include "UObject/UnrealType.h"
#include "StructInitialization.h"
#include "RetainedBlueprintGeneration.h"
#include "Engine/TimerHandle.h"
#include "AI/Navigation/NavigationTypes.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "Math/RandomStream.h"
#include "UObject/SoftObjectPath.h"

bool UNativeValueStructTranslator::CanTranslate(const UScriptStruct *Structure) const
{
    return Structure == TBaseStructure<FKey>::Get() || Structure == TBaseStructure<FTransform>::Get() ||
        Structure == FGameplayTag::StaticStruct() || Structure == FGameplayTagContainer::StaticStruct() ||
        Structure == FTimerHandle::StaticStruct() || Structure == FNavMovementProperties::StaticStruct() ||
        Structure == FBlackboardKeySelector::StaticStruct() || Structure == TBaseStructure<FRandomStream>::Get() ||
        Structure == FSoftObjectPath::StaticStruct() || Structure == FSoftClassPath::StaticStruct();
}

bool UNativeValueStructTranslator::CanInitializeValueBeforeEngineStartup(const FStructTranslationRequest& Request) const
{
    if (!Request.PropertyValue || Request.Operation != EStructTranslationOperation::PropertyValue ||
        !Request.DestinationExpression.IsEmpty())
    {
        return false;
    }
    if (Request.Structure == TBaseStructure<FTransform>::Get() || Request.Structure == TBaseStructure<FKey>::Get())
    {
        return true;
    }
    if (Request.Structure == FSoftObjectPath::StaticStruct() || Request.Structure == FSoftClassPath::StaticStruct())
    {
        return true;
    }
    if (Request.Structure == FGameplayTag::StaticStruct())
    {
        return true;
    }
    if (Request.Structure == TBaseStructure<FRandomStream>::Get())
    {
        const FRandomStream& Value = *static_cast<const FRandomStream*>(Request.PropertyValue);
        return Value.GetInitialSeed() == Value.GetCurrentSeed();
    }
    if (Request.Structure == FGameplayTagContainer::StaticStruct())
    {
        return true;
    }
    if (Request.Structure == FTimerHandle::StaticStruct())
    {
        return !static_cast<const FTimerHandle*>(Request.PropertyValue)->IsValid();
    }
    if (Request.Structure == FNavMovementProperties::StaticStruct())
    {
        return true;
    }
    if (Request.Structure == FBlackboardKeySelector::StaticStruct())
    {
        for (TFieldIterator<FProperty> Member(Request.Structure); Member; ++Member)
        {
            if (!FRetainedBlueprintGeneration::CanInitializeValueBeforeEngineStartup(*Member,
                Member->ContainerPtrToValuePtr<void>(Request.PropertyValue)))
            {
                return false;
            }
        }
        return true;
    }
    return false;
}

bool UNativeValueStructTranslator::Translate(const FStructTranslationRequest &Request,
                                             FStructTranslationResult &Result) const
{
    if (Request.Structure == FSoftObjectPath::StaticStruct() || Request.Structure == FSoftClassPath::StaticStruct())
    {
        FString NativeType = TEXT("FSoftObjectPath");
        if (Request.Structure == FSoftClassPath::StaticStruct())
        {
            NativeType = TEXT("FSoftClassPath");
        }
        if (FStructInitialization::Describe(Request, Result, NativeType, TEXT("UObject/SoftObjectPath.h"), TEXT("CoreUObject")))
        {
            return true;
        }
        if (!FStructInitialization::IsValueOperation(Request.Operation))
        {
            return false;
        }
        FStructOnScope Storage(Request.Structure);
        const void* Source = FStructInitialization::ReadValue(Request, Storage, Result.Error);
        if (!Source)
        {
            return false;
        }
        FString Path;
        if (Request.Structure == FSoftClassPath::StaticStruct())
        {
            Path = static_cast<const FSoftClassPath*>(Source)->ToString();
        }
        else
        {
            Path = static_cast<const FSoftObjectPath*>(Source)->ToString();
        }
        Result.Expression = NativeType + TEXT("(") + FStructInitialization::Quote(Path) + TEXT(")");
        return true;
    }
    if (Request.Structure == TBaseStructure<FRandomStream>::Get())
    {
        if (FStructInitialization::Describe(Request, Result, TEXT("FRandomStream"), TEXT("Math/RandomStream.h"), TEXT("Core")))
        {
            return true;
        }
        if (!FStructInitialization::IsValueOperation(Request.Operation))
        {
            return false;
        }
        FStructOnScope Storage(Request.Structure);
        const void* Source = FStructInitialization::ReadValue(Request, Storage, Result.Error);
        if (!Source)
        {
            return false;
        }
        const FRandomStream& Value = *static_cast<const FRandomStream*>(Source);
        if (Value.GetInitialSeed() != Value.GetCurrentSeed())
        {
            return FStructInitialization::Fallback(Request, Result);
        }
        Result.Expression = TEXT("FRandomStream(") + FString::FromInt(Value.GetInitialSeed()) + TEXT(")");
        return true;
    }
    if (Request.Structure == FTimerHandle::StaticStruct() || Request.Structure == FNavMovementProperties::StaticStruct() ||
        Request.Structure == FBlackboardKeySelector::StaticStruct())
    {
        Result.ModuleDependencies.Add(TEXT("Engine"));
        if (Request.Structure == FTimerHandle::StaticStruct())
        {
            Result.NativeType = TEXT("FTimerHandle");
            Result.HeaderIncludes.Add(TEXT("Engine/TimerHandle.h"));
        }
        else if (Request.Structure == FNavMovementProperties::StaticStruct())
        {
            Result.NativeType = TEXT("FNavMovementProperties");
            Result.HeaderIncludes.Add(TEXT("AI/Navigation/NavigationTypes.h"));
        }
        else
        {
            Result.NativeType = TEXT("FBlackboardKeySelector");
            Result.HeaderIncludes.Add(TEXT("BehaviorTree/BehaviorTreeTypes.h"));
            Result.ModuleDependencies.Add(TEXT("AIModule"));
        }
        if (Request.Operation == EStructTranslationOperation::DescribeType)
        {
            Result.Expression = Result.NativeType;
            return true;
        }
        if (Request.Operation == EStructTranslationOperation::Conversion && Request.DestinationType == Result.NativeType)
        {
            Result.Expression = Request.SourceExpression;
            return true;
        }
        if (!FStructInitialization::IsValueOperation(Request.Operation))
        {
            return false;
        }
        FStructOnScope Storage(Request.Structure);
        const void* Source = FStructInitialization::ReadValue(Request, Storage, Result.Error);
        if (!Source)
        {
            return false;
        }
        if (Request.Structure == FTimerHandle::StaticStruct())
        {
            if (static_cast<const FTimerHandle*>(Source)->IsValid())
            {
                Result.Error = TEXT("An initialized timer identifier cannot be recreated as a default value.");
                return false;
            }
            Result.Expression = TEXT("FTimerHandle{}");
            return true;
        }
        if (Request.Structure == FBlackboardKeySelector::StaticStruct())
        {
            FArrayProperty* AllowedTypes = FindFProperty<FArrayProperty>(Request.Structure, TEXT("AllowedTypes"));
            FNameProperty* SelectedKeyName = FindFProperty<FNameProperty>(Request.Structure, TEXT("SelectedKeyName"));
            FClassProperty* SelectedKeyType = FindFProperty<FClassProperty>(Request.Structure, TEXT("SelectedKeyType"));
            FIntProperty* SelectedKeyIdentifier = FindFProperty<FIntProperty>(Request.Structure, TEXT("SelectedKeyID"));
            FBoolProperty* NoneIsAllowed = FindFProperty<FBoolProperty>(Request.Structure, TEXT("bNoneIsAllowedValue"));
            if (!AllowedTypes || !CastField<FObjectPropertyBase>(AllowedTypes->Inner) || !SelectedKeyName || !SelectedKeyType ||
                !SelectedKeyIdentifier || !NoneIsAllowed || AllowedTypes->ArrayDim != 1 || SelectedKeyName->ArrayDim != 1 ||
                SelectedKeyType->ArrayDim != 1 || SelectedKeyIdentifier->ArrayDim != 1 || NoneIsAllowed->ArrayDim != 1)
            {
                Result.Error = TEXT("Blackboard key selector initialization requires the expected five reflected properties and an int32 SelectedKeyID.");
                return false;
            }
            Result.HeaderIncludes.Add(TEXT("UObject/UnrealType.h"));
            const FString Name = FStructInitialization::VariableName(Request, TEXT("BlackboardSelectorDefaults"));
            const FString IdentifierPropertyName = FStructInitialization::VariableName(Request, TEXT("BlackboardSelectorIdentifierProperty"));
            Result.Preparations.Add(TEXT("FBlackboardKeySelector ") + Name + TEXT("{};"));
            FScriptArrayHelper AllowedValues(AllowedTypes, AllowedTypes->ContainerPtrToValuePtr<void>(Source));
            FObjectPropertyBase* AllowedTypeProperty = CastFieldChecked<FObjectPropertyBase>(AllowedTypes->Inner);
            TArray<FString> AllowedExpressions;
            for (int32 Index = 0; Index < AllowedValues.Num(); ++Index)
            {
                UObject* AllowedValue = AllowedTypeProperty->GetObjectPropertyValue(AllowedValues.GetRawPtr(Index));
                if (AllowedValue && !AllowedValue->IsAsset())
                {
                    AllowedExpressions.Add(FRetainedBlueprintGeneration::ConstructInstancedObject(AllowedTypeProperty,
                        AllowedValue, Request.OwnerExpression));
                }
                else
                {
                    AllowedExpressions.Add(FStructInitialization::PropertyExpression(AllowedTypeProperty, AllowedValues.GetRawPtr(Index), Request));
                }
            }
            Result.Preparations.Add(Name + TEXT(".AllowedTypes = {") + FString::Join(AllowedExpressions, TEXT(", ")) + TEXT("};"));
            for (FProperty* Property : {static_cast<FProperty*>(SelectedKeyName), static_cast<FProperty*>(SelectedKeyType)})
            {
                Result.Preparations.Add(Name + TEXT(".") + Property->GetName() + TEXT(" = ") +
                    FStructInitialization::PropertyExpression(Property, Property->ContainerPtrToValuePtr<void>(Source), Request) + TEXT(";"));
            }
            const FBlackboardKeySelector DefaultSelector{};
            if (!SelectedKeyIdentifier->Identical_InContainer(Source, &DefaultSelector))
            {
                Result.Preparations.Add(TEXT("const FIntProperty* ") + IdentifierPropertyName +
                    TEXT(" = FindFProperty<FIntProperty>(FBlackboardKeySelector::StaticStruct(), TEXT(\"SelectedKeyID\"));"));
                Result.Preparations.Add(TEXT("check(") + IdentifierPropertyName + TEXT(" && ") + IdentifierPropertyName + TEXT("->ArrayDim == 1);"));
                Result.Preparations.Add(IdentifierPropertyName + TEXT("->SetPropertyValue_InContainer(&") + Name + TEXT(", ") +
                    FString::FromInt(SelectedKeyIdentifier->GetPropertyValue_InContainer(Source)) + TEXT(");"));
            }
            Result.Preparations.Add(Name + TEXT(".AllowNoneAsValue(") +
                FStructInitialization::PropertyExpression(NoneIsAllowed, NoneIsAllowed->ContainerPtrToValuePtr<void>(Source), Request) + TEXT(");"));
            Result.Expression = Name;
            return true;
        }
        const FNavMovementProperties& Value = *static_cast<const FNavMovementProperties*>(Source);
        const FString Name = FStructInitialization::VariableName(Request, TEXT("NavigationMovementDefaults"));
        Result.Preparations.Add(TEXT("FNavMovementProperties ") + Name + TEXT("{};"));
        Result.Preparations.Add(Name + TEXT(".FixedPathBrakingDistance = ") + FStructInitialization::Number(Value.FixedPathBrakingDistance) + TEXT(";"));
        for (const FName PropertyName : {FName(TEXT("bUpdateNavAgentWithOwnersCollision")), FName(TEXT("bUseAccelerationForPaths")),
            FName(TEXT("bUseFixedBrakingDistanceForPaths")), FName(TEXT("bStopMovementAbortPaths"))})
        {
            const FBoolProperty* Property = CastFieldChecked<FBoolProperty>(Request.Structure->FindPropertyByName(PropertyName));
            FString BooleanValue = TEXT("false");
            if (Property->GetPropertyValue_InContainer(&Value))
            {
                BooleanValue = TEXT("true");
            }
            Result.Preparations.Add(Name + TEXT(".") + PropertyName.ToString() + TEXT(" = ") + BooleanValue + TEXT(";"));
        }
        Result.Expression = Name;
        return true;
    }
    if (Request.Structure == FGameplayTag::StaticStruct() || Request.Structure == FGameplayTagContainer::StaticStruct())
    {
        const bool bContainer = Request.Structure == FGameplayTagContainer::StaticStruct();
        Result.NativeType = TEXT("FGameplayTag");
        if (bContainer)
        {
            Result.NativeType = TEXT("FGameplayTagContainer");
        }
        Result.HeaderIncludes = {TEXT("GameplayTagContainer.h")};
        Result.ModuleDependencies = {TEXT("GameplayTags")};
        if (Request.Operation == EStructTranslationOperation::DescribeType)
        {
            Result.Expression = Result.NativeType;
            return true;
        }
        if (Request.Operation == EStructTranslationOperation::Conversion && Request.DestinationType == Result.NativeType)
        {
            Result.Expression = Request.SourceExpression;
            return true;
        }
        if (Request.Operation == EStructTranslationOperation::ReadMember)
        {
            if (!bContainer && Request.MemberName == TEXT("TagName"))
            {
                Result.Expression = Request.SourceExpression + TEXT(".GetTagName()");
                return true;
            }
            if (bContainer && Request.MemberName == TEXT("GameplayTags"))
            {
                Result.Expression = Request.SourceExpression + TEXT(".GetGameplayTagArray()");
                return true;
            }
        }
        if (Request.Operation == EStructTranslationOperation::Construction || Request.Operation == EStructTranslationOperation::WriteMember)
        {
            FName MemberName = TEXT("TagName");
            FString Factory = TEXT("FGameplayTag::RequestGameplayTag");
            if (bContainer)
            {
                MemberName = TEXT("GameplayTags");
                Factory = TEXT("FGameplayTagContainer::CreateFromArray");
            }
            if (Request.Arguments.Num() == 1 &&
                (Request.Operation == EStructTranslationOperation::Construction || Request.MemberName == MemberName))
            {
                Result.Expression = Factory + TEXT("(") + Request.Arguments[0].Expression + TEXT(")");
                if (Request.Operation == EStructTranslationOperation::WriteMember)
                {
                    Result.Expression = Request.SourceExpression + TEXT(" = ") + Result.Expression;
                }
                return true;
            }
        }
        if (Request.Operation == EStructTranslationOperation::DefaultValue ||
            Request.Operation == EStructTranslationOperation::PropertyValue || Request.Operation == EStructTranslationOperation::SerializedValue)
        {
            FGameplayTag Tag;
            FGameplayTagContainer Container;
            void* ValueAddress = &Tag;
            if (bContainer)
            {
                ValueAddress = &Container;
            }
            if (Request.PropertyValue)
            {
                Request.Structure->CopyScriptStruct(ValueAddress, Request.PropertyValue);
            }
            else if (!Request.SerializedValue.IsEmpty() &&
                !Request.Structure->ImportText(*Request.SerializedValue, ValueAddress, nullptr, PPF_None, GWarn, TEXT("GameplayTag")))
            {
                Result.Error = TEXT("Invalid serialized gameplay tag value.");
                return false;
            }
            const FNameProperty* TagNameProperty = FindFProperty<FNameProperty>(FGameplayTag::StaticStruct(), TEXT("TagName"));
            if (!TagNameProperty || TagNameProperty->ArrayDim != 1)
            {
                Result.Error = TEXT("Gameplay tag literal initialization requires the reflected FName TagName property.");
                return false;
            }
            Result.HeaderIncludes.Add(TEXT("UObject/UnrealType.h"));
            auto TagExpression = [&Request](const FGameplayTag& Value)
            {
                if (!Value.IsValid())
                {
                    return FString(TEXT("FGameplayTag()"));
                }
                const FString TagValueName = FStructInitialization::VariableName(Request, TEXT("GameplayTagLiteralValue"));
                const FString TagNamePropertyName = FStructInitialization::VariableName(Request, TEXT("GameplayTagLiteralNameProperty"));
                return TEXT("([]() { FGameplayTag ") + TagValueName + TEXT("; const FNameProperty* ") + TagNamePropertyName +
                    TEXT(" = FindFProperty<FNameProperty>(FGameplayTag::StaticStruct(), TEXT(\"TagName\")); check(") + TagNamePropertyName +
                    TEXT("); ") + TagNamePropertyName + TEXT("->SetPropertyValue_InContainer(&") + TagValueName + TEXT(", FName(TEXT(\"") +
                    Value.GetTagName().ToString().ReplaceCharWithEscapedChar() + TEXT("\"))); return ") + TagValueName + TEXT("; }())");
            };
            Result.Expression = TagExpression(Tag);
            if (bContainer)
            {
                const FArrayProperty* GameplayTagsProperty = FindFProperty<FArrayProperty>(Request.Structure, TEXT("GameplayTags"));
                const FArrayProperty* ParentTagsProperty = FindFProperty<FArrayProperty>(Request.Structure, TEXT("ParentTags"));
                const FStructProperty* GameplayTagsInner = nullptr;
                const FStructProperty* ParentTagsInner = nullptr;
                if (GameplayTagsProperty && ParentTagsProperty)
                {
                    GameplayTagsInner = CastField<FStructProperty>(GameplayTagsProperty->Inner);
                    ParentTagsInner = CastField<FStructProperty>(ParentTagsProperty->Inner);
                }
                if (!GameplayTagsInner || !ParentTagsInner || GameplayTagsInner->Struct != FGameplayTag::StaticStruct() ||
                    ParentTagsInner->Struct != FGameplayTag::StaticStruct() || GameplayTagsProperty->ArrayDim != 1 || ParentTagsProperty->ArrayDim != 1)
                {
                    Result.Error = TEXT("Gameplay tag container literal initialization requires reflected GameplayTags and ParentTags arrays of FGameplayTag.");
                    return false;
                }
                TArray<FGameplayTag> ParentTags;
                ParentTagsProperty->CopyCompleteValue(&ParentTags, ParentTagsProperty->ContainerPtrToValuePtr<void>(&Container));
                if (Container.IsEmpty() && ParentTags.IsEmpty())
                {
                    Result.Expression = TEXT("FGameplayTagContainer{}");
                    return true;
                }
                TArray<FString> GameplayTagExpressions;
                TArray<FString> ParentTagExpressions;
                for (const FGameplayTag& Value : Container)
                {
                    GameplayTagExpressions.Add(TagExpression(Value));
                }
                for (const FGameplayTag& Value : ParentTags)
                {
                    ParentTagExpressions.Add(TagExpression(Value));
                }
                const FString ContainerValueName = FStructInitialization::VariableName(Request, TEXT("GameplayTagContainerLiteralValue"));
                const FString GameplayTagsName = FStructInitialization::VariableName(Request, TEXT("GameplayTagContainerLiteralTags"));
                const FString ParentTagsName = FStructInitialization::VariableName(Request, TEXT("GameplayTagContainerLiteralParents"));
                const FString GameplayTagsPropertyName = FStructInitialization::VariableName(Request, TEXT("GameplayTagContainerLiteralTagsProperty"));
                const FString ParentTagsPropertyName = FStructInitialization::VariableName(Request, TEXT("GameplayTagContainerLiteralParentsProperty"));
                Result.Preparations.Add(TEXT("FGameplayTagContainer ") + ContainerValueName + TEXT(";\n{\nconst TArray<FGameplayTag> ") + GameplayTagsName + TEXT("{") +
                    FString::Join(GameplayTagExpressions, TEXT(", ")) + TEXT("}; const TArray<FGameplayTag> ") + ParentTagsName + TEXT("{") +
                    FString::Join(ParentTagExpressions, TEXT(", ")) + TEXT("}; const FArrayProperty* ") + GameplayTagsPropertyName +
                    TEXT(" = FindFProperty<FArrayProperty>(FGameplayTagContainer::StaticStruct(), TEXT(\"GameplayTags\")); const FArrayProperty* ") + ParentTagsPropertyName +
                    TEXT(" = FindFProperty<FArrayProperty>(FGameplayTagContainer::StaticStruct(), TEXT(\"ParentTags\")); check(") + GameplayTagsPropertyName + TEXT(" && ") + ParentTagsPropertyName + TEXT("); ") +
                    GameplayTagsPropertyName + TEXT("->CopyCompleteValue(") + GameplayTagsPropertyName + TEXT("->ContainerPtrToValuePtr<void>(&") + ContainerValueName + TEXT("), &") + GameplayTagsName + TEXT("); ") +
                    ParentTagsPropertyName + TEXT("->CopyCompleteValue(") + ParentTagsPropertyName + TEXT("->ContainerPtrToValuePtr<void>(&") + ContainerValueName + TEXT("), &") + ParentTagsName + TEXT(");\n}\n"));
                Result.Expression = ContainerValueName;
            }
            return true;
        }
        Result.Error = TEXT("Unsupported gameplay tag translation operation.");
        return false;
    }
    const bool bKey = Request.Structure == TBaseStructure<FKey>::Get();
    Result.NativeType = TEXT("FTransform");
    Result.HeaderIncludes = {TEXT("Math/Transform.h")};
    Result.ModuleDependencies = {TEXT("Core")};
    if (bKey)
    {
        Result.NativeType = TEXT("FKey");
        Result.HeaderIncludes = {TEXT("InputCoreTypes.h")};
        Result.ModuleDependencies = {TEXT("InputCore")};
    }
    if (Request.Operation == EStructTranslationOperation::DescribeType)
    {
        Result.Expression = Result.NativeType;
        return true;
    }
    if (Request.Operation == EStructTranslationOperation::Conversion)
    {
        if (Request.DestinationType == Result.NativeType)
        {
            Result.Expression = Request.SourceExpression;
            return true;
        }
    }
    else if (Request.Operation == EStructTranslationOperation::ReadMember ||
             Request.Operation == EStructTranslationOperation::WriteMember)
    {
        FString Member = Request.MemberName.ToString();
        if (!bKey && Member == TEXT("Location"))
        {
            Member = TEXT("Translation");
        }
        if (!bKey && Member == TEXT("Scale"))
        {
            Member = TEXT("Scale3D");
        }
        FString Getter;
        FString Setter;
        if (bKey && Member == TEXT("KeyName"))
        {
            Getter = TEXT("GetFName");
        }
        else if (!bKey && (Member == TEXT("Rotation") || Member == TEXT("Translation") || Member == TEXT("Scale3D")))
        {
            Getter = TEXT("Get") + Member;
            Setter = TEXT("Set") + Member;
        }
        if (!Getter.IsEmpty() && Request.Operation == EStructTranslationOperation::ReadMember)
        {
            Result.Expression = Request.SourceExpression + TEXT(".") + Getter + TEXT("()");
            if (!bKey && Member == TEXT("Rotation") && Request.DestinationType == TEXT("FRotator"))
            {
                Result.Expression += TEXT(".Rotator()");
            }
            return true;
        }
        if (Request.Arguments.Num() == 1)
        {
            if (bKey && Member == TEXT("KeyName"))
            {
                Result.Expression =
                    Request.SourceExpression + TEXT(" = FKey(") + Request.Arguments[0].Expression + TEXT(")");
                return true;
            }
            if (!Setter.IsEmpty())
            {
                FString Value = Request.Arguments[0].Expression;
                if (!bKey && Member == TEXT("Rotation") && Request.Arguments[0].NativeType == TEXT("FRotator"))
                {
                    Value = TEXT("(") + Value + TEXT(").Quaternion()");
                }
                Result.Expression = Request.SourceExpression + TEXT(".") + Setter + TEXT("(") +
                                    Value + TEXT(")");
                return true;
            }
        }
    }
    else if (Request.Operation == EStructTranslationOperation::Construction)
    {
        if (bKey && Request.Arguments.Num() == 1)
        {
            Result.Expression = TEXT("FKey(") + Request.Arguments[0].Expression + TEXT(")");
            return true;
        }
        if (!bKey)
        {
            TArray<FString> Values;
            for (const FName Member : {FName(TEXT("Rotation")), FName(TEXT("Translation")), FName(TEXT("Scale3D"))})
            {
                const auto *Argument = Request.Arguments.FindByPredicate(
                    [&](const FStructTranslationArgument &Candidate)
                    {
                        return Candidate.Name == Member ||
                            (Member == TEXT("Translation") && Candidate.Name == TEXT("Location")) ||
                            (Member == TEXT("Scale3D") && Candidate.Name == TEXT("Scale"));
                    });
                if (!Argument)
                {
                    Result.Error = TEXT("Transform construction requires Rotation, Translation and Scale3D.");
                    return false;
                }
                Values.Add(Argument->Expression);
            }
            Result.Expression = TEXT("FTransform(") + FString::Join(Values, TEXT(", ")) + TEXT(")");
            return true;
        }
    }
    else
    {
        if (bKey)
        {
            FKey Value;
            if (Request.PropertyValue)
            {
                Value = *static_cast<const FKey *>(Request.PropertyValue);
            }
            else if (!Request.SerializedValue.IsEmpty())
            {
                const TCHAR *Text = *Request.SerializedValue;
                if (!Value.ImportTextItem(Text, PPF_None, nullptr, nullptr))
                {
                    Result.Error = TEXT("Invalid serialized key.");
                    return false;
                }
            }
            Result.Expression = FString::Format(TEXT("FKey(FName(TEXT(\"{0}\")))"),
                                                {Value.GetFName().ToString().ReplaceCharWithEscapedChar()});
            return true;
        }
        FTransform Value = FTransform::Identity;
        if (Request.PropertyValue)
        {
            Value = *static_cast<const FTransform *>(Request.PropertyValue);
        }
        else if (!Request.SerializedValue.IsEmpty())
        {
            if (!Request.Structure->ImportText(*Request.SerializedValue, &Value, nullptr, PPF_None, GWarn,
                                               TEXT("Transform")))
            {
                Result.Error = TEXT("Invalid serialized transform.");
                return false;
            }
        }
        const FQuat Rotation = Value.GetRotation();
        const FVector Translation = Value.GetTranslation();
        const FVector Scale = Value.GetScale3D();
        Result.Expression = FString::Format(
            TEXT("FTransform(FQuat({0}, {1}, {2}, {3}), FVector({4}, {5}, {6}), FVector({7}, {8}, {9}))"),
            {FStructInitialization::Number(Rotation.X), FStructInitialization::Number(Rotation.Y), FStructInitialization::Number(Rotation.Z),
             FStructInitialization::Number(Rotation.W), FStructInitialization::Number(Translation.X),
             FStructInitialization::Number(Translation.Y), FStructInitialization::Number(Translation.Z),
             FStructInitialization::Number(Scale.X), FStructInitialization::Number(Scale.Y), FStructInitialization::Number(Scale.Z)});
        return true;
    }
    Result.Error = TEXT("Unsupported structure translation operation.");
    return false;
}
