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
            Signatures.Add(Parameter->GetStringField(TEXT("Type")) + TEXT(" ") + Parameter->GetStringField(TEXT("Name")));
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
        Dispatch += TEXT("if (Function && Function->GetFName() == FName(") + FRetainedBlueprintGeneration::Quote(Event->GetName()) +
            TEXT(") && Function->GetOuterUClass() == ") + OwnerClass + TEXT("::StaticClass())\n{\n");
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
}
