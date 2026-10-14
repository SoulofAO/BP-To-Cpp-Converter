#include "NativeApiPreflight.h"
#include "NativeApiCatalog.h"
#include "NativeModuleCache.h"
#include "BlueprintNativeApiCacheLibrary.h"
#include "BlueprintNativizationSettings.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "K2Node_CallParentFunction.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "EdGraphSchema_K2.h"
#include "TranslatorObjects/CallParentTranslatorObject.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "SourceCodeNavigation.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

struct FSourceInsertion
{
    FString Filename;
    int32 Position = 0;
    FString Text;
};

static bool IsUnconditional(const TSharedPtr<FJsonObject>& Callable)
{
    if (!Callable->GetBoolField(TEXT("AccessVerified")))
    {
        return false;
    }
    for (const auto& Value : Callable->GetArrayField(TEXT("Evidence")))
    {
        const auto Evidence = Value->AsObject();
        if (Evidence->GetStringField(TEXT("File")).EndsWith(TEXT(".h")) &&
            !Evidence->GetStringField(TEXT("Condition")).IsEmpty())
        {
            return false;
        }
    }
    return true;
}

static bool IsOwnSource(const FString& Filename)
{
    FString FullFilename = FPaths::ConvertRelativePathToFull(Filename);
    FString SourceDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Source")) + TEXT("/");
    FPaths::NormalizeFilename(FullFilename);
    FPaths::NormalizeFilename(SourceDirectory);
    FPaths::CollapseRelativeDirectories(FullFilename);
    return FullFilename.StartsWith(SourceDirectory, ESearchCase::IgnoreCase);
}

static bool AddInsertions(const TArray<FSourceInsertion>& Insertions, const FString& Module, FNativeApiReview& Review)
{
    TMap<FString, TArray<FSourceInsertion>> ByFile;
    for (const auto& Insertion : Insertions)
    {
        if (!IsOwnSource(Insertion.Filename))
        {
            return false;
        }
        ByFile.FindOrAdd(Insertion.Filename).Add(Insertion);
    }
    TArray<FNativeApiSourcePatch> Patches;
    for (auto& Entry : ByFile)
    {
        FNativeApiSourcePatch Patch;
        Patch.Filename = Entry.Key;
        Patch.Module = Module;
        if (!FFileHelper::LoadFileToArray(Patch.Original, *Patch.Filename))
        {
            return false;
        }
        if (const auto* Existing = Review.SourcePatches.FindByPredicate([&Entry](const auto& Candidate) { return Candidate.Filename == Entry.Key; }))
        {
            if (Existing->Original != Patch.Original)
            {
                return false;
            }
            Patch.Insertions = Existing->Insertions;
        }
        for (const auto& Insertion : Entry.Value)
        {
            FString Text = Insertion.Text;
            if (Patch.Original.Contains(13))
            {
                Text.ReplaceInline(TEXT("\n"), TEXT("\r\n"));
            }
            Patch.Insertions.AddUnique({Insertion.Position, Text});
        }
        Patch.Insertions.Sort([](const auto& First, const auto& Second) { return First.Key > Second.Key; });
        Patch.Updated = Patch.Original;
        for (const auto& Insertion : Patch.Insertions)
        {
            if (Insertion.Key < 0 || Insertion.Key > Patch.Original.Num())
            {
                return false;
            }
            FTCHARToUTF8 Encoded(*Insertion.Value);
            Patch.Updated.Insert(reinterpret_cast<const uint8*>(Encoded.Get()), Encoded.Length(), Insertion.Key);
        }
        Patches.Add(MoveTemp(Patch));
    }
    for (auto& Patch : Patches)
    {
        Review.SourcePatches.RemoveAll([&Patch](const auto& Existing) { return Existing.Filename == Patch.Filename; });
        Review.SourcePatches.Add(MoveTemp(Patch));
    }
    return true;
}

static bool HasExport(const TSharedPtr<FJsonObject>& Type, const TSharedPtr<FJsonObject>& Callable, const FString& OutputModule)
{
    return Type->GetStringField(TEXT("Module")) == OutputModule || Type->GetBoolField(TEXT("Exported")) || Callable->GetBoolField(TEXT("Exported")) ||
        Callable->GetBoolField(TEXT("InlineDefinition"));
}

