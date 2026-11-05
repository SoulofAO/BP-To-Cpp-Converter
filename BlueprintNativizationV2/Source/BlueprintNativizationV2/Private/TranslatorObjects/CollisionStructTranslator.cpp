/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright 2026 AO All Rights Reserved.
 */

#include "TranslatorObjects/SpecializedStructTranslators.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"
#include "StructInitialization.h"
#include "StructPrivateMemberAccess.h"
#include "UObject/UnrealType.h"
#include "RetainedBlueprintGeneration.h"

struct FExternalCollisionSourceAccess
{
    using Member = TWeakObjectPtr<UBodySetup> FBodyInstance::*;
    friend Member GetStructPrivateMember(FExternalCollisionSourceAccess);
};
template struct TStructPrivateMemberAccess<FExternalCollisionSourceAccess, &FBodyInstance::ExternalCollisionProfileBodySetup>;

static FString CollisionChannelExpression(int32 Channel)
{
    return StaticEnum<ECollisionChannel>()->GetNameStringByValue(Channel);
}

static FString CollisionResponseExpression(ECollisionResponse Response)
{
    return StaticEnum<ECollisionResponse>()->GetNameStringByValue(Response);
}

bool UCollisionStructTranslator::TranslateSerializedFallback(const FStructTranslationRequest& Request, FStructTranslationResult& Result) const
{
    if (Request.Structure == FBodyInstance::StaticStruct() && !Request.DestinationExpression.IsEmpty() &&
        Request.InitializationStage != EStructInitializationStage::NativeConstructor)
    {
        Result.Error = TEXT("ImportText cannot safely update a live physics body configuration.");
        return false;
    }
    if (Request.Structure == FCollisionResponse::StaticStruct())
    {
        return FStructInitialization::Fallback(Request, Result,
            [](void* Value) { static_cast<FCollisionResponse*>(Value)->UpdateResponseContainerFromArray(); },
            TEXT("UpdateResponseContainerFromArray()"));
    }
    FStructOnScope Storage(Request.Structure);
    const void* Source = FStructInitialization::ReadValue(Request, Storage, Result.Error);
    if (!Source)
    {
        return false;
    }
    const FBodyInstance* OriginalValue = static_cast<const FBodyInstance*>(Source);
    if (Request.PropertyValue)
    {
        OriginalValue = static_cast<const FBodyInstance*>(Request.PropertyValue);
    }
    const auto& Original = *OriginalValue;
    if ((Original.*GetStructPrivateMember(FExternalCollisionSourceAccess{})).IsValid())
    {
        Result.Error = TEXT("ImportText cannot preserve an external collision configuration source.");
        return false;
    }
    FBodyInstance Configuration;
    for (TFieldIterator<FProperty> Field(Request.Structure); Field; ++Field)
    {
        if (!Field->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
        {
            Field->CopyCompleteValue(Field->ContainerPtrToValuePtr<void>(&Configuration), Field->ContainerPtrToValuePtr<void>(Source));
        }
    }
    Configuration.LoadProfileData(false);
    FStructTranslationRequest ConfigurationRequest = Request;
    ConfigurationRequest.PropertyValue = &Configuration;
    return FStructInitialization::Fallback(ConfigurationRequest, Result,
        [](void* Value) { static_cast<FBodyInstance*>(Value)->LoadProfileData(false); }, TEXT("LoadProfileData(false)"));
}

bool UCollisionStructTranslator::CanTranslate(const UScriptStruct* Structure) const
{
    return Structure == FBodyInstance::StaticStruct() || Structure == FCollisionResponse::StaticStruct();
}

bool UCollisionStructTranslator::CanInitializeValueBeforeEngineStartup(const FStructTranslationRequest& Request) const
{
    if (!Request.PropertyValue || Request.Operation != EStructTranslationOperation::PropertyValue || !Request.DestinationExpression.IsEmpty())
    {
        return false;
    }
    if (Request.Structure == FCollisionResponse::StaticStruct())
    {
        return true;
    }
    if (Request.Structure != FBodyInstance::StaticStruct())
    {
        return false;
    }
    const FBodyInstance& Original = *static_cast<const FBodyInstance*>(Request.PropertyValue);
    if ((Original.*GetStructPrivateMember(FExternalCollisionSourceAccess{})).IsValid() || Original.GetPhysMaterialOverride())
    {
        return false;
    }
    FBodyInstance Baseline;
    for (TFieldIterator<FProperty> PropertyIterator(Request.Structure); PropertyIterator; ++PropertyIterator)
    {
        if (PropertyIterator->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
        {
            continue;
        }
        for (int32 ElementIndex = 0; ElementIndex < PropertyIterator->ArrayDim; ++ElementIndex)
        {
            const void* PropertyMemory = PropertyIterator->ContainerPtrToValuePtr<void>(Request.PropertyValue, ElementIndex);
            if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(*PropertyIterator))
            {
                if (ObjectProperty->GetObjectPropertyValue(PropertyMemory))
                {
                    return false;
                }
            }
            if (!PropertyIterator->Identical(PropertyMemory, PropertyIterator->ContainerPtrToValuePtr<void>(&Baseline, ElementIndex)) &&
                !FRetainedBlueprintGeneration::CanInitializeValueBeforeEngineStartup(*PropertyIterator, PropertyMemory))
            {
                return false;
            }
        }
    }
    return true;
}

bool UCollisionStructTranslator::Translate(const FStructTranslationRequest& Request, FStructTranslationResult& Result) const
{
    const bool bBody = Request.Structure == FBodyInstance::StaticStruct();
    FString NativeType = TEXT("FCollisionResponse");
    if (bBody)
    {
        NativeType = TEXT("FBodyInstance");
    }
    if (FStructInitialization::Describe(Request, Result, NativeType, TEXT("PhysicsEngine/BodyInstance.h"), TEXT("Engine")))
    {
        return true;
    }
    if (!FStructInitialization::IsValueOperation(Request.Operation))
    {
        Result.Error = TEXT("Unsupported collision structure operation.");
        return false;
    }
    FStructOnScope Storage(Request.Structure);
    const void* Source = FStructInitialization::ReadValue(Request, Storage, Result.Error);
    if (!Source)
    {
        return false;
    }
    const FString Name = FStructInitialization::VariableName(Request, Request.Structure->GetName() + TEXT("Settings"));
    Result.Preparations.Add(NativeType + TEXT(" ") + Name + TEXT(";"));
    if (!bBody)
    {
        FCollisionResponse Desired = *static_cast<const FCollisionResponse*>(Source);
        if (!Request.PropertyValue)
        {
            Desired.UpdateResponseContainerFromArray();
        }
        FCollisionResponse Baseline;
        for (int32 Channel = 0; Channel < UE_ARRAY_COUNT(FCollisionResponseContainer().EnumArray); ++Channel)
        {
            const ECollisionChannel CollisionChannel = static_cast<ECollisionChannel>(Channel);
            if (Desired.GetResponse(CollisionChannel) != Baseline.GetResponse(CollisionChannel))
            {
                Result.Preparations.Add(Name + TEXT(".SetResponse(") + CollisionChannelExpression(Channel) + TEXT(", ") +
                    CollisionResponseExpression(Desired.GetResponse(CollisionChannel)) + TEXT(");"));
                Baseline.SetResponse(CollisionChannel, Desired.GetResponse(CollisionChannel));
            }
        }
        if (Baseline != Desired)
        {
            Result.Error = TEXT("Collision responses cannot be represented consistently by channel setters.");
            return false;
        }
        Result.Expression = Name;
        return true;
    }
    if (!Request.DestinationExpression.IsEmpty() && Request.InitializationStage != EStructInitializationStage::NativeConstructor)
    {
        Result.Error = TEXT("Body configuration requires a constructor context before physics initialization.");
        return false;
    }
    const FBodyInstance* OriginalValue = static_cast<const FBodyInstance*>(Source);
    if (Request.PropertyValue)
    {
        OriginalValue = static_cast<const FBodyInstance*>(Request.PropertyValue);
    }
    const auto& Original = *OriginalValue;
    FBodyInstance Desired;
    for (TFieldIterator<FProperty> Field(Request.Structure); Field; ++Field)
    {
        if (!Field->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
        {
            Field->CopyCompleteValue(Field->ContainerPtrToValuePtr<void>(&Desired), Field->ContainerPtrToValuePtr<void>(Source));
        }
    }
    if (!Request.PropertyValue)
    {
        auto* Responses = FindFProperty<FStructProperty>(Request.Structure, TEXT("CollisionResponses"));
        static_cast<FCollisionResponse*>(Responses->ContainerPtrToValuePtr<void>(&Desired))->UpdateResponseContainerFromArray();
    }
    UBodySetup* ExternalSource = (Original.*GetStructPrivateMember(FExternalCollisionSourceAccess{})).Get();
    FBodyInstance Baseline;
    TArray<FString> CollisionInstructions;
    auto AddCollisionInstruction = [&](const FString& Instruction)
    {
        Result.Preparations.Add(Name + TEXT(".") + Instruction + TEXT(";"));
        CollisionInstructions.Add(Request.DestinationExpression + TEXT(".") + Instruction + TEXT(";"));
    };
    const FName Profile = Desired.GetCollisionProfileName();
    if (ExternalSource)
    {
        Result.HeaderIncludes.Add(TEXT("PhysicsEngine/BodySetup.h"));
        const FString SourceExpression = TEXT("LoadObject<UBodySetup>(nullptr, ") + FStructInitialization::Quote(ExternalSource->GetPathName()) + TEXT(")");
        AddCollisionInstruction(TEXT("UseExternalCollisionProfile(") + SourceExpression + TEXT(")"));
        Baseline.UseExternalCollisionProfile(ExternalSource);
        Desired.UseExternalCollisionProfile(ExternalSource);
    }
    else if (Desired.DoesUseCollisionProfile())
    {
        Baseline.SetCollisionProfileName(Profile);
        Desired.SetCollisionProfileNameDeferred(Profile);
        Desired.ApplyDeferredCollisionProfileName();
        AddCollisionInstruction(TEXT("SetCollisionProfileNameDeferred(FName(") + FStructInitialization::Quote(Profile.ToString()) + TEXT("))"));
    }
    else
    {
        AddCollisionInstruction(TEXT("SetCollisionProfileName(NAME_None)"));
        AddCollisionInstruction(TEXT("SetObjectType(") + CollisionChannelExpression(Desired.GetObjectType()) + TEXT(")"));
        Baseline.SetObjectType(Desired.GetObjectType());
        AddCollisionInstruction(TEXT("SetCollisionEnabled(ECollisionEnabled::") + StaticEnum<ECollisionEnabled::Type>()->GetNameStringByValue(Desired.GetCollisionEnabled(false)) + TEXT(", false)"));
        Baseline.SetCollisionEnabled(Desired.GetCollisionEnabled(false), false);
        int32 ResponseCounts[3] = {};
        for (const uint8 Response : Desired.GetResponseToChannels().EnumArray)
        {
            if (Response > ECR_Block)
            {
                Result.Error = TEXT("Invalid collision channel response.");
                return false;
            }
            ++ResponseCounts[Response];
        }
        ECollisionResponse CommonResponse = ECR_Ignore;
        for (int32 ResponseIndex = 1; ResponseIndex < 3; ++ResponseIndex)
        {
            if (ResponseCounts[ResponseIndex] > ResponseCounts[CommonResponse])
            {
                CommonResponse = static_cast<ECollisionResponse>(ResponseIndex);
            }
        }
        AddCollisionInstruction(TEXT("SetResponseToAllChannels(") + CollisionResponseExpression(CommonResponse) + TEXT(")"));
        Baseline.SetResponseToAllChannels(CommonResponse);
        for (int32 Channel = 0; Channel < UE_ARRAY_COUNT(FCollisionResponseContainer().EnumArray); ++Channel)
        {
            const ECollisionChannel CollisionChannel = static_cast<ECollisionChannel>(Channel);
            if (Desired.GetResponseToChannel(CollisionChannel) != Baseline.GetResponseToChannel(CollisionChannel))
            {
                AddCollisionInstruction(TEXT("SetResponseToChannel(") + CollisionChannelExpression(Channel) + TEXT(", ") +
                    CollisionResponseExpression(Desired.GetResponseToChannel(CollisionChannel)) + TEXT(")"));
                Baseline.SetResponseToChannel(CollisionChannel, Desired.GetResponseToChannel(CollisionChannel));
            }
        }
    }
    auto AddGroupedSetting = [&](const FString& Instruction, bool bChanged, bool bDestinationChanged)
    {
        if (bChanged)
        {
            Result.Preparations.Add(Name + TEXT(".") + Instruction + TEXT(";"));
        }
        if (!Request.DestinationExpression.IsEmpty() && bDestinationChanged)
        {
            Result.InitializationInstructions.Add(Request.DestinationExpression + TEXT(".") + Instruction + TEXT(";"));
        }
    };
    const auto* DestinationBase = static_cast<const FBodyInstance*>(Request.BaseValue);
    const FString Mass = FStructInitialization::Number(Desired.GetMassOverride());
    FString OverrideMass = TEXT("false");
    if (Desired.bOverrideMass)
    {
        OverrideMass = TEXT("true");
    }
    AddGroupedSetting(TEXT("SetMassOverride(") + Mass + TEXT(", ") + OverrideMass + TEXT(")"),
        Baseline.GetMassOverride() != Desired.GetMassOverride() || Baseline.bOverrideMass != Desired.bOverrideMass,
        !DestinationBase || DestinationBase->GetMassOverride() != Desired.GetMassOverride() || DestinationBase->bOverrideMass != Desired.bOverrideMass);
    Baseline.SetMassOverride(Desired.GetMassOverride(), Desired.bOverrideMass);
    AddGroupedSetting(TEXT("SetMaxDepenetrationVelocity(") + FStructInitialization::Number(Desired.GetMaxDepenetrationVelocity()) + TEXT(")"),
        Baseline.GetMaxDepenetrationVelocity() != Desired.GetMaxDepenetrationVelocity(),
        !DestinationBase || DestinationBase->GetMaxDepenetrationVelocity() != Desired.GetMaxDepenetrationVelocity());
    Baseline.SetMaxDepenetrationVelocity(Desired.GetMaxDepenetrationVelocity());
    FString OverrideDepenetration = TEXT("false");
    if (Desired.GetOverrideMaxDepenetrationVelocity())
    {
        OverrideDepenetration = TEXT("true");
    }
    AddGroupedSetting(TEXT("SetOverrideMaxDepenetrationVelocity(") + OverrideDepenetration + TEXT(")"),
        Baseline.GetOverrideMaxDepenetrationVelocity() != Desired.GetOverrideMaxDepenetrationVelocity(),
        !DestinationBase || DestinationBase->GetOverrideMaxDepenetrationVelocity() != Desired.GetOverrideMaxDepenetrationVelocity());
    Baseline.SetOverrideMaxDepenetrationVelocity(Desired.GetOverrideMaxDepenetrationVelocity());
    for (TFieldIterator<FProperty> Field(Request.Structure); Field; ++Field)
    {
        if (Field->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated) || Field->GetFName() == TEXT("CollisionProfileName") ||
            Field->GetFName() == TEXT("CollisionEnabled") || Field->GetFName() == TEXT("ObjectType") ||
            Field->GetFName() == TEXT("CollisionResponses") || Field->GetFName() == TEXT("MassInKgOverride") ||
            Field->GetFName() == TEXT("bOverrideMass") || Field->GetFName() == TEXT("MaxDepenetrationVelocity") ||
            Field->GetFName() == TEXT("bOverrideMaxDepenetrationVelocity"))
        {
            continue;
        }
        const void* TargetValue = Field->ContainerPtrToValuePtr<void>(&Desired);
        const void* DefaultValue = Field->ContainerPtrToValuePtr<void>(&Baseline);
        const bool bChanged = !Field->Identical(TargetValue, DefaultValue, PPF_None);
        const bool bDestinationChanged = !Request.BaseValue ||
            !Field->Identical(TargetValue, Field->ContainerPtrToValuePtr<void>(Request.BaseValue), PPF_None);
        if (!bChanged && (Request.DestinationExpression.IsEmpty() || !bDestinationChanged))
        {
            continue;
        }
        FString Instruction;
        const FString ValueExpression = FStructInitialization::PropertyExpression(*Field, TargetValue, Request);
        if (Field->GetFName() == TEXT("bUseMACD"))
        {
            Instruction = TEXT("SetUseMACD(") + ValueExpression + TEXT(")");
        }
        else if (Field->GetFName() == TEXT("bAllowPartialIslandSleep"))
        {
            Instruction = TEXT("SetAllowPartialIslandSleep(") + ValueExpression + TEXT(")");
        }
        else if (Field->HasAnyPropertyFlags(CPF_NativeAccessSpecifierPrivate | CPF_NativeAccessSpecifierProtected))
        {
            Result.Error = TEXT("No complete public initialization for body setting: ") + Field->GetName();
            return false;
        }
        else
        {
            Instruction = Field->GetNameCPP() + TEXT(" = ") + ValueExpression;
        }
        if (bChanged)
        {
            Result.Preparations.Add(Name + TEXT(".") + Instruction + TEXT(";"));
        }
        if (!Request.DestinationExpression.IsEmpty() && bDestinationChanged)
        {
            Result.InitializationInstructions.Add(Request.DestinationExpression + TEXT(".") + Instruction + TEXT(";"));
        }
    }
    Result.InitializationInstructions.Append(CollisionInstructions);
    Result.Expression = Name;
    return true;
}
