/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSettings.h"
#include "StructInitialization.h"
#include "NativeApiCatalog.h"
#include "NativeGenerationContext.h"
#include "TranslatorObjects/ConstructionScriptTranslation.h"
#include "TranslatorObjects/SpecializedStructTranslators.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

static bool IsUnconditionalCallable(const TSharedPtr<FJsonObject> &Callable)
{
    if (Callable->GetBoolField(TEXT("Deleted")))
    {
        return false;
    }
    for (const auto &Value : Callable->GetArrayField(TEXT("Evidence")))
    {
        if (!Value->AsObject()->GetStringField(TEXT("Condition")).IsEmpty())
        {
            return false;
        }
    }
    return true;
}

static bool IsHeaderDefinedCallable(const TSharedPtr<FJsonObject>& Callable)
{
    if (Callable->GetBoolField(TEXT("Exported")) || Callable->GetBoolField(TEXT("InlineDefinition")))
    {
        return true;
    }
    if (!Callable->GetBoolField(TEXT("Constructor")) || !Callable->GetArrayField(TEXT("Parameters")).IsEmpty())
    {
        return false;
    }
    const FString Signature = Callable->GetStringField(TEXT("Name")) + TEXT("()");
    for (const auto& EvidenceValue : Callable->GetArrayField(TEXT("Evidence")))
    {
        const auto Evidence = EvidenceValue->AsObject();
        if (!Evidence->GetStringField(TEXT("File")).EndsWith(TEXT(".h")))
        {
            continue;
        }
        FString Text = Evidence->GetStringField(TEXT("Text"));
        Text.ReplaceInline(TEXT(" "), TEXT(""));
        Text.ReplaceInline(TEXT("\t"), TEXT(""));
        Text.ReplaceInline(TEXT("\r"), TEXT(""));
        Text.ReplaceInline(TEXT("\n"), TEXT(""));
        if (Text.EndsWith(Signature + TEXT("=default;")) && !Text.Contains(TEXT("::") + Signature))
        {
            return true;
        }
    }
    return false;
}

static bool HasEmptyConstructorBody(const TSharedPtr<FJsonObject>& Callable)
{
    FString Body = Callable->GetStringField(TEXT("Body"));
    Body.ReplaceInline(TEXT(" "), TEXT(""));
    Body.ReplaceInline(TEXT("\t"), TEXT(""));
    Body.ReplaceInline(TEXT("\r"), TEXT(""));
    Body.ReplaceInline(TEXT("\n"), TEXT(""));
    return Body == TEXT("{}");
}

bool UBlueprintNativizationSettingsLibrary::FindConstructorDescriptorByStruct(UStruct *OwnerStruct,
                                                                              FConstructorDescriptor &OutNativeDesc)
{
    if (!OwnerStruct || !OwnerStruct->IsNative())
    {
        return false;
    }
    if (UScriptStruct *Structure = Cast<UScriptStruct>(OwnerStruct))
    {
        if (FindStructTranslator(Structure))
        {
            return false;
        }
    }
    const auto Type = FNativeApiCatalog::FindType(OwnerStruct);
    if (!Type)
    {
        return false;
    }
    TArray<FConstructorDescriptor> Candidates;
    for (const auto &Value : Type->GetArrayField(TEXT("Callables")))
    {
        const auto Callable = Value->AsObject();
        if (!Callable->GetBoolField(TEXT("Constructor")) || Callable->GetIntegerField(TEXT("Access")) != 1 ||
            !IsUnconditionalCallable(Callable) || !IsHeaderDefinedCallable(Callable) || !HasEmptyConstructorBody(Callable))
        {
            continue;
        }
        const auto &Parameters = Callable->GetArrayField(TEXT("Parameters"));
        if (Parameters.IsEmpty())
        {
            continue;
        }
        FConstructorDescriptor Candidate;
        Candidate.OriginalStructName = UBlueprintNativizationLibrary::GetUniqueFieldName(OwnerStruct);
        const auto Initializers = Callable->GetObjectField(TEXT("Initializers"));
        bool bComplete = true;
        for (int32 ParameterIndex = 0; ParameterIndex < Parameters.Num(); ++ParameterIndex)
        {
            const auto Parameter = Parameters[ParameterIndex]->AsObject();
            FString MemberName;
            for (const auto &Initializer : Initializers->Values)
            {
                if (Initializer.Value->AsString() == Parameter->GetStringField(TEXT("Name")) &&
                    OwnerStruct->FindPropertyByName(FName(*Initializer.Key)))
                {
                    if (!MemberName.IsEmpty())
                    {
                        bComplete = false;
                    }
                    MemberName = Initializer.Key;
                }
            }
            if (MemberName.IsEmpty())
            {
                bComplete = false;
            }
            Candidate.ConstructorProperties.Add(
                {MemberName, ParameterIndex, Parameter->GetStringField(TEXT("Default"))});
        }
        int32 PropertyCount = 0;
        for (TFieldIterator<FProperty> Property(OwnerStruct); Property; ++Property)
        {
            ++PropertyCount;
        }
        if (bComplete && Candidate.ConstructorProperties.Num() == PropertyCount)
        {
            Candidates.Add(Candidate);
        }
    }
    if (Candidates.Num() == 1)
    {
        OutNativeDesc = Candidates[0];
        return true;
    }
    return false;
}

