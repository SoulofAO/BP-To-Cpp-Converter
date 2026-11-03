/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#include "NativeEventGeneration.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSettings.h"
#include "NativeGenerationContext.h"
#include "RetainedBlueprintGeneration.h"
#include "TranslatorObjects/StructTranslatorObject.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "UObject/UnrealType.h"
#include "NativeApiCatalog.h"
#include "GeneratedCodeCleanup.h"
#include "BlueprintNativizationUniqueNameSubsystem.h"
#include "Engine/Engine.h"

TSharedPtr<FJsonObject> FNativeEventGeneration::FindImplementation(const UFunction* Function)
{
    UFunction* RootFunction = UBlueprintNativizationLibrary::GetRootFunctionDeclaration(Function);
    if (!RootFunction || !RootFunction->GetOwnerClass()->HasAnyClassFlags(CLASS_Native) ||
        !RootFunction->HasAllFunctionFlags(FUNC_Native | FUNC_BlueprintEvent))
    {
        return nullptr;
    }
    const TSharedPtr<FJsonObject> Type = FNativeApiCatalog::FindType(RootFunction->GetOwnerClass());
    if (!Type)
    {
        return nullptr;
    }
    TSharedPtr<FJsonObject> Implementation;
    for (const TSharedPtr<FJsonValue>& Value : Type->GetArrayField(TEXT("Callables")))
    {
        const TSharedPtr<FJsonObject> Candidate = Value->AsObject();
        if (Candidate->GetStringField(TEXT("Name")) != RootFunction->GetName() + TEXT("_Implementation") ||
            !Candidate->GetBoolField(TEXT("Virtual")) || Candidate->GetBoolField(TEXT("Static")) ||
            Candidate->GetBoolField(TEXT("Deleted")) || !Candidate->GetBoolField(TEXT("AccessVerified")) ||
            Candidate->GetIntegerField(TEXT("Access")) == 3)
        {
            continue;
        }
        bool bUnconditional = true;
        for (const TSharedPtr<FJsonValue>& Evidence : Candidate->GetArrayField(TEXT("Evidence")))
        {
            if (!Evidence->AsObject()->GetStringField(TEXT("Condition")).IsEmpty())
            {
                bUnconditional = false;
                break;
            }
        }
        if (!bUnconditional)
        {
            continue;
        }
        int32 ParameterCount = 0;
        for (TFieldIterator<FProperty> Parameter(RootFunction); Parameter && Parameter->HasAnyPropertyFlags(CPF_Parm); ++Parameter)
        {
            if (!Parameter->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                ++ParameterCount;
            }
        }
        if (Candidate->GetArrayField(TEXT("Parameters")).Num() != ParameterCount)
        {
            continue;
        }
        if (Implementation)
        {
            FNativeApiCatalog::AddError(TEXT("Native event implementation is ambiguous: ") + RootFunction->GetPathName());
            return nullptr;
        }
        Implementation = Candidate;
    }
    return Implementation;
}

bool FNativeEventGeneration::IsImplementationConst(const UFunction* Function, const UClass* GeneratedClass)
{
    if (const TSharedPtr<FJsonObject> Implementation = FindImplementation(Function))
    {
        return Implementation->GetBoolField(TEXT("Const"));
    }
    return Function->HasAnyFunctionFlags(FUNC_Const) || GeneratedClass->HasAnyClassFlags(CLASS_Const);
}

static UFunction* GetDispatchedNativeEvent(UBlueprint* Blueprint, const FGenerateFunctionStruct& Entry)
{
    UFunction* Root = UBlueprintNativizationLibrary::GetRootFunctionDeclaration(Entry.OriginalUFunction);
    if (!Root || !Root->GetOwnerClass()->HasAnyClassFlags(CLASS_Native) ||
        !Root->HasAnyFunctionFlags(FUNC_BlueprintEvent) || Root->HasAnyFunctionFlags(FUNC_Native))
    {
        return nullptr;
    }
    FFunctionDescriptor NativeMethod;
    if (UBlueprintNativizationSettingsLibrary::FindNativeByClassAndName(Blueprint->GeneratedClass, Root->GetName(), NativeMethod))
    {
        return nullptr;
    }
    return Root;
}