static bool MatchesEvidence(const TSharedPtr<FJsonObject>& Evidence)
{
    TArray<uint8> Original;
    const FString Filename = Evidence->GetStringField(TEXT("File"));
    const int32 Begin = Evidence->GetIntegerField(TEXT("BeginOffset"));
    const int32 End = Evidence->GetIntegerField(TEXT("EndOffset"));
    FTCHARToUTF8 Expected(*Evidence->GetStringField(TEXT("Text")));
    return FFileHelper::LoadFileToArray(Original, *Filename) && Begin >= 0 && End > Begin && End <= Original.Num() &&
        Expected.Length() == End - Begin && FMemory::Memcmp(Original.GetData() + Begin, Expected.Get(), Expected.Length()) == 0;
}

static bool AddAccessRepair(const TSharedPtr<FJsonObject>& Type, const TArray<TSharedPtr<FJsonObject>>& Callables,
    bool bNeedsClassExport, FNativeApiReview& Review)
{
    const auto Declaration = Type->GetObjectField(TEXT("Declaration"));
    const FString Filename = Declaration->GetStringField(TEXT("File"));
    if (!IsOwnSource(Filename) || Type->GetStringField(TEXT("Module")).IsEmpty() ||
        !Declaration->GetStringField(TEXT("Condition")).IsEmpty() || !MatchesEvidence(Declaration))
    {
        return false;
    }
    const FString Api = Type->GetStringField(TEXT("Module")).ToUpper() + TEXT("_API ");
    TArray<FSourceInsertion> Insertions;
    for (const auto& Callable : Callables)
    {
        TSharedPtr<FJsonObject> Header;
        for (const auto& Value : Callable->GetArrayField(TEXT("Evidence")))
        {
            if (Value->AsObject()->GetStringField(TEXT("File")) == Filename)
            {
                Header = Value->AsObject();
                break;
            }
        }
        if (!Header || !IsUnconditional(Callable) || !MatchesEvidence(Header))
        {
            return false;
        }
        const int32 Begin = Header->GetIntegerField(TEXT("BeginOffset"));
        const int32 End = Header->GetIntegerField(TEXT("EndOffset"));
        FString Prefix;
        if (Callable->GetIntegerField(TEXT("Access")) == 3)
        {
            Prefix = TEXT("protected:\n\t");
            Insertions.Add({Filename, End, TEXT("\nprivate:")});
        }
        if (Type->GetBoolField(TEXT("MinimalApi")) && !HasExport(Type, Callable, Review.OutputModule))
        {
            Prefix += Api;
        }
        if (!Prefix.IsEmpty())
        {
            Insertions.Add({Filename, Begin, Prefix});
        }
    }
    if (bNeedsClassExport)
    {
        const FString DeclarationText = Declaration->GetStringField(TEXT("Text"));
        const FString Name = Type->GetStringField(TEXT("Name"));
        const int32 NamePosition = DeclarationText.Find(Name, ESearchCase::CaseSensitive);
        if (NamePosition == INDEX_NONE || Type->GetBoolField(TEXT("MinimalApi")))
        {
            return false;
        }
        FTCHARToUTF8 BeforeName(*DeclarationText.Left(NamePosition));
        Insertions.Add({Filename, static_cast<int32>(Declaration->GetIntegerField(TEXT("BeginOffset"))) + BeforeName.Length(), Api});
    }
    if (Insertions.IsEmpty() || !AddInsertions(Insertions, Type->GetStringField(TEXT("Module")), Review))
    {
        return false;
    }
    Review.Summary += TEXT("Proposed source changes:\n");
    for (const auto& Insertion : Insertions)
    {
        Review.Summary += FString::Format(TEXT("{0}, byte {1}: insert\n{2}\n"), {Insertion.Filename, Insertion.Position, Insertion.Text});
    }
    return true;
}