bool UBlueprintNativizationSettingsLibrary::FindGetterAndSetterDescriptorDescriptorByProperty(
    FProperty *Property, FGetterAndSetterPropertyDescriptor &Result)
{
    UStruct *Owner = Property->GetOwnerStruct();
    if (!Owner || !Owner->IsNative())
    {
        return false;
    }
    if (UScriptStruct *Structure = Cast<UScriptStruct>(Owner))
    {
        if (FindStructTranslator(Structure))
        {
            return false;
        }
    }
    const auto Type = FNativeApiCatalog::FindType(Owner);
    if (!Type)
    {
        return false;
    }
    Result = {};
    Result.OriginalStructName = UBlueprintNativizationLibrary::GetUniqueFieldName(Owner);
    Result.OriginalPropertyName = Property->GetNameCPP();
    TSet<FString> Getters;
    TSet<FString> Setters;
    for (const auto &Value : Type->GetArrayField(TEXT("Callables")))
    {
        const auto Callable = Value->AsObject();
        if (Callable->GetIntegerField(TEXT("Access")) != 1 || !IsUnconditionalCallable(Callable))
        {
            continue;
        }
        if (Callable->GetStringField(TEXT("ReadMember")) == Property->GetNameCPP())
        {
            bool bCompatibleReturn = true;
            if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
            {
                if (!Property->IsA<FClassProperty>() && !ObjectProperty->PropertyClass->HasAnyClassFlags(CLASS_Const))
                {
                    FString ReturnType = Callable->GetStringField(TEXT("ReturnType"));
                    ReturnType.ReplaceInline(TEXT(" "), TEXT(""));
                    bCompatibleReturn = !ReturnType.StartsWith(TEXT("const")) && !ReturnType.Contains(TEXT("const*"));
                }
            }
            if (bCompatibleReturn)
            {
                Getters.Add(Callable->GetStringField(TEXT("Name")));
            }
        }
        if (Callable->GetStringField(TEXT("WriteMember")) == Property->GetNameCPP())
        {
            Setters.Add(Callable->GetStringField(TEXT("Name")));
        }
    }
    const FString MetadataGetter = Property->GetMetaData(TEXT("BlueprintGetter"));
    const FString MetadataSetter = Property->GetMetaData(TEXT("BlueprintSetter"));
    if (!MetadataGetter.IsEmpty())
    {
        Getters = {MetadataGetter};
    }
    if (!MetadataSetter.IsEmpty())
    {
        Setters = {MetadataSetter};
    }
    FString GetterMemberName = Property->GetNameCPP();
    if (Property->IsA<FBoolProperty>())
    {
        GetterMemberName.RemoveFromStart(TEXT("b"));
    }
    const FString PreferredGetter = TEXT("Get") + GetterMemberName;
    if (MetadataGetter.IsEmpty() && Getters.Contains(PreferredGetter))
    {
        Getters = {PreferredGetter};
    }
    if (Getters.Num() > 1 && MetadataGetter.IsEmpty())
    {
        TSet<FString> NamedGetters;
        for (const FString& Getter : Getters)
        {
            if (Getter.StartsWith(TEXT("Get")))
            {
                NamedGetters.Add(Getter);
            }
        }
        if (NamedGetters.Num() == 1)
        {
            Getters = MoveTemp(NamedGetters);
        }
    }
    if (Getters.Num() == 1)
    {
        Result.GetterPropertyFunctionName = Getters.Array()[0];
        FString SetterName;
        if (Result.GetterPropertyFunctionName.StartsWith(TEXT("Get")))
        {
            SetterName = TEXT("Set") + Result.GetterPropertyFunctionName.RightChop(3);
        }
        else if (Property->IsA<FBoolProperty>() && Result.GetterPropertyFunctionName.StartsWith(TEXT("Is")))
        {
            SetterName = TEXT("Set") + Result.GetterPropertyFunctionName.RightChop(2);
        }
        if (!SetterName.IsEmpty())
        {
            int32 CandidateCount = 0;
            for (const auto &Value : Type->GetArrayField(TEXT("Callables")))
            {
                const auto Callable = Value->AsObject();
                const auto &Parameters = Callable->GetArrayField(TEXT("Parameters"));
                if (Callable->GetStringField(TEXT("Name")) != SetterName || Parameters.IsEmpty() ||
                    Callable->GetIntegerField(TEXT("Access")) != 1 || !IsUnconditionalCallable(Callable))
                {
                    continue;
                }
                FString ParameterType = Parameters[0]->AsObject()->GetStringField(TEXT("Type"));
                ParameterType.RemoveFromStart(TEXT("const "));
                ParameterType.RemoveFromEnd(TEXT("&"));
                ParameterType.TrimStartAndEndInline();
                FString PropertyType = Property->GetCPPType();
                if (Property->IsA<FBoolProperty>())
                {
                    PropertyType = TEXT("bool");
                }
                bool bCompatible = ParameterType == PropertyType;
                for (int32 ParameterIndex = 1; ParameterIndex < Parameters.Num(); ++ParameterIndex)
                {
                    bCompatible &= !Parameters[ParameterIndex]->AsObject()->GetStringField(TEXT("Default")).IsEmpty();
                }
                if (bCompatible)
                {
                    ++CandidateCount;
                }
            }
            if (CandidateCount == 1)
            {
                Setters = {SetterName};
            }
        }
    }
    if (Setters.IsEmpty())
    {
        FString MemberName = Property->GetNameCPP();
        if (Property->IsA<FBoolProperty>())
        {
            MemberName.RemoveFromStart(TEXT("b"));
        }
        const FString SetterName = TEXT("Set") + MemberName;
        int32 CompatibleSetters = 0;
        for (const auto& Value : Type->GetArrayField(TEXT("Callables")))
        {
            const auto Callable = Value->AsObject();
            const auto& Parameters = Callable->GetArrayField(TEXT("Parameters"));
            if (Callable->GetStringField(TEXT("Name")) != SetterName || Parameters.IsEmpty() ||
                Callable->GetIntegerField(TEXT("Access")) != 1 || !IsUnconditionalCallable(Callable))
            {
                continue;
            }
            FString ParameterType = Parameters[0]->AsObject()->GetStringField(TEXT("Type"));
            ParameterType.RemoveFromStart(TEXT("const "));
            ParameterType.RemoveFromStart(TEXT("class "));
            ParameterType.RemoveFromStart(TEXT("struct "));
            ParameterType.RemoveFromEnd(TEXT("&"));
            ParameterType.ReplaceInline(TEXT(" "), TEXT(""));
            FString ExtendedPropertyType;
            FString PropertyType = Property->GetCPPType(&ExtendedPropertyType) + ExtendedPropertyType;
            if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
            {
                if (!Property->IsA<FClassProperty>())
                {
                    PropertyType = FString(ObjectProperty->PropertyClass->GetPrefixCPP()) + ObjectProperty->PropertyClass->GetName() + TEXT("*");
                }
            }
            if (PropertyType.StartsWith(TEXT("TEnumAsByte<")))
            {
                PropertyType = PropertyType.Mid(12, PropertyType.Len() - 13);
            }
            if (Property->IsA<FBoolProperty>())
            {
                PropertyType = TEXT("bool");
            }
            PropertyType.ReplaceInline(TEXT(" "), TEXT(""));
            bool bCompatible = ParameterType == PropertyType;
            for (int32 ParameterIndex = 1; ParameterIndex < Parameters.Num(); ++ParameterIndex)
            {
                bCompatible &= !Parameters[ParameterIndex]->AsObject()->GetStringField(TEXT("Default")).IsEmpty();
            }
            if (bCompatible)
            {
                ++CompatibleSetters;
            }
        }
        if (CompatibleSetters == 1)
        {
            Setters.Add(SetterName);
        }
    }
    if (Setters.Num() == 1)
    {
        Result.SetterPropertyFunctionName = Setters.Array()[0];
    }
    return !Result.GetterPropertyFunctionName.IsEmpty() || !Result.SetterPropertyFunctionName.IsEmpty();
}