FString FNativeEventGeneration::GenerateConstructor(UBlueprint* Blueprint, const TArray<FGenerateFunctionStruct>& Entries)
{
    FString Code;
    TSet<FString> Implementations;
    FString BehaviorTreeBase;
    for (const FGenerateFunctionStruct& Entry : Entries)
    {
        UFunction* Event = GetDispatchedNativeEvent(Blueprint, Entry);
        if (!Event)
        {
            continue;
        }
        const FString OwnerName = Event->GetOwnerClass()->GetName();
        if (OwnerName != TEXT("BTTask_BlueprintBase") && OwnerName != TEXT("BTService_BlueprintBase") &&
            OwnerName != TEXT("BTDecorator_BlueprintBase"))
        {
            continue;
        }
        BehaviorTreeBase = OwnerName;
        FString EventName = Event->GetName();
        FString Implementation = TEXT("FBTNodeBPImplementationHelper::Generic");
        if (EventName.RemoveFromEnd(TEXT("AI")))
        {
            Implementation = TEXT("FBTNodeBPImplementationHelper::AISpecific");
        }
        Code += EventName + TEXT("Implementations |= ") + Implementation + TEXT(";\n");
        Implementations.Add(EventName);
    }
    if (Code.IsEmpty())
    {
        return Code;
    }
    FStructTranslationResult Requirements;
    Requirements.HeaderIncludes.Add(TEXT("BehaviorTree/BTFunctionLibrary.h"));
    Requirements.ModuleDependencies.Add(TEXT("AIModule"));
    FNativeGenerationContext::AddRequirements(Requirements);
    if (Implementations.Contains(TEXT("ReceiveTick")))
    {
        Code += TEXT("bNotifyTick = ReceiveTickImplementations != 0;\n");
    }
    if (BehaviorTreeBase == TEXT("BTService_BlueprintBase"))
    {
        Code += TEXT("bNotifyBecomeRelevant = ReceiveActivationImplementations != 0;\n");
        Code += TEXT("bNotifyCeaseRelevant = ReceiveDeactivationImplementations != 0;\n");
        Code += TEXT("bNotifyOnSearch = ReceiveSearchStartImplementations != 0;\n");
    }
    else if (BehaviorTreeBase == TEXT("BTDecorator_BlueprintBase"))
    {
        Code += TEXT("bNotifyBecomeRelevant = ReceiveObserverActivatedImplementations != 0 || PerformConditionCheckImplementations != 0;\n");
        Code += TEXT("bNotifyCeaseRelevant = ReceiveObserverDeactivatedImplementations != 0 || PerformConditionCheckImplementations != 0;\n");
        Code += TEXT("bNotifyActivation = ReceiveExecutionStartImplementations != 0;\n");
        Code += TEXT("bNotifyDeactivation = ReceiveExecutionFinishImplementations != 0;\n");
    }
    return Code;
}