static void AnalyzeParentCall(UClass* Base, UFunction* Function, const FString& MethodName, const FString& Context, FNativeApiReview& Review)
{
    for (UClass* Owner = Base; Owner; Owner = Owner->GetSuperClass())
    {
        const auto Type = FNativeApiCatalog::FindType(Owner);
        if (!Type)
        {
            continue;
        }
        TArray<TSharedPtr<FJsonObject>> Methods;
        for (const auto& Value : Type->GetArrayField(TEXT("Callables")))
        {
            const auto Callable = Value->AsObject();
            bool bHasHeaderDeclaration = false;
            for (const auto& Evidence : Callable->GetArrayField(TEXT("Evidence")))
            {
                bHasHeaderDeclaration |= Evidence->AsObject()->GetStringField(TEXT("File")).EndsWith(TEXT(".h"));
            }
            if (Callable->GetStringField(TEXT("Name")) == MethodName && bHasHeaderDeclaration)
            {
                Methods.Add(Callable);
            }
        }
        if (Methods.IsEmpty() && MethodName.EndsWith(TEXT("_Implementation")) && Function->HasAllFunctionFlags(FUNC_Native | FUNC_Event))
        {
            // UHT supplies the native-event implementation declaration when the original header only declares its wrapper.
            for (const auto& Value : Type->GetArrayField(TEXT("Callables")))
            {
                if (Value->AsObject()->GetStringField(TEXT("Name")) == Function->GetName())
                {
                    Methods.Add(Value->AsObject());
                }
            }
        }
        if (Methods.IsEmpty())
        {
            continue;
        }
        if (Methods.Num() != 1 || !IsUnconditional(Methods[0]))
        {
            Review.bReady = false;
            Review.Summary += Context + TEXT(": Super call cannot be resolved unambiguously: ") + MethodName + TEXT("\n");
            return;
        }
        const auto Method = Methods[0];
        const int32 Access = Method->GetIntegerField(TEXT("Access"));
        if ((Access == 1 || Access == 2) && HasExport(Type, Method, Review.OutputModule))
        {
            return;
        }
        Review.bReady = false;
        Review.Summary += Context + TEXT(": required Super call is inaccessible or not exported: ") + MethodName + TEXT("\n");
        const bool bNeedsClassExport = Type->GetStringField(TEXT("Module")) != Review.OutputModule &&
            !Type->GetBoolField(TEXT("Exported")) && !Type->GetBoolField(TEXT("MinimalApi"));
        if (!AddAccessRepair(Type, {Method}, bNeedsClassExport, Review))
        {
            Review.Summary += TEXT("No automatic edit for this Super call.\n");
        }
        return;
    }
}