bool UBlueprintNativizationSettingsLibrary::FindNativeByClassAndName(UClass *OwnerClass, FString OwnerName,
                                                                     FFunctionDescriptor &OutNativeDesc)
{
    if (OwnerName.IsEmpty())
    {
        return false;
    }
    if (FConstructionScriptTranslation::TranslateNativeEvent(OwnerClass, OwnerName, OutNativeDesc))
    {
        return true;
    }
    UFunction *Event = OwnerClass->FindFunctionByName(FName(*OwnerName));
    if (!Event || !Event->HasAnyFunctionFlags(FUNC_BlueprintEvent))
    {
        return false;
    }
    if (!Event->HasAnyFunctionFlags(FUNC_Native))
    {
        return false;
    }
    for (UClass *Owner = OwnerClass; Owner; Owner = Owner->GetSuperClass())
    {
        if (!Owner->HasAnyClassFlags(CLASS_Native))
        {
            continue;
        }
        const auto Type = FNativeApiCatalog::FindType(Owner);
        if (!Type)
        {
            continue;
        }
        TArray<FFunctionDescriptor> Candidates;
        for (const auto &Value : Type->GetArrayField(TEXT("Callables")))
        {
            const auto Callable = Value->AsObject();
            bool bVirtual = false;
            const TSharedPtr<FJsonObject> *Calls = nullptr;
            if (!Callable->TryGetBoolField(TEXT("Virtual"), bVirtual) || !bVirtual ||
                !Callable->TryGetObjectField(TEXT("DirectCalls"), Calls) || !(*Calls)->HasField(OwnerName) ||
                Callable->GetStringField(TEXT("ReturnType")) != TEXT("void") || !IsUnconditionalCallable(Callable))
            {
                continue;
            }
            const auto &Arguments = (*Calls)->GetArrayField(OwnerName);
            const auto &Parameters = Callable->GetArrayField(TEXT("Parameters"));
            TArray<FProperty *> EventParameters;
            for (TFieldIterator<FProperty> Parameter(Event); Parameter; ++Parameter)
            {
                if (Parameter->HasAnyPropertyFlags(CPF_Parm) && !Parameter->HasAnyPropertyFlags(CPF_ReturnParm))
                {
                    EventParameters.Add(*Parameter);
                }
            }
            bool bCompatible = Arguments.Num() == EventParameters.Num() && Arguments.Num() == Parameters.Num();
            for (int32 ParameterIndex = 0; bCompatible && ParameterIndex < Parameters.Num(); ++ParameterIndex)
            {
                bCompatible &= Arguments[ParameterIndex]->AsString() ==
                               Parameters[ParameterIndex]->AsObject()->GetStringField(TEXT("Name"));
            }
            if (!bCompatible)
            {
                continue;
            }
            FFunctionDescriptor Candidate;
            Candidate.ClassName = UBlueprintNativizationLibrary::GetUniqueFieldName(Owner);
            Candidate.FunctionName = Callable->GetStringField(TEXT("Name"));
            for (const auto &Parameter : Parameters)
            {
                Candidate.ParameterSignature.Add(Parameter->AsObject()->GetStringField(TEXT("Type")) + TEXT(" ") +
                                                 Parameter->AsObject()->GetStringField(TEXT("Name")));
            }
            Candidates.Add(Candidate);
        }
        if (Candidates.Num() == 1)
        {
            OutNativeDesc = Candidates[0];
            return true;
        }
    }
    return false;
}