static bool CompleteEnvironmentQueryContext(UBlueprint* Blueprint, FString& Header, FString& Source)
{
    UClass* ContextBase = FindObject<UClass>(nullptr, TEXT("/Script/AIModule.EnvQueryContext_BlueprintBase"));
    if (!ContextBase || !Blueprint->GeneratedClass->IsChildOf(ContextBase))
    {
        return false;
    }
    UClass* NativeParent = Blueprint->GeneratedClass->GetSuperClass();
    while (NativeParent && !NativeParent->HasAnyClassFlags(CLASS_Native))
    {
        NativeParent = NativeParent->GetSuperClass();
    }
    if (NativeParent != ContextBase)
    {
        return false;
    }
    const TArray<FString> Events = {TEXT("ProvideSingleActor"), TEXT("ProvideSingleLocation"),
        TEXT("ProvideActorsSet"), TEXT("ProvideLocationsSet")};
    TArray<bool> ImplementedModes;
    ImplementedModes.Init(false, Events.Num());
    for (int32 ModeIndex = 0; ModeIndex < Events.Num(); ++ModeIndex)
    {
        for (UClass* Layer = Blueprint->GeneratedClass; Layer && Layer != ContextBase; Layer = Layer->GetSuperClass())
        {
            UBlueprint* LayerBlueprint = Cast<UBlueprint>(Layer->ClassGeneratedBy);
            if (LayerBlueprint && UBlueprintNativizationLibrary::FindEntryNodeByFunctionName(LayerBlueprint, FName(*Events[ModeIndex])))
            {
                ImplementedModes[ModeIndex] = true;
                break;
            }
        }
    }
    const FString ClassName = UBlueprintNativizationLibrary::GetUniqueFieldName(Blueprint->GeneratedClass);
    Header.InsertAt(Header.Find(TEXT("};"), ESearchCase::CaseSensitive, ESearchDir::FromEnd),
        TEXT("public:\nvirtual UWorld* GetWorld() const override;\n")
        TEXT("virtual void ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const override;\n"));
    Source += TEXT("\nUWorld* ") + ClassName + TEXT("::GetWorld() const\n{\ncheck(GetOuter());\n")
        TEXT("if (UEnvQueryManager* QueryManager = Cast<UEnvQueryManager>(GetOuter()))\n{\nreturn QueryManager->GetWorld();\n}\nreturn nullptr;\n}\n");
    Source += TEXT("\nvoid ") + ClassName + TEXT("::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const\n{\n")
        TEXT("UObject* QuerierObject = QueryInstance.Owner.Get();\nif (!QuerierObject)\n{\nreturn;\n}\n")
        TEXT("AActor* QuerierActor = Cast<AActor>(QuerierObject);\nint32 ContextMode = INDEX_NONE;\nUFunction* ContextFunction = nullptr;\n");
    for (int32 ModeIndex = 0; ModeIndex < Events.Num(); ++ModeIndex)
    {
        Source += TEXT("{\nUFunction* CandidateFunction = FindFunctionChecked(FName(TEXT(\"") + Events[ModeIndex] + TEXT("\")));\n");
        if (!ImplementedModes[ModeIndex])
        {
            Source += TEXT("if (CandidateFunction->GetOuterUClass() != UEnvQueryContext_BlueprintBase::StaticClass())\n{\n");
        }
        Source += TEXT("ContextMode = ") + FString::FromInt(ModeIndex) + TEXT(";\nContextFunction = CandidateFunction;\n");
        if (!ImplementedModes[ModeIndex])
        {
            Source += TEXT("}\n");
        }
        Source += TEXT("}\n");
    }
    Source += TEXT("switch (ContextMode)\n{\n");
    const TArray<FString> ResultTypes = {TEXT("AActor*"), TEXT("FVector"), TEXT("TArray<AActor*>"), TEXT("TArray<FVector>")};
    const TArray<FString> InitialValues = {TEXT("nullptr"), TEXT("FAISystem::InvalidLocation"), TEXT("{}"), TEXT("{}")};
    for (int32 ModeIndex = 0; ModeIndex < Events.Num(); ++ModeIndex)
    {
        Source += TEXT("case ") + FString::FromInt(ModeIndex) + TEXT(":\n{\nstruct FContextEventParameters\n{\nUObject* QuerierObject;\nAActor* QuerierActor;\n") +
            ResultTypes[ModeIndex] + TEXT(" Result;\n};\nFContextEventParameters Parameters{QuerierObject, QuerierActor, ") + InitialValues[ModeIndex] + TEXT("};\n") +
            TEXT("const_cast<") + ClassName + TEXT("*>(this)->ProcessEvent(ContextFunction, &Parameters);\n");
        if (ModeIndex == 0)
        {
            Source += TEXT("if (Parameters.Result)\n{\nUEnvQueryItemType_Actor::SetContextHelper(ContextData, Parameters.Result);\n}\n");
        }
        else if (ModeIndex == 1)
        {
            Source += TEXT("if (FAISystem::IsValidLocation(Parameters.Result))\n{\nUEnvQueryItemType_Point::SetContextHelper(ContextData, Parameters.Result);\n}\n");
        }
        else if (ModeIndex == 2)
        {
            Source += TEXT("Parameters.Result.Remove(nullptr);\nif (!Parameters.Result.IsEmpty())\n{\nUEnvQueryItemType_Actor::SetContextHelper(ContextData, Parameters.Result);\n}\n");
        }
        else
        {
            Source += TEXT("Parameters.Result.RemoveAll([](const FVector& Location)\n{\nreturn !FAISystem::IsValidLocation(Location);\n});\n")
                TEXT("if (!Parameters.Result.IsEmpty())\n{\nUEnvQueryItemType_Point::SetContextHelper(ContextData, Parameters.Result);\n}\n");
        }
        Source += TEXT("break;\n}\n");
    }
    Source += TEXT("default:\n{\nbreak;\n}\n}\n}\n");
    FStructTranslationResult Requirements;
    Requirements.HeaderIncludes = {TEXT("AITypes.h"), TEXT("EnvironmentQuery/EnvQueryTypes.h"), TEXT("UObject/Class.h"),
        TEXT("EnvironmentQuery/EnvQueryManager.h"), TEXT("EnvironmentQuery/Items/EnvQueryItemType_Actor.h"),
        TEXT("EnvironmentQuery/Items/EnvQueryItemType_Point.h"), TEXT("GameFramework/Actor.h")};
    Requirements.ModuleDependencies.Add(TEXT("AIModule"));
    FNativeGenerationContext::AddRequirements(Requirements);
    return true;
}