void FNativeApiPreflight::AnalyzeDefinition(const TSharedPtr<FJsonObject>& Type, const FString& Context, FNativeApiReview& Review)
{
    if (!Type || !Type->GetBoolField(TEXT("ReflectedClass")) || Type->GetObjectField(TEXT("GeneratedBody"))->GetStringField(TEXT("Text")).IsEmpty())
    {
        Review.bReady = false;
        Review.Summary += TEXT("Native class declaration is incomplete: ") + Context + TEXT("\n");
        return;
    }
    const auto Declaration = Type->GetObjectField(TEXT("Declaration"));
    if (!Declaration->GetStringField(TEXT("Condition")).IsEmpty() ||
        Declaration->GetStringField(TEXT("Text")).Contains(TEXT(" final")))
    {
        Review.bReady = false;
        Review.Summary += Context + TEXT(": native inheritance is conditional or the class is final.\n");
        return;
    }
    TArray<TSharedPtr<FJsonObject>> Eligible;
    TArray<TSharedPtr<FJsonObject>> Repairs;
    bool bExplicitConstructor = false;
    for (const auto& Value : Type->GetArrayField(TEXT("Callables")))
    {
        const auto Callable = Value->AsObject();
        if (!Callable->GetBoolField(TEXT("Constructor")))
        {
            continue;
        }
        bExplicitConstructor = true;
        if (Callable->GetBoolField(TEXT("Deleted")) || !IsUnconditional(Callable))
        {
            continue;
        }
        const auto& Parameters = Callable->GetArrayField(TEXT("Parameters"));
        bool bDefaultCallable = true;
        bool bInitializerCallable = false;
        if (!Parameters.IsEmpty())
        {
            FString FirstType = Parameters[0]->AsObject()->GetStringField(TEXT("Type"));
            FirstType.ReplaceInline(TEXT(" "), TEXT(""));
            bInitializerCallable = FirstType == TEXT("constFObjectInitializer&");
            for (int32 ParameterIndex = 0; ParameterIndex < Parameters.Num(); ++ParameterIndex)
            {
                if (Parameters[ParameterIndex]->AsObject()->GetStringField(TEXT("Default")).IsEmpty())
                {
                    bDefaultCallable = false;
                    if (ParameterIndex != 0)
                    {
                        bInitializerCallable = false;
                    }
                }
            }
        }
        if (bDefaultCallable || bInitializerCallable)
        {
            Eligible.Add(Callable);
        }
    }
    bool bConstructorReady = false;
    if (!bExplicitConstructor && Type->GetBoolField(TEXT("CustomConstructor")))
    {
        Review.bReady = false;
        Review.Summary += Context + TEXT(": custom constructor declaration is unavailable. No automatic source edit.\n");
        return;
    }
    for (const auto& Callable : Eligible)
    {
        const int32 Access = Callable->GetIntegerField(TEXT("Access"));
        if ((Access == 1 || Access == 2) && HasExport(Type, Callable, Review.OutputModule))
        {
            bConstructorReady = true;
        }
    }
    if (!Type->GetBoolField(TEXT("CustomConstructor")) &&
        (!bExplicitConstructor || Type->GetBoolField(TEXT("LegacyGeneratedBody"))) &&
        (Type->GetStringField(TEXT("Module")) == Review.OutputModule || Type->GetBoolField(TEXT("MinimalApi")) || Type->GetBoolField(TEXT("Exported"))))
    {
        bConstructorReady = true;
    }
    bool bNeedsClassExport = false;
    if (!bConstructorReady)
    {
        Review.bReady = false;
        Review.Summary += FString::Format(TEXT("{0}: no accessible exported constructor for C++ inheritance. Source: {1}:{2}\n"),
            {Context, Type->GetObjectField(TEXT("Declaration"))->GetStringField(TEXT("File")),
            Type->GetObjectField(TEXT("Declaration"))->GetIntegerField(TEXT("Line"))});
        if (!Eligible.IsEmpty())
        {
            Repairs.Add(Eligible[0]);
        }
        if (Type->GetStringField(TEXT("Module")) != Review.OutputModule &&
            !Type->GetBoolField(TEXT("MinimalApi")) && !Type->GetBoolField(TEXT("Exported")))
        {
            bNeedsClassExport = true;
        }
        if (bExplicitConstructor && Eligible.IsEmpty())
        {
            Review.Summary += TEXT("Constructor signature cannot be repaired automatically.\n");
            return;
        }
    }
    for (const auto& Value : Type->GetArrayField(TEXT("Callables")))
    {
        const auto Callable = Value->AsObject();
        if (!Callable->GetBoolField(TEXT("Destructor")))
        {
            continue;
        }
        if (!IsUnconditional(Callable) || Callable->GetBoolField(TEXT("Deleted")))
        {
            Review.bReady = false;
            Review.Summary += Context + TEXT(": destructor is deleted or its availability cannot be verified.\n");
            continue;
        }
        if (Callable->GetIntegerField(TEXT("Access")) == 3 || !HasExport(Type, Callable, Review.OutputModule))
        {
            Review.bReady = false;
            Repairs.Add(Callable);
            bNeedsClassExport |= Type->GetStringField(TEXT("Module")) != Review.OutputModule &&
                !Type->GetBoolField(TEXT("MinimalApi")) && !Type->GetBoolField(TEXT("Exported"));
            Review.Summary += Context + TEXT(": destructor is inaccessible or not exported.\n");
        }
    }
    if ((!Repairs.IsEmpty() || bNeedsClassExport) && !AddAccessRepair(Type, Repairs, bNeedsClassExport, Review))
    {
        Review.Summary += TEXT("No automatic edit: Engine/third-party source or insufficient declaration evidence. Keep the Blueprint or revise the native API explicitly.\n");
    }
}