bool UBlueprintNativizationSettingsLibrary::FindNativeByBlueprint(const FFunctionDescriptor BlueprintDesc,
                                                                  FFunctionDescriptor &OutNativeDesc)
{
    for (TObjectIterator<UClass> Class; Class; ++Class)
    {
        if (UBlueprintNativizationLibrary::GetUniqueFieldName(*Class) == BlueprintDesc.ClassName)
        {
            return FindNativeByClassAndName(*Class, BlueprintDesc.FunctionName, OutNativeDesc);
        }
    }
    return false;
}

UStructTranslatorObject *UBlueprintNativizationSettingsLibrary::FindStructTranslator(const UScriptStruct *Structure)
{
    for (const auto &Class : GetDefault<UBlueprintNativizationV2EditorSettings>()->StructTranslators)
    {
        if (Class && Class->GetDefaultObject<UStructTranslatorObject>()->CanTranslate(Structure))
        {
            return Class->GetDefaultObject<UStructTranslatorObject>();
        }
    }
    const auto* ExternalTranslator = GetDefault<UExternalBlueprintStructTranslator>();
    if (ExternalTranslator->CanTranslate(Structure))
    {
        return const_cast<UExternalBlueprintStructTranslator*>(ExternalTranslator);
    }
    return nullptr;
}