static void CompleteEnvironmentQueryGeneratorContracts(UBlueprint* Blueprint,
    const TArray<FGenerateFunctionStruct>& Entries, FString& Header, FString& Source)
{
    UClass* GeneratorBase = FindObject<UClass>(nullptr, TEXT("/Script/AIModule.EnvQueryGenerator_BlueprintBase"));
    UClass* NativeParent = Blueprint->GeneratedClass->GetSuperClass();
    while (NativeParent && !NativeParent->HasAnyClassFlags(CLASS_Native))
    {
        NativeParent = NativeParent->GetSuperClass();
    }
    if (!GeneratorBase || NativeParent != GeneratorBase)
    {
        return;
    }
    TArray<FString> Contracts;
    for (const FGenerateFunctionStruct& Entry : Entries)
    {
        UFunction* Event = GetDispatchedNativeEvent(Blueprint, Entry);
        if (Event && Event->GetOwnerClass() == GeneratorBase &&
            (Event->GetFName() == TEXT("DoItemGeneration") || Event->GetFName() == TEXT("DoItemGenerationFromActors")))
        {
            Contracts.AddUnique(TEXT("FName(") + FRetainedBlueprintGeneration::Quote(Event->GetName()) + TEXT(")"));
        }
    }
    if (Contracts.IsEmpty())
    {
        return;
    }
    const FString ClassName = UBlueprintNativizationLibrary::GetUniqueFieldName(Blueprint->GeneratedClass);
    const FString Registration = UBlueprintNativizationLibrary::GenerateLambdaConstructorUniqueVariableName(
        Blueprint->GeneratedClass, TEXT("RegisterGeneratorContracts"));
    const FString Constructor = ClassName + TEXT("::") + ClassName + TEXT("(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)");
    if (!Source.Contains(Constructor))
    {
        FNativeApiCatalog::AddError(TEXT("Cannot register EQS generator event contracts before the native parent constructor: ") + ClassName);
        return;
    }
    Source.ReplaceInline(*Constructor, *(ClassName + TEXT("::") + ClassName +
        TEXT("(const FObjectInitializer& ObjectInitializer) : Super(") + Registration + TEXT("(ObjectInitializer))")));
    Header.InsertAt(Header.Find(TEXT("};"), ESearchCase::CaseSensitive, ESearchDir::FromEnd),
        TEXT("private:\nstatic const FObjectInitializer& ") + Registration + TEXT("(const FObjectInitializer& ObjectInitializer);\n"));
    Source += TEXT("\nconst FObjectInitializer& ") + ClassName + TEXT("::") + Registration +
        TEXT("(const FObjectInitializer& ObjectInitializer)\n{\nstatic FCriticalSection RegistrationMutex;\nFScopeLock RegistrationLock(&RegistrationMutex);\nUClass* GeneratorClass = ") +
        ClassName + TEXT("::StaticClass();\nfor (const FName EventName : {") + FString::Join(Contracts, TEXT(", ")) +
        TEXT("})\n{\nif (!GeneratorClass->FindFunctionByName(EventName, EIncludeSuperFlag::ExcludeSuper))\n{\nUFunction* ParentContract = UEnvQueryGenerator_BlueprintBase::StaticClass()->FindFunctionByName(EventName);\nUFunction* Contract = DuplicateObject<UFunction>(ParentContract, GeneratorClass, EventName);\nContract->SetSuperStruct(ParentContract);\nGeneratorClass->AddFunctionToFunctionMap(Contract, EventName);\n}\n}\nObjectInitializer.GetClass()->ClearFunctionMapsCaches();\nreturn ObjectInitializer;\n}\n");
    FNativeGenerationContext::AddImplementationInclude(TEXT("#include \"HAL/CriticalSection.h\""));
    FNativeGenerationContext::AddImplementationInclude(TEXT("#include \"Misc/ScopeLock.h\""));
    FNativeGenerationContext::AddImplementationInclude(TEXT("#include \"UObject/UObjectGlobals.h\""));
}