FNativeApiReview FNativeApiPreflight::Analyze(const TArray<UObject*>& Targets, bool bFunctionFragment, const FString& OutputModule,
    bool bIncludeProjectClients)
{
    FNativeApiReview Review;
    Review.Identifier = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    Review.OutputModule = OutputModule;
    FNativeApiCatalog::BeginGeneration(true);
    TMap<UClass*, TArray<UBlueprint*>> Bases;
        for (UObject* Target : Targets)
        {
            if (UBlueprint* Blueprint = Cast<UBlueprint>(Target))
            {
                if (Blueprint->BlueprintType == BPTYPE_Interface)
                {
                    continue;
                }
                UClass* Base = Blueprint->ParentClass;
                while (Base && !Base->HasAnyClassFlags(CLASS_Native))
                {
                    Base = Base->GetSuperClass();
                }
                if (Base)
                {
                    Bases.FindOrAdd(Base).Add(Blueprint);
                }
            }
        }
    TSet<UClass*> ProjectClients;
    if (Targets.IsEmpty() || bIncludeProjectClients)
    {
        for (TObjectIterator<UClass> Class; Class; ++Class)
        {
            FString Header;
            if (!Class->HasAnyClassFlags(CLASS_Native) ||
                Class->HasAnyClassFlags(CLASS_Interface | CLASS_Deprecated | CLASS_NewerVersionExists) ||
                !FSourceCodeNavigation::FindClassHeaderPath(*Class, Header) || !IsOwnSource(Header))
            {
                continue;
            }
            bool bBlueprintCallable = false;
            for (TFieldIterator<UFunction> Function(*Class, EFieldIteratorFlags::ExcludeSuper); Function; ++Function)
            {
                if (Function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure))
                {
                    bBlueprintCallable = true;
                    break;
                }
            }
            if (FKismetEditorUtilities::CanCreateBlueprintOfClass(*Class) ||
                UEdGraphSchema_K2::IsAllowableBlueprintVariableType(*Class) || bBlueprintCallable)
            {
                ProjectClients.Add(*Class);
                Bases.FindOrAdd(*Class);
            }
        }
    }
    for (const auto& Entry : Bases)
    {
        const bool bReadyBefore = Review.bReady;
        Review.bReady = true;
        const auto Type = FNativeApiCatalog::FindType(Entry.Key);
        const bool bProjectClient = ProjectClients.Contains(Entry.Key);
        const bool bCheckInheritance = (!bFunctionFragment || bProjectClient) &&
            (!Entry.Value.IsEmpty() || FKismetEditorUtilities::CanCreateBlueprintOfClass(Entry.Key));
        if (bCheckInheritance)
        {
            AnalyzeDefinition(Type, Entry.Key->GetPathName(), Review);
        }
        else if (bProjectClient && (!Type || !Type->GetBoolField(TEXT("ReflectedClass")) ||
            Type->GetObjectField(TEXT("GeneratedBody"))->GetStringField(TEXT("Text")).IsEmpty()))
        {
            Review.bReady = false;
            Review.Summary += TEXT("Native Blueprint client declaration is unavailable: ") + Entry.Key->GetPathName() + TEXT("\n");
        }
        FString Include;
        if ((bCheckInheritance || !Entry.Value.IsEmpty()) &&
            !GEngine->GetEngineSubsystem<UNativizationV2Subsystem>()->GetIncludeFromField(Entry.Key, Include))
        {
            Review.bReady = false;
            Review.Summary += Entry.Key->GetPathName() + TEXT(": native base has no accessible header.\n");
        }
        if (bProjectClient && bCheckInheritance)
        {
            TSet<FString> CheckedMethods;
            for (TFieldIterator<UFunction> Function(Entry.Key); Function; ++Function)
            {
                if (!Function->HasAnyFunctionFlags(FUNC_BlueprintEvent) || Function->HasAnyFunctionFlags(FUNC_Static))
                {
                    continue;
                }
                FString MethodName;
                FFunctionDescriptor NativeEvent;
                if (Function->HasAllFunctionFlags(FUNC_Native | FUNC_Event))
                {
                    MethodName = Function->GetName() + TEXT("_Implementation");
                }
                else if (UBlueprintNativizationSettingsLibrary::FindNativeByClassAndName(
                    Entry.Key, Function->GetName(), NativeEvent))
                {
                    MethodName = NativeEvent.FunctionName;
                }
                if (!MethodName.IsEmpty() && !CheckedMethods.Contains(MethodName))
                {
                    CheckedMethods.Add(MethodName);
                    AnalyzeParentCall(Entry.Key, *Function, MethodName, Entry.Key->GetPathName(), Review);
                }
            }
        }
        for (const auto& Blueprint : Entry.Value)
        {
            TArray<UEdGraph*> Graphs;
            Blueprint->GetAllGraphs(Graphs);
            TSet<FString> CheckedMethods;
            for (UEdGraph* Graph : Graphs)
            {
                for (UEdGraphNode* Node : Graph->Nodes)
                {
                    if (const UK2Node_Event* Event = Cast<UK2Node_Event>(Node))
                    {
                        if (!UBlueprintNativizationLibrary::ShouldGenerateEvent(Event))
                        {
                            continue;
                        }
                    }
                    if (Node->IsA<UK2Node_Event>() || Node->IsA<UK2Node_FunctionEntry>())
                    {
                        const FString EventName = UBlueprintNativizationLibrary::GetFunctionNameByEntryNode(Node);
                        FFunctionDescriptor NativeEvent;
                        UFunction* Function = Blueprint->GeneratedClass->FindFunctionByName(*EventName);
                        if (Function && UBlueprintNativizationSettingsLibrary::FindNativeByClassAndName(
                            Blueprint->GeneratedClass, EventName, NativeEvent) && !CheckedMethods.Contains(NativeEvent.FunctionName))
                        {
                            CheckedMethods.Add(NativeEvent.FunctionName);
                            AnalyzeParentCall(Entry.Key, Function, NativeEvent.FunctionName, Blueprint->GetPathName(), Review);
                        }
                    }
                    if (auto* ParentCall = Cast<UK2Node_CallParentFunction>(Node))
                    {
                        UFunction* Function = ParentCall->GetTargetFunction();
                        if (!Function)
                        {
                            continue;
                        }
                        if (Targets.Contains(Function->GetOwnerClass()->ClassGeneratedBy))
                        {
                            continue;
                        }
                        const FString MethodName = UCallParentTranslatorObject::GetParentMethodName(ParentCall);
                        if (!CheckedMethods.Contains(MethodName))
                        {
                            CheckedMethods.Add(MethodName);
                            AnalyzeParentCall(Entry.Key, Function, MethodName, Blueprint->GetPathName(), Review);
                        }
                    }
                }
            }
            if (!Review.bReady)
            {
                Review.Summary += TEXT("Affected Blueprint: ") + Blueprint->GetPathName() + TEXT("\n");
            }
        }
        Review.bReady &= bReadyBefore;
    }
    FString RebuildReason;
    if (RequiresRebuild(RebuildReason))
    {
        Review.bReady = false;
        Review.SourcePatches.Reset();
        Review.Summary += RebuildReason + TEXT("\n");
    }
    for (const auto& Error : FNativeApiCatalog::GetErrors())
    {
        Review.bReady = false;
        Review.SourcePatches.Reset();
        Review.Summary += Error + TEXT("\n");
    }
    if (Review.bReady)
    {
        Review.Summary = TEXT("Native inheritance API is available for the checked classes. Full C++ compilation has not been checked.");
    }
    if (Targets.IsEmpty() || bIncludeProjectClients)
    {
        FString AuditStatus = TEXT("issues found");
        if (Review.bReady)
        {
            AuditStatus = TEXT("passed");
        }
        Review.Summary = FString::Format(TEXT("Project native Blueprint API: {0} client classes checked; {1}.\n"),
            {ProjectClients.Num(), AuditStatus}) + Review.Summary;
    }
    return Review;
}