static bool InitializeStructureFromCache(const FStructTranslationRequest& Request, FStructTranslationResult& Result)
{
    FStructOnScope Source(Request.Structure);
    const void* Value = FStructInitialization::ReadValue(Request, Source, Result.Error);
    if (!Value)
    {
        return false;
    }
    UScriptStruct* Structure = const_cast<UScriptStruct*>(Request.Structure);
    const auto Type = FNativeApiCatalog::FindType(Structure);
    if (!Type || Type->GetStringField(TEXT("Include")).IsEmpty())
    {
        return false;
    }
    Result.NativeType = UBlueprintNativizationLibrary::GetUniqueFieldName(Structure);
    Result.HeaderIncludes.Add(Type->GetStringField(TEXT("Include")));
    Result.ModuleDependencies.Add(Type->GetStringField(TEXT("Module")));
    FConstructorDescriptor Constructor;
    if (UBlueprintNativizationSettingsLibrary::FindConstructorDescriptorByStruct(Structure, Constructor))
    {
        TArray<FString> Arguments;
        for (const auto& Member : Constructor.ConstructorProperties)
        {
            FProperty* Property = Structure->FindPropertyByName(FName(*Member.OriginalPropertyName));
            if (!Property)
            {
                return false;
            }
            Arguments.Add(FStructInitialization::PropertyExpression(Property, Property->ContainerPtrToValuePtr<void>(Value), Request));
        }
        Result.Expression = Result.NativeType + TEXT("(") + FString::Join(Arguments, TEXT(", ")) + TEXT(")");
        return true;
    }
    bool bHasConstructors = false;
    bool bDefaultConstructor = false;
    bool bForceInitConstructor = false;
    TMap<FString, FString> DefaultMemberInitializers;
    for (const auto& CallableValue : Type->GetArrayField(TEXT("Callables")))
    {
        const auto Callable = CallableValue->AsObject();
        if (!Callable->GetBoolField(TEXT("Constructor")))
        {
            continue;
        }
        bHasConstructors = true;
        bool bHasRequiredParameter = false;
        const auto& Parameters = Callable->GetArrayField(TEXT("Parameters"));
        for (const auto& Parameter : Parameters)
        {
            bHasRequiredParameter |= Parameter->AsObject()->GetStringField(TEXT("Default")).IsEmpty();
        }
        const bool bAccessibleDefaultConstructor = !bHasRequiredParameter && Callable->GetIntegerField(TEXT("Access")) == 1 &&
            IsUnconditionalCallable(Callable) && IsHeaderDefinedCallable(Callable);
        bDefaultConstructor |= bAccessibleDefaultConstructor;
        if (bAccessibleDefaultConstructor && Parameters.IsEmpty() && HasEmptyConstructorBody(Callable))
        {
            for (const auto& Initializer : Callable->GetObjectField(TEXT("Initializers"))->Values)
            {
                DefaultMemberInitializers.Add(FString(Initializer.Key), Initializer.Value->AsString().TrimStartAndEnd());
            }
        }
        bForceInitConstructor |= Parameters.Num() == 1 &&
            Parameters[0]->AsObject()->GetStringField(TEXT("Type")) == TEXT("EForceInit") &&
            Callable->GetIntegerField(TEXT("Access")) == 1 && IsUnconditionalCallable(Callable) &&
            (Callable->GetBoolField(TEXT("Exported")) || Callable->GetBoolField(TEXT("InlineDefinition")));
    }
    const bool bRequiresForceInit = Structure->GetCppStructOps() && Structure->GetCppStructOps()->HasNoopConstructor();
    if ((bRequiresForceInit && !bForceInitConstructor) ||
        (!bRequiresForceInit && bHasConstructors && !bDefaultConstructor))
    {
        return false;
    }
    const FString Name = FStructInitialization::VariableName(Request, Structure->GetName() + TEXT("Value"));
    FString Initialization = TEXT("{}");
    if (bRequiresForceInit)
    {
        Initialization = TEXT("(ForceInit)");
    }
    Result.Preparations.Add(Result.NativeType + TEXT(" ") + Name + Initialization + TEXT(";"));
    FStructOnScope Defaults(Structure);
    const bool bRequiresExplicitMembers = Structure->GetCppStructOps() &&
        Structure->GetCppStructOps()->HasZeroConstructor();
    for (TFieldIterator<FProperty> Field(Structure); Field; ++Field)
    {
        if (Field->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
        {
            continue;
        }
        const void* FieldValue = Field->ContainerPtrToValuePtr<void>(Value);
        bool bKnownZeroDefault = false;
        if (const FString* Initializer = DefaultMemberInitializers.Find(Field->GetNameCPP()))
        {
            if (CastField<FNumericProperty>(*Field) || CastField<FBoolProperty>(*Field))
            {
                bKnownZeroDefault = *Initializer == TEXT("0") || *Initializer == TEXT("0.f") ||
                    *Initializer == TEXT("0.0f") || *Initializer == TEXT("0.0");
                if (CastField<FBoolProperty>(*Field) && *Initializer == TEXT("false"))
                {
                    bKnownZeroDefault = true;
                }
            }
        }
        if ((!bRequiresExplicitMembers || bKnownZeroDefault) &&
            Field->Identical(FieldValue, Field->ContainerPtrToValuePtr<void>(Defaults.GetStructMemory()), PPF_None))
        {
            continue;
        }
        FString Assignment;
        TSharedPtr<FJsonObject> FieldOwnerType = Type;
        if (Field->GetOwnerStruct() != Structure)
        {
            FieldOwnerType = FNativeApiCatalog::FindType(Field->GetOwnerStruct());
        }
        if (!FieldOwnerType)
        {
            return false;
        }
        for (const auto& MemberValue : FieldOwnerType->GetArrayField(TEXT("Members")))
        {
            const auto Member = MemberValue->AsObject();
            if (Member->GetStringField(TEXT("Name")) == Field->GetNameCPP() && Member->GetIntegerField(TEXT("Access")) == 1 &&
                Member->GetObjectField(TEXT("Evidence"))->GetStringField(TEXT("Condition")).IsEmpty() &&
                !Member->GetStringField(TEXT("Type")).StartsWith(TEXT("const ")))
            {
                Assignment = Name + TEXT(".") + Field->GetNameCPP() + TEXT(" = ");
                break;
            }
        }
        const FString Expression = FStructInitialization::PropertyExpression(*Field, FieldValue, Request);
        if (Assignment.IsEmpty())
        {
            FGetterAndSetterPropertyDescriptor Setter;
            if (!UBlueprintNativizationSettingsLibrary::FindGetterAndSetterDescriptorDescriptorByProperty(*Field, Setter) ||
                Setter.SetterPropertyFunctionName.IsEmpty())
            {
                return false;
            }
            bool bAccessibleSetter = false;
            for (const auto& CallableValue : FieldOwnerType->GetArrayField(TEXT("Callables")))
            {
                const auto Callable = CallableValue->AsObject();
                if (Callable->GetStringField(TEXT("Name")) == Setter.SetterPropertyFunctionName &&
                    (Callable->GetBoolField(TEXT("Exported")) || Callable->GetBoolField(TEXT("InlineDefinition"))))
                {
                    bAccessibleSetter = true;
                }
            }
            if (!bAccessibleSetter)
            {
                return false;
            }
            Result.Preparations.Add(Name + TEXT(".") + Setter.SetterPropertyFunctionName + TEXT("(") + Expression + TEXT(");"));
        }
        else
        {
            Result.Preparations.Add(Assignment + Expression + TEXT(";"));
        }
    }
    Result.Expression = Name;
    return true;
}

FString UBlueprintNativizationSettingsLibrary::TranslateStructure(const FStructTranslationRequest &Request, FGenerationPreparations* Preparations)
{
    const FNativeGenerationRequirements OriginalRequirements = FNativeGenerationContext::CaptureRequirements();
    const int32 OriginalErrorCount = FNativeApiCatalog::GetErrors().Num();
    FStructTranslationResult Result;
    UStructTranslatorObject *Translator = FindStructTranslator(Request.Structure);
    bool bTranslated = false;
    if (Translator)
    {
        bTranslated = Translator->Translate(Request, Result);
    }
    else if (FStructInitialization::IsValueOperation(Request.Operation))
    {
        bTranslated = InitializeStructureFromCache(Request, Result);
    }
    if (FStructInitialization::IsValueOperation(Request.Operation) && FNativeApiCatalog::GetErrors().Num() != OriginalErrorCount)
    {
        bTranslated = false;
    }
    if (!bTranslated)
    {
        FNativeGenerationContext::RestoreRequirements(OriginalRequirements);
        const FString TranslatorError = Result.Error;
        Result = {};
        bool bFallbackSucceeded = false;
        if (FStructInitialization::IsValueOperation(Request.Operation))
        {
            if (Translator)
            {
                bFallbackSucceeded = Translator->TranslateSerializedFallback(Request, Result);
            }
            else
            {
                bFallbackSucceeded = FStructInitialization::Fallback(Request, Result);
            }
        }
        if (!bFallbackSucceeded)
        {
            FNativeApiCatalog::AddError(FString::Format(TEXT("Structure translation failed for {0}: {1} {2}"),
                {Request.Structure->GetName(), TranslatorError, Result.Error}));
            return TEXT("{}");
        }
        FNativeApiCatalog::DiscardErrorsAfter(OriginalErrorCount);
    }
    FNativeGenerationContext::AddRequirements(Result);
    if (Request.Operation == EStructTranslationOperation::DescribeType)
    {
        return Result.NativeType;
    }
    if (!Request.DestinationExpression.IsEmpty())
    {
        if (!Result.InitializationInstructions.IsEmpty())
        {
            return FString::Join(Result.InitializationInstructions, TEXT("\n")) + TEXT("\n");
        }
        FString Expression = Result.Expression;
        if (!Result.Preparations.IsEmpty())
        {
            return TEXT("{\n") + FString::Join(Result.Preparations, TEXT("\n")) + TEXT("\n") +
                Request.DestinationExpression + TEXT(" = ") + Expression + TEXT(";\n}\n");
        }
        return Request.DestinationExpression + TEXT(" = ") + Expression + TEXT(";\n");
    }
    if (!Result.Preparations.IsEmpty())
    {
        if (Preparations)
        {
            for (const FString& Instruction : Result.Preparations)
            {
                Preparations->Add(Instruction);
            }
            return Result.Expression;
        }
        return TEXT("[&]() {\n") + FString::Join(Result.Preparations, TEXT("\n")) +
               TEXT("\nreturn ") + Result.Expression + TEXT(";\n}()");
    }
    return Result.Expression;
}