void FNativeEventGeneration::Complete(UBlueprint* Blueprint, const TArray<FGenerateFunctionStruct>& Entries,
    FString& Header, FString& Source)
{
    struct FEventOverride
    {
        TSharedPtr<FJsonObject> Method;
        FString Body;
        FString OriginalParentClass;
        FString Diagnostic;
        TMap<FString, FString> Implementations;
        TArray<TPair<UFunction*, FString>> Events;
    };
    const bool bHasEnvironmentQueryAdapter = CompleteEnvironmentQueryContext(Blueprint, Header, Source);
    TMap<FString, FEventOverride> Overrides;
    TSet<FName> AdaptedEvents;
    FStructTranslationResult OverrideRequirements;
    for (const FGenerateFunctionStruct& Entry : Entries)
    {
        UFunction* Event = GetDispatchedNativeEvent(Blueprint, Entry);
        if (!Event || AdaptedEvents.Contains(Event->GetFName()))
        {
            continue;
        }
        if (bHasEnvironmentQueryAdapter && Event->GetOwnerClass()->GetPathName() == TEXT("/Script/AIModule.EnvQueryContext_BlueprintBase"))
        {
            continue;
        }
        bool bInheritedImplementation = false;
        for (UClass* Parent = Blueprint->GeneratedClass->GetSuperClass(); Parent && !Parent->HasAnyClassFlags(CLASS_Native); Parent = Parent->GetSuperClass())
        {
            UBlueprint* ParentBlueprint = Cast<UBlueprint>(Parent->ClassGeneratedBy);
            if (!FRetainedBlueprintGeneration::IsRetained(Parent) && ParentBlueprint &&
                UBlueprintNativizationLibrary::FindEntryNodeByFunctionName(ParentBlueprint, Event->GetFName()))
            {
                bInheritedImplementation = true;
                break;
            }
        }
        if (bInheritedImplementation)
        {
            AdaptedEvents.Add(Event->GetFName());
            continue;
        }
        UClass* MethodOwner = nullptr;
        TSharedPtr<FJsonObject> Method;
        FString Body;
        FString Diagnostic;
        TSet<FString> Includes;
        TSet<FString> Modules;
        if (!FNativeApiCatalog::FindEventOverride(Blueprint->GeneratedClass, Event, MethodOwner, Method,
            Body, Diagnostic, Includes, Modules))
        {
            UE_LOG(LogTemp, Warning, TEXT("%s"), *FString::Format(
                TEXT("Event '{0}' requires exceptional ProcessEvent dispatch: {1}"), {Event->GetPathName(), Diagnostic}));
            continue;
        }
        bool bSharesInheritedCaller = false;
        for (UClass* Parent = Blueprint->GeneratedClass->GetSuperClass(); Parent && !Parent->HasAnyClassFlags(CLASS_Native); Parent = Parent->GetSuperClass())
        {
            if (FRetainedBlueprintGeneration::IsRetained(Parent))
            {
                continue;
            }
            for (TFieldIterator<UFunction> ParentFunction(Parent, EFieldIteratorFlags::ExcludeSuper); ParentFunction; ++ParentFunction)
            {
                UBlueprint* ParentBlueprint = Cast<UBlueprint>(Parent->ClassGeneratedBy);
                if (Method->GetObjectField(TEXT("DirectCalls"))->HasField(ParentFunction->GetName()) && ParentBlueprint &&
                    UBlueprintNativizationLibrary::FindEntryNodeByFunctionName(ParentBlueprint, ParentFunction->GetFName()))
                {
                    bSharesInheritedCaller = true;
                    break;
                }
            }
        }
        if (bSharesInheritedCaller)
        {
            UE_LOG(LogTemp, Warning, TEXT("%s"), *FString::Format(
                TEXT("Event '{0}' requires exceptional ProcessEvent dispatch: its virtual caller already adapts a different event in a generated parent."),
                {Event->GetPathName()}));
            continue;
        }
        OverrideRequirements.HeaderIncludes.Append(Includes.Array());
        OverrideRequirements.ModuleDependencies.Append(Modules.Array());
        FEventOverride& Override = Overrides.FindOrAdd(Method->GetStringField(TEXT("Signature")));
        Override.Method = Method;
        Override.Body = Body;
        Override.OriginalParentClass = UBlueprintNativizationLibrary::GetUniqueFieldName(MethodOwner->GetSuperClass());
        Override.Diagnostic = Diagnostic;
        Override.Implementations.Add(Event->GetName(), Entry.Name.ToString() + TEXT("_Implementation"));
        Override.Events.Emplace(Event, Entry.Name.ToString() + TEXT("_Implementation"));
        AdaptedEvents.Add(Event->GetFName());
    }
    const FString ClassName = UBlueprintNativizationLibrary::GetUniqueFieldName(Blueprint->GeneratedClass);
    for (const TPair<FString, FEventOverride>& Pair : Overrides)
    {
        const FEventOverride& Override = Pair.Value;
        const TArray<TSharedPtr<FJsonValue>>& Parameters = Override.Method->GetArrayField(TEXT("Parameters"));
        TArray<FString> Signatures;
        TArray<FString> Arguments;
        for (const TSharedPtr<FJsonValue>& ParameterValue : Parameters)
        {
            const TSharedPtr<FJsonObject> Parameter = ParameterValue->AsObject();
            Arguments.Add(Parameter->GetStringField(TEXT("Name")));
        }
        int32 ReplacedCalls = 0;
        FString Body = FGeneratedCodeCleanup::RewriteNativeEventBody(Override.Body, Override.Implementations,
            ReplacedCalls, ClassName, Override.OriginalParentClass);
        if (Body.IsEmpty() || ReplacedCalls < Override.Events.Num())
        {
            if (Override.Method->GetStringField(TEXT("ReturnType")) != TEXT("void"))
            {
                for (const TPair<UFunction*, FString>& Event : Override.Events)
                {
                    AdaptedEvents.Remove(Event.Key->GetFName());
                    UE_LOG(LogTemp, Warning, TEXT("%s"), *FString::Format(
                        TEXT("Event '{0}' requires exceptional ProcessEvent dispatch because its non-void caller cannot be reproduced: {1}"),
                        {Event.Key->GetPathName(), Override.Diagnostic}));
                }
                continue;
            }
            Body = TEXT("{\nSuper::") + Override.Method->GetStringField(TEXT("Name")) +
                TEXT("(") + FString::Join(Arguments, TEXT(", ")) + TEXT(");\n");
            bool bCompatible = true;
            for (const TPair<UFunction*, FString>& Event : Override.Events)
            {
                TArray<FString> EventArguments;
                const TArray<TSharedPtr<FJsonValue>>* CachedCallArguments = nullptr;
                if (!Override.Method->GetObjectField(TEXT("DirectCalls"))->TryGetArrayField(Event.Key->GetName(), CachedCallArguments))
                {
                    bCompatible = false;
                    break;
                }
                const TArray<TSharedPtr<FJsonValue>>& CachedArguments = *CachedCallArguments;
                int32 ParameterIndex = 0;
                for (TFieldIterator<FProperty> Parameter(Event.Key); Parameter && Parameter->HasAnyPropertyFlags(CPF_Parm); ++Parameter)
                {
                    if (Parameter->HasAnyPropertyFlags(CPF_ReturnParm) ||
                        (Parameter->HasAnyPropertyFlags(CPF_OutParm) && !Parameter->HasAnyPropertyFlags(CPF_ConstParm)) ||
                        ParameterIndex >= CachedArguments.Num())
                    {
                        bCompatible = false;
                        break;
                    }
                    const FString CachedArgument = CachedArguments[ParameterIndex]->AsString().TrimStartAndEnd();
                    TSharedPtr<FJsonObject> NativeParameter;
                    for (const TSharedPtr<FJsonValue>& ParameterValue : Parameters)
                    {
                        if (ParameterValue->AsObject()->GetStringField(TEXT("Name")) == CachedArgument)
                        {
                            NativeParameter = ParameterValue->AsObject();
                            break;
                        }
                    }
                    if (!NativeParameter)
                    {
                        FString GetterName = CachedArgument;
                        if (GetterName.RemoveFromEnd(TEXT("()")))
                        {
                            UFunction* Getter = Blueprint->GeneratedClass->FindFunctionByName(FName(*GetterName));
                            if (Getter && Getter->HasAllFunctionFlags(FUNC_Native | FUNC_Public | FUNC_Const) &&
                                Getter->NumParms == 1 && Getter->GetReturnProperty())
                            {
                                const TSharedPtr<FJsonObject> CachedGetter = FNativeApiCatalog::FindCallable(Getter);
                                if (CachedGetter && CachedGetter->GetIntegerField(TEXT("Access")) == 1)
                                {
                                    NativeParameter = MakeShared<FJsonObject>();
                                    NativeParameter->SetStringField(TEXT("Name"), CachedArgument);
                                    NativeParameter->SetStringField(TEXT("Type"),
                                        UBlueprintNativizationLibrary::GetPropertyType(Getter->GetReturnProperty()));
                                }
                            }
                        }
                    }
                    if (!NativeParameter)
                    {
                        bCompatible = false;
                        break;
                    }
                    if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(*Parameter))
                    {
                        EventArguments.Add(TEXT("Cast<") + FRetainedBlueprintGeneration::GetClassName(ObjectProperty->PropertyClass) +
                            TEXT(">(") + NativeParameter->GetStringField(TEXT("Name")) + TEXT(")"));
                    }
                    else
                    {
                        FString NativeType = NativeParameter->GetStringField(TEXT("Type"));
                        FString EventType = UBlueprintNativizationLibrary::GetPropertyType(*Parameter);
                        NativeType.RemoveFromStart(TEXT("const "));
                        NativeType.ReplaceInline(TEXT(" "), TEXT(""));
                        EventType.ReplaceInline(TEXT(" "), TEXT(""));
                        if (Parameter->HasAnyPropertyFlags(CPF_ConstParm))
                        {
                            NativeType.RemoveFromEnd(TEXT("&"));
                            EventType.RemoveFromStart(TEXT("const"));
                            EventType.RemoveFromEnd(TEXT("&"));
                        }
                        if (NativeType.StartsWith(TEXT("TEnumAsByte<")) && NativeType.EndsWith(TEXT(">")))
                        {
                            NativeType = NativeType.Mid(12, NativeType.Len() - 13);
                        }
                        if (EventType.StartsWith(TEXT("TEnumAsByte<")) && EventType.EndsWith(TEXT(">")))
                        {
                            EventType = EventType.Mid(12, EventType.Len() - 13);
                        }
                        if (NativeType != EventType)
                        {
                            bCompatible = false;
                            break;
                        }
                        EventArguments.Add(NativeParameter->GetStringField(TEXT("Name")));
                    }
                    ++ParameterIndex;
                }
                bCompatible &= ParameterIndex == CachedArguments.Num();
                if (!bCompatible)
                {
                    break;
                }
                Body += Event.Value + TEXT("(") + FString::Join(EventArguments, TEXT(", ")) + TEXT(");\n");
            }
            if (!bCompatible)
            {
                for (const TPair<UFunction*, FString>& Event : Override.Events)
                {
                    AdaptedEvents.Remove(Event.Key->GetFName());
                    UE_LOG(LogTemp, Warning, TEXT("%s"), *FString::Format(
                        TEXT("Event '{0}' requires exceptional ProcessEvent dispatch: cached arguments depend on unavailable locals or unsupported conversions."),
                        {Event.Key->GetPathName()}));
                }
                continue;
            }
            Body += TEXT("}");
            UE_LOG(LogTemp, Warning, TEXT("%s"), *FString::Format(
                TEXT("Native event adapter '{0}::{1}' runs event logic after Super; original ordering is not preserved. {2} Direct Blueprint event calls outside this virtual method are not covered."),
                {ClassName, Override.Method->GetStringField(TEXT("Name")), Override.Diagnostic}));
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("%s"), *FString::Format(
                TEXT("Native event adapter '{0}::{1}' preserves the cached caller body. Direct reflected event calls outside this virtual method are not covered."),
                {ClassName, Override.Method->GetStringField(TEXT("Name"))}));
        }
        UBlueprintNativizationUniqueNameSubsystem* Names = GEngine->GetEngineSubsystem<UBlueprintNativizationUniqueNameSubsystem>();
        TArray<FName> MemberNames;
        for (UClass* Layer = Blueprint->GeneratedClass; Layer; Layer = Layer->GetSuperClass())
        {
            MemberNames.Append(Names->GetAllClassGlobalPropertyNames(Layer));
            MemberNames.Append(Names->GetAllFunctionNames(Layer));
        }
        TMap<FString, FString> ParameterNames;
        for (const TSharedPtr<FJsonValue>& ParameterValue : Parameters)
        {
            const TSharedPtr<FJsonObject> Parameter = ParameterValue->AsObject();
            const FString OriginalName = Parameter->GetStringField(TEXT("Name"));
            FString ParameterName = OriginalName;
            if (MemberNames.Contains(FName(*OriginalName)))
            {
                FString BaseName = TEXT("NativeEvent") + OriginalName;
                do
                {
                    ParameterName = Names->GetLambdaUniqueVariableName(BaseName, Blueprint->GeneratedClass,
                        Pair.Key + TEXT(":") + OriginalName).ToString();
                    BaseName += TEXT("Input");
                }
                while (Body.Contains(ParameterName) || Arguments.Contains(ParameterName));
                ParameterNames.Add(OriginalName, ParameterName);
            }
            Signatures.Add(Parameter->GetStringField(TEXT("Type")) + TEXT(" ") + ParameterName);
        }
        if (!ParameterNames.IsEmpty())
        {
            Body = FGeneratedCodeCleanup::RenameGeneratedParameters(Body, ParameterNames);
        }
        FString ConstQualifier;
        if (Override.Method->GetBoolField(TEXT("Const")))
        {
            ConstQualifier = TEXT(" const");
        }
        const FString Signature = Override.Method->GetStringField(TEXT("ReturnType")) + TEXT(" ") +
            Override.Method->GetStringField(TEXT("Name")) + TEXT("(") + FString::Join(Signatures, TEXT(", ")) + TEXT(")") + ConstQualifier;
        Header.InsertAt(Header.Find(TEXT("};"), ESearchCase::CaseSensitive, ESearchDir::FromEnd),
            TEXT("public:\nvirtual ") + Signature + TEXT(" override;\n"));
        Source += TEXT("\n") + Override.Method->GetStringField(TEXT("ReturnType")) + TEXT(" ") + ClassName + TEXT("::") +
            Override.Method->GetStringField(TEXT("Name")) + TEXT("(") + FString::Join(Signatures, TEXT(", ")) + TEXT(")") + ConstQualifier + TEXT("\n") + Body + TEXT("\n");
    }
    FNativeGenerationContext::AddRequirements(OverrideRequirements);
    FString Dispatch;
    TSet<FName> Events;
    for (const FGenerateFunctionStruct& Entry : Entries)
    {
        UFunction* Event = GetDispatchedNativeEvent(Blueprint, Entry);
        if (!Event || AdaptedEvents.Contains(Event->GetFName()) || Events.Contains(Event->GetFName()))
        {
            continue;
        }
        Events.Add(Event->GetFName());
        const FString OwnerClass = UBlueprintNativizationLibrary::GetUniqueFieldName(Event->GetOwnerClass());
        FString OwnerCondition = TEXT("Function->GetOuterUClass() == ") + OwnerClass + TEXT("::StaticClass()");
        if (Event->GetOwnerClass()->GetPathName() == TEXT("/Script/AIModule.EnvQueryGenerator_BlueprintBase"))
        {
            OwnerCondition = TEXT("(") + OwnerCondition + TEXT(" || (Function->GetOuterUClass()->HasAnyClassFlags(CLASS_Native) && Function->GetSuperFunction() == ") +
                OwnerClass + TEXT("::StaticClass()->FindFunctionByName(Function->GetFName())))");
        }
        Dispatch += TEXT("if (Function && Function->GetFName() == FName(") + FRetainedBlueprintGeneration::Quote(Event->GetName()) +
            TEXT(") && ") + OwnerCondition + TEXT(")\n{\n");
        TArray<FString> Arguments;
        FString Output;
        FString ReturnValue;
        for (TFieldIterator<FProperty> Parameter(Event); Parameter && Parameter->HasAnyPropertyFlags(CPF_Parm); ++Parameter)
        {
            FProperty* Property = *Parameter;
            const FString Value = TEXT("EventValue") + Property->GetName();
            const FString Descriptor = TEXT("EventProperty") + Property->GetName();
            const FString Address = TEXT("EventAddress") + Property->GetName();
            FEdGraphPinType PinType;
            GetDefault<UEdGraphSchema_K2>()->ConvertPropertyToPinType(Property, PinType);
            const bool bUsesParameterStorage = Property->IsA<FStructProperty>() &&
                !Property->HasAnyPropertyFlags(CPF_ReturnParm) &&
                Property->HasAnyPropertyFlags(CPF_ReferenceParm | CPF_ConstParm | CPF_OutParm);
            if (!bUsesParameterStorage)
            {
                Dispatch += UBlueprintNativizationLibrary::GetPinType(PinType, false) + TEXT(" ") + Value + TEXT("{};\n");
            }
            Dispatch += TEXT("FProperty* ") + Descriptor + TEXT(" = FindFProperty<FProperty>(Function, ") +
                FRetainedBlueprintGeneration::Quote(Property->GetName()) + TEXT(");\n");
            Dispatch += TEXT("check(") + Descriptor + TEXT(");\nvoid* ") + Address + TEXT(" = ") + Descriptor + TEXT("->ContainerPtrToValuePtr<void>(Parameters);\n");
            if (bUsesParameterStorage)
            {
                PinType.bIsReference = false;
                PinType.bIsConst = false;
                FString ParameterType = UBlueprintNativizationLibrary::GetPinType(PinType, false);
                if (Property->HasAnyPropertyFlags(CPF_ConstParm))
                {
                    ParameterType = TEXT("const ") + ParameterType;
                }
                Dispatch += ParameterType + TEXT("& ") + Value + TEXT(" = *static_cast<") + ParameterType +
                    TEXT("*>(") + Address + TEXT(");\n");
                Arguments.Add(Value);
                continue;
            }
            if (Property->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                ReturnValue = Value;
            }
            else
            {
                Arguments.Add(Value);
                Dispatch += FRetainedBlueprintGeneration::Transfer(Property, Descriptor, Address, Value, false);
            }
            if (Property->HasAnyPropertyFlags(CPF_ReturnParm) ||
                (Property->HasAnyPropertyFlags(CPF_OutParm) && !Property->HasAnyPropertyFlags(CPF_ConstParm)))
            {
                Output += FRetainedBlueprintGeneration::Transfer(Property, Descriptor, Address, Value, true);
            }
        }
        if (!ReturnValue.IsEmpty())
        {
            Dispatch += ReturnValue + TEXT(" = ");
        }
        Dispatch += Entry.Name.ToString() + TEXT("_Implementation(") + FString::Join(Arguments, TEXT(", ")) + TEXT(");\n") +
            Output + TEXT("return;\n}\n");
    }
    if (Dispatch.IsEmpty())
    {
        return;
    }
    const int32 ClassEnd = Header.Find(TEXT("};"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
    Header.InsertAt(ClassEnd, TEXT("public:\nvirtual void ProcessEvent(UFunction* Function, void* Parameters) override;\n"));
    Source += TEXT("\nvoid ") + UBlueprintNativizationLibrary::GetUniqueFieldName(Blueprint->GeneratedClass) +
        TEXT("::ProcessEvent(UFunction* Function, void* Parameters)\n{\n") + Dispatch + TEXT("Super::ProcessEvent(Function, Parameters);\n}\n");
    FStructTranslationResult Requirements;
    Requirements.HeaderIncludes.Add(TEXT("UObject/UnrealType.h"));
    FNativeGenerationContext::AddRequirements(Requirements);
    CompleteEnvironmentQueryGeneratorContracts(Blueprint, Entries, Header, Source);
}