bool FNativeApiPreflight::UsesObjectInitializer(UClass* Base)
{
    if (!Base)
    {
        return true;
    }
    const auto Type = FNativeApiCatalog::FindType(Base);
    if (!Type)
    {
        return false;
    }
    bool bExplicitConstructor = false;
    for (const auto& Value : Type->GetArrayField(TEXT("Callables")))
    {
        const auto Callable = Value->AsObject();
        if (!Callable->GetBoolField(TEXT("Constructor")))
        {
            continue;
        }
        bExplicitConstructor = true;
        if (Callable->GetBoolField(TEXT("Deleted")) || !IsUnconditional(Callable) ||
            (Callable->GetIntegerField(TEXT("Access")) != 1 && Callable->GetIntegerField(TEXT("Access")) != 2))
        {
            continue;
        }
        const auto& Parameters = Callable->GetArrayField(TEXT("Parameters"));
        if (Parameters.IsEmpty())
        {
            continue;
        }
        FString FirstType = Parameters[0]->AsObject()->GetStringField(TEXT("Type"));
        FirstType.ReplaceInline(TEXT(" "), TEXT(""));
        bool bInitializerCallable = FirstType == TEXT("constFObjectInitializer&");
        for (int32 ParameterIndex = 1; ParameterIndex < Parameters.Num(); ++ParameterIndex)
        {
            bInitializerCallable &= !Parameters[ParameterIndex]->AsObject()->GetStringField(TEXT("Default")).IsEmpty();
        }
        if (bInitializerCallable)
        {
            return true;
        }
    }
    if (Type->GetBoolField(TEXT("LegacyGeneratedBody")) && !Type->GetBoolField(TEXT("CustomConstructor")))
    {
        return true;
    }
    if (!bExplicitConstructor && !Type->GetBoolField(TEXT("CustomConstructor")))
    {
        return UsesObjectInitializer(Base->GetSuperClass());
    }
    return false;
}

bool FNativeApiPreflight::RequiresRebuild(FString& Reason)
{
    int32 RepairProcess = 0;
    GConfig->GetInt(TEXT("BlueprintNativizationSourceRepair"), TEXT("Process"), RepairProcess, GEditorPerProjectIni);
    if (RepairProcess != 0 && RepairProcess == static_cast<int32>(FPlatformProcess::GetCurrentProcessId()))
    {
        Reason = TEXT("Native source changed. Rebuild the project, restart the editor and rebuild the Project API cache before exporting.");
        return true;
    }
    bool bPending = false;
    GConfig->GetBool(TEXT("BlueprintNativizationSourceRepair"), TEXT("Pending"), bPending, GEditorPerProjectIni);
    if (bPending)
    {
        FString TimestampText;
        FString ModuleNames;
        GConfig->GetString(TEXT("BlueprintNativizationSourceRepair"), TEXT("Timestamp"), TimestampText, GEditorPerProjectIni);
        GConfig->GetString(TEXT("BlueprintNativizationSourceRepair"), TEXT("Modules"), ModuleNames, GEditorPerProjectIni);
        FDateTime Timestamp;
        TArray<FString> Modules;
        ModuleNames.ParseIntoArray(Modules, TEXT(","), true);
        const FString IndexFile = FNativeModuleCache::GetIndexFile(false);
        TSharedPtr<FJsonObject> Index;
        FString Error;
        Reason = TEXT("Source repair requires rebuilt native modules, editor restart and a current Project API cache.");
        if (!FDateTime::ParseIso8601(*TimestampText, Timestamp) || Modules.IsEmpty() ||
            !FNativeModuleCache::ReadIndex(IndexFile, Index, Error) || IFileManager::Get().GetTimeStamp(*IndexFile) < Timestamp)
        {
            return true;
        }
        for (const FString& Module : Modules)
        {
            const FString Binary = FModuleManager::Get().GetModuleFilename(*Module);
            const TSharedPtr<FJsonObject>* Entry = nullptr;
            TSharedPtr<FJsonObject> Cache;
            if (!FPaths::FileExists(Binary) || IFileManager::Get().GetTimeStamp(*Binary) < Timestamp ||
                !Index->GetObjectField(TEXT("Modules"))->TryGetObjectField(Module, Entry) ||
                !UBlueprintNativeApiCacheLibrary::ReadValidatedSourceCache((*Entry)->GetStringField(TEXT("File")), Cache, Error))
            {
                return true;
            }
        }
        GConfig->SetBool(TEXT("BlueprintNativizationSourceRepair"), TEXT("Pending"), false, GEditorPerProjectIni);
        GConfig->SetInt(TEXT("BlueprintNativizationSourceRepair"), TEXT("Process"), 0, GEditorPerProjectIni);
        GConfig->Flush(false, GEditorPerProjectIni);
    }
    return false;
}

bool FNativeApiPreflight::Apply(const FNativeApiReview& Review, FString& Report)
{
    FGuid Identifier;
    if (Review.SourcePatches.IsEmpty() || !FGuid::ParseExact(Review.Identifier, EGuidFormats::Digits, Identifier))
    {
        Report = TEXT("No automatic project source repairs are available. Analyze native API first.");
        return false;
    }
    TMap<FString, FNativeApiSourcePatch> Combined;
    TArray<FString> RepairedModules;
    for (const auto& Patch : Review.SourcePatches)
    {
        if (!IsOwnSource(Patch.Filename) || Combined.Contains(Patch.Filename) || Patch.Module.IsEmpty())
        {
            Report = TEXT("Source changes require a new combined review: ") + Patch.Filename;
            return false;
        }
        TArray<uint8> Current;
        if (!FFileHelper::LoadFileToArray(Current, *Patch.Filename) || Current != Patch.Original)
        {
            Report = TEXT("Source changed after review. Analyze again: ") + Patch.Filename;
            return false;
        }
        Combined.Add(Patch.Filename, Patch);
        RepairedModules.AddUnique(Patch.Module);
    }
    const FString BackupDirectory = FPaths::ProjectSavedDir() / TEXT("BlueprintNativization/SourceRepairs") / Review.Identifier;
    TArray<FString> Written;
    for (const auto& Entry : Combined)
    {
        FString Relative = FPaths::ConvertRelativePathToFull(Entry.Key);
        const FString ProjectDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
        FPaths::MakePathRelativeTo(Relative, *ProjectDirectory);
        const FString Backup = BackupDirectory / Relative;
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup), true);
        if (FPaths::FileExists(Backup) || !FFileHelper::SaveArrayToFile(Entry.Value.Original, *Backup))
        {
            Report = TEXT("Cannot create source backup: ") + Backup;
            return false;
        }
    }
    for (const auto& Entry : Combined)
    {
        const FString Temporary = Entry.Key + TEXT(".") + Review.Identifier + TEXT(".tmp");
        Written.Add(Entry.Key);
        if (!FFileHelper::SaveArrayToFile(Entry.Value.Updated, *Temporary) ||
            !IFileManager::Get().Move(*Entry.Key, *Temporary, true, false))
        {
            IFileManager::Get().Delete(*Temporary);
            TArray<FString> RestoreFailures;
            for (const auto& Filename : Written)
            {
                if (!FFileHelper::SaveArrayToFile(Combined[Filename].Original, *Filename))
                {
                    RestoreFailures.Add(Filename);
                }
            }
            Report = TEXT("Source repair failed. Backup: ") + BackupDirectory;
            if (!RestoreFailures.IsEmpty())
            {
                Report += TEXT("\nRestore these files from the backup: ") + FString::Join(RestoreFailures, TEXT(", "));
            }
            else
            {
                Report += TEXT("\nPreviously written files were restored.");
            }
            return false;
        }
    }
    GConfig->SetInt(TEXT("BlueprintNativizationSourceRepair"), TEXT("Process"), static_cast<int32>(FPlatformProcess::GetCurrentProcessId()), GEditorPerProjectIni);
    GConfig->SetBool(TEXT("BlueprintNativizationSourceRepair"), TEXT("Pending"), true, GEditorPerProjectIni);
    GConfig->SetString(TEXT("BlueprintNativizationSourceRepair"), TEXT("Timestamp"), *FDateTime::UtcNow().ToIso8601(), GEditorPerProjectIni);
    GConfig->SetString(TEXT("BlueprintNativizationSourceRepair"), TEXT("Modules"), *FString::Join(RepairedModules, TEXT(",")), GEditorPerProjectIni);
    GConfig->Flush(false, GEditorPerProjectIni);
    Report = TEXT("Project source API repaired. Backup: ") + BackupDirectory +
        TEXT("\nRebuild the project, restart the editor and rebuild the Project API cache. Export remains blocked in this editor session.");
    return true;
}
