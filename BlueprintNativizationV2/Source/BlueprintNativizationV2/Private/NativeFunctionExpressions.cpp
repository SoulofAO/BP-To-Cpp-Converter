#include "NativeFunctionExpressions.h"
#include "NativeApiCatalog.h"
#include "BlueprintNativizationLibrary.h"
#include "Internationalization/Regex.h"
#include "UObject/UnrealType.h"
#include "K2Node_CallFunction.h"
#include "Engine/Blueprint.h"
#include "NativeGenerationContext.h"
#include "Misc/ScopeExit.h"
#include "GeneratedCodeCleanup.h"
#include "TranslatorObjects/StructTranslatorObject.h"

static bool UsesClassOnlyAsFilter(const TSharedPtr<FJsonObject>& Method, const FString& ParameterName,
    const TArray<TSharedPtr<FJsonValue>>& Methods, TSet<FString>& Visiting, bool& bFilterEvidence)
{
    const FString Identity = Method->GetStringField(TEXT("Signature")) + TEXT(":") + ParameterName;
    if (Visiting.Contains(Identity))
    {
        return true;
    }
    Visiting.Add(Identity);
    ON_SCOPE_EXIT
    {
        Visiting.Remove(Identity);
    };
    FString Remaining = Method->GetStringField(TEXT("Body"));
    if (Remaining.IsEmpty())
    {
        return false;
    }
    TArray<FString> Names = {ParameterName};
    for (int32 NameIndex = 0; NameIndex < Names.Num(); ++NameIndex)
    {
        const FString Name = Names[NameIndex];
        const FRegexPattern AliasPattern(TEXT("(?:const\\s+)?UClass\\s*\\*\\s*(\\w+)\\s*=\\s*\\b") + Name +
            TEXT("\\b(?:\\s*\\.\\s*Get\\s*\\(\\s*\\))?(?=\\s*[;)])"));
        FRegexMatcher Alias(AliasPattern, Remaining);
        while (Alias.FindNext())
        {
            Names.AddUnique(Alias.GetCaptureGroup(1));
            Remaining.RemoveAt(Alias.GetMatchBeginning(), Alias.GetMatchEnding() - Alias.GetMatchBeginning());
            Alias = FRegexMatcher(AliasPattern, Remaining);
        }
        const FRegexPattern GuardPattern(TEXT("\\bif\\s*\\(\\s*!?\\s*") + Name + TEXT("\\s*\\)"));
        FRegexMatcher Guard(GuardPattern, Remaining);
        while (Guard.FindNext())
        {
            Remaining.RemoveAt(Guard.GetMatchBeginning(), Guard.GetMatchEnding() - Guard.GetMatchBeginning());
            Guard = FRegexMatcher(GuardPattern, Remaining);
        }
        const FRegexPattern FilterPattern(TEXT("(?:->|\\.)\\s*(?:IsA|IsChildOf)\\s*\\(\\s*") + Name +
            TEXT("\\s*(?:\\.\\s*Get\\s*\\(\\s*\\))?\\s*\\)"));
        FRegexMatcher Filter(FilterPattern, Remaining);
        while (Filter.FindNext())
        {
            bFilterEvidence = true;
            Remaining.RemoveAt(Filter.GetMatchBeginning(), Filter.GetMatchEnding() - Filter.GetMatchBeginning());
            Filter = FRegexMatcher(FilterPattern, Remaining);
        }
        const FRegexPattern ClassComparisonPattern(TEXT("\\b") + Name +
            TEXT("\\s*(?:==|!=)\\s*\\w+::StaticClass\\s*\\(\\s*\\)"));
        FRegexMatcher ClassComparison(ClassComparisonPattern, Remaining);
        while (ClassComparison.FindNext())
        {
            bFilterEvidence = true;
            Remaining.RemoveAt(ClassComparison.GetMatchBeginning(), ClassComparison.GetMatchEnding() - ClassComparison.GetMatchBeginning());
            ClassComparison = FRegexMatcher(ClassComparisonPattern, Remaining);
        }
        const FRegexPattern ClassReceiverPattern(TEXT("\\b") + Name +
            TEXT("\\s*->\\s*IsChildOf\\s*\\(\\s*\\w+::StaticClass\\s*\\(\\s*\\)\\s*\\)"));
        FRegexMatcher ClassReceiver(ClassReceiverPattern, Remaining);
        while (ClassReceiver.FindNext())
        {
            bFilterEvidence = true;
            Remaining.RemoveAt(ClassReceiver.GetMatchBeginning(), ClassReceiver.GetMatchEnding() - ClassReceiver.GetMatchBeginning());
            ClassReceiver = FRegexMatcher(ClassReceiverPattern, Remaining);
        }
        const FRegexPattern ForwardPattern(TEXT("\\b([A-Za-z_]\\w*)\\s*(?:<[^;{}()]+>)?\\s*\\(\\s*") + Name +
            TEXT("\\s*(?:\\.\\s*Get\\s*\\(\\s*\\))?\\s*(?=[,)])"));
        FRegexMatcher Forward(ForwardPattern, Remaining);
        while (Forward.FindNext())
        {
            const int32 Opening = Remaining.Find(TEXT("("), ESearchCase::CaseSensitive, ESearchDir::FromStart, Forward.GetMatchBeginning());
            int32 ParenthesisDepth = 1;
            int32 BraceDepth = 0;
            int32 ArgumentCount = 1;
            for (int32 CharacterIndex = Opening + 1; CharacterIndex < Remaining.Len() && ParenthesisDepth > 0; ++CharacterIndex)
            {
                const TCHAR Character = Remaining[CharacterIndex];
                if (Character == TEXT('('))
                {
                    ++ParenthesisDepth;
                }
                else if (Character == TEXT(')'))
                {
                    --ParenthesisDepth;
                }
                else if (Character == TEXT('{'))
                {
                    ++BraceDepth;
                }
                else if (Character == TEXT('}'))
                {
                    --BraceDepth;
                }
                else if (Character == TEXT(',') && ParenthesisDepth == 1 && BraceDepth == 0)
                {
                    ++ArgumentCount;
                }
            }
            if (ParenthesisDepth != 0)
            {
                return false;
            }
            TSharedPtr<FJsonObject> Target;
            for (const TSharedPtr<FJsonValue>& CandidateValue : Methods)
            {
                const TSharedPtr<FJsonObject> Candidate = CandidateValue->AsObject();
                const TArray<TSharedPtr<FJsonValue>>& Parameters = Candidate->GetArrayField(TEXT("Parameters"));
                if (Candidate->GetStringField(TEXT("Name")) != Forward.GetCaptureGroup(1) || Parameters.Num() < ArgumentCount)
                {
                    continue;
                }
                bool bDefaultsAvailable = true;
                for (int32 ParameterIndex = ArgumentCount; ParameterIndex < Parameters.Num(); ++ParameterIndex)
                {
                    if (Parameters[ParameterIndex]->AsObject()->GetStringField(TEXT("Default")).IsEmpty())
                    {
                        bDefaultsAvailable = false;
                    }
                }
                if (!bDefaultsAvailable)
                {
                    continue;
                }
                const FString ParameterType = Parameters[0]->AsObject()->GetStringField(TEXT("Type")).Replace(TEXT(" "), TEXT(""));
                if (!ParameterType.Contains(TEXT("TSubclassOf<")) && !ParameterType.Contains(TEXT("UClass*")))
                {
                    continue;
                }
                if (Target)
                {
                    return false;
                }
                Target = Candidate;
            }
            if (!Target || !UsesClassOnlyAsFilter(Target,
                Target->GetArrayField(TEXT("Parameters"))[0]->AsObject()->GetStringField(TEXT("Name")), Methods, Visiting, bFilterEvidence))
            {
                return false;
            }
            Remaining.RemoveAt(Forward.GetMatchBeginning(), Forward.GetMatchEnding() - Forward.GetMatchBeginning());
            Forward = FRegexMatcher(ForwardPattern, Remaining);
        }
        FRegexMatcher UnknownUse(FRegexPattern(TEXT("\\b") + Name + TEXT("\\b")), Remaining);
        if (UnknownUse.FindNext())
        {
            return false;
        }
    }
    return true;
}

bool FNativeFunctionExpressions::IsClassFilterParameter(UFunction* Function, FName ParameterName)
{
    if (!Function || !Function->HasAnyFunctionFlags(FUNC_Native) ||
        !CastField<FClassProperty>(FindFProperty<FProperty>(Function, ParameterName)))
    {
        return false;
    }
    const TSharedPtr<FJsonObject> Type = FNativeApiCatalog::FindType(Function->GetOwnerClass());
    const TSharedPtr<FJsonObject> Method = FNativeApiCatalog::FindCallable(Function);
    if (!Type || !Method)
    {
        return false;
    }
    TSet<FString> Visiting;
    bool bFilterEvidence = false;
    return UsesClassOnlyAsFilter(Method, ParameterName.ToString(), Type->GetArrayField(TEXT("Callables")), Visiting, bFilterEvidence) && bFilterEvidence;
}

FString FNativeFunctionExpressions::BehaviorTree(UFunction* Function, const FString& Arguments, UK2Node_CallFunction* Call)
{
    if (!Call || Function->GetOwnerClass()->GetName() != TEXT("BTFunctionLibrary") ||
        !Function->HasAnyFunctionFlags(FUNC_Static))
    {
        return FString();
    }
    const auto Type = FNativeApiCatalog::FindType(Function->GetOwnerClass());
    if (!Type)
    {
        return FString();
    }
    TMap<FString, TSharedPtr<FJsonObject>> Methods;
    for (const auto& Value : Type->GetArrayField(TEXT("Callables")))
    {
        const auto Method = Value->AsObject();
        if (Methods.Contains(Method->GetStringField(TEXT("Name"))))
        {
            FNativeApiCatalog::AddError(TEXT("Ambiguous behavior tree library method in the source cache."));
            return FString();
        }
        Methods.Add(Method->GetStringField(TEXT("Name")), Method);
    }
    const auto* OwnerMethod = Methods.Find(TEXT("GetOwnerComponent"));
    if (!OwnerMethod || !(*OwnerMethod)->GetStringField(TEXT("Body")).Contains(TEXT("CLASS_CompiledFromBlueprint")) ||
        !(*OwnerMethod)->GetStringField(TEXT("Body")).Contains(TEXT("Cast<UBehaviorTreeComponent>(NodeOwner->GetOuter())")))
    {
        FNativeApiCatalog::AddError(TEXT("The native behavior tree ownership contract is not supported by the validated source cache."));
        return FString();
    }
    FStructTranslationResult Requirements;
    Requirements.HeaderIncludes = {TEXT("BehaviorTree/BehaviorTreeComponent.h"), TEXT("BehaviorTree/BlackboardComponent.h"),
        TEXT("BehaviorTree/BTNode.h"), TEXT("BehaviorTree/BehaviorTreeTypes.h"), TEXT("Misc/RuntimeErrors.h"), TEXT("GameFramework/Actor.h")};
    Requirements.ModuleDependencies.Add(TEXT("AIModule"));
    const FString HelperName = UBlueprintNativizationLibrary::GetUniqueFieldName(Call->GetBlueprint()->GeneratedClass) +
        TEXT("BehaviorTree") + Function->GetName();
    FString Declaration = TEXT("struct ") + HelperName + TEXT("\n{\n");
    TSet<FString> Visited;
    TArray<FString> Pending = {Function->GetName()};
    while (!Pending.IsEmpty())
    {
        const FString Name = Pending.Pop(EAllowShrinking::No);
        if (Visited.Contains(Name))
        {
            continue;
        }
        Visited.Add(Name);
        const auto* CachedMethod = Methods.Find(Name);
        if (!CachedMethod || (*CachedMethod)->GetStringField(TEXT("Body")).IsEmpty())
        {
            FNativeApiCatalog::AddError(TEXT("Missing behavior tree library implementation in the source cache: ") + Name);
            return FString();
        }
        const auto Method = *CachedMethod;
        FString Body = Method->GetStringField(TEXT("Body"));
        if (Name == TEXT("GetOwnerComponent"))
        {
            Body = TEXT("{\nUBehaviorTreeComponent* OwnerComponent{};\nif (NodeOwner)\n{\nOwnerComponent = Cast<UBehaviorTreeComponent>(NodeOwner->GetOuter());\n}\nreturn OwnerComponent;\n}");
        }
        else
        {
            for (const auto& Pair : Methods)
            {
                FRegexMatcher Dependency(FRegexPattern(TEXT("\\b") + Pair.Key + TEXT("\\s*\\(")), Body);
                if (Dependency.FindNext())
                {
                    Pending.Add(Pair.Key);
                }
            }
        }
        FRegexMatcher KeyTypes(FRegexPattern(TEXT("\\bUBlackboardKeyType_\\w+\\b")), Body);
        while (KeyTypes.FindNext())
        {
            const auto Dependency = FNativeApiCatalog::FindPublicDeclaration(KeyTypes.GetCaptureGroup(0));
            if (!Dependency)
            {
                FNativeApiCatalog::AddError(TEXT("Missing public blackboard key type in the source cache."));
                return FString();
            }
            Requirements.HeaderIncludes.AddUnique(Dependency->GetStringField(TEXT("Include")));
        }
        FRegexMatcher ConditionalReturn(FRegexPattern(TEXT("return\\s+([^;?]+)\\?\\s*([^;]+?)\\s*:\\s*([^;]+);")), Body);
        if (ConditionalReturn.FindNext())
        {
            Body = Body.Left(ConditionalReturn.GetMatchBeginning()) + TEXT("if (") + ConditionalReturn.GetCaptureGroup(1) +
                TEXT(")\n{\nreturn ") + ConditionalReturn.GetCaptureGroup(2) + TEXT(";\n}\nreturn ") +
                ConditionalReturn.GetCaptureGroup(3) + TEXT(";") + Body.Mid(ConditionalReturn.GetMatchEnding());
        }
        TArray<FString> Parameters;
        for (const auto& ParameterValue : Method->GetArrayField(TEXT("Parameters")))
        {
            const auto Parameter = ParameterValue->AsObject();
            Parameters.Add(Parameter->GetStringField(TEXT("Type")) + TEXT(" ") + Parameter->GetStringField(TEXT("Name")));
        }
        int32 IgnoredCalls = 0;
        Body = FGeneratedCodeCleanup::RewriteNativeEventBody(Body, {}, IgnoredCalls);
        Declaration += TEXT("static ") + Method->GetStringField(TEXT("ReturnType")) + TEXT(" ") + Name +
            TEXT("(") + FString::Join(Parameters, TEXT(", ")) + TEXT(")\n") + Body + TEXT("\n");
    }
    Declaration += TEXT("};");
    Requirements.ForwardDeclarations.Add(Declaration);
    FNativeGenerationContext::AddRequirements(Requirements);
    return HelperName + TEXT("::") + Function->GetName() + TEXT("(") + Arguments + TEXT(")");
}

FString FNativeFunctionExpressions::Arithmetic(UFunction* Function, const TArray<FString>& Arguments, const TArray<FString>& ArgumentTypes)
{
    if (!Function->HasAllFunctionFlags(FUNC_Static | FUNC_BlueprintPure) || !Function->GetReturnProperty())
    {
        return FString();
    }
    const auto Callable = FNativeApiCatalog::FindCallable(Function);
    if (!Callable)
    {
        return FString();
    }
    FString Body = Callable->GetStringField(TEXT("Body")).TrimStartAndEnd();
    if (!Body.RemoveFromStart(TEXT("{")) || !Body.RemoveFromEnd(TEXT("}")))
    {
        return FString();
    }
    Body.TrimStartAndEndInline();
    if (!Body.RemoveFromStart(TEXT("return ")) || !Body.RemoveFromEnd(TEXT(";")) || Body.Contains(TEXT(";")))
    {
        return FString();
    }
    TMap<FString, FString> Values;
    int32 ArgumentIndex = 0;
    bool bBuiltinOperands = true;
    for (TFieldIterator<FProperty> Parameter(Function); Parameter; ++Parameter)
    {
        if (!Parameter->HasAnyPropertyFlags(CPF_Parm) || Parameter->HasAnyPropertyFlags(CPF_ReturnParm))
        {
            continue;
        }
        if (ArgumentIndex >= Arguments.Num() ||
            (Parameter->HasAnyPropertyFlags(CPF_OutParm) && !Parameter->HasAnyPropertyFlags(CPF_ConstParm)) ||
            (!Parameter->IsA<FNumericProperty>() && !Parameter->IsA<FBoolProperty>() && !Parameter->IsA<FStructProperty>()))
        {
            return FString();
        }
        const FString ParameterType = UBlueprintNativizationLibrary::GetPropertyType(*Parameter);
        FString Argument = Arguments[ArgumentIndex];
        if (!ArgumentTypes.IsValidIndex(ArgumentIndex) || ArgumentTypes[ArgumentIndex] != ParameterType)
        {
            Argument = ParameterType + TEXT("(") + Argument + TEXT(")");
        }
        else
        {
            Argument = TEXT("(") + Argument + TEXT(")");
        }
        Values.Add(Parameter->GetName(), Argument);
        bBuiltinOperands &= !Parameter->IsA<FStructProperty>();
        ++ArgumentIndex;
    }
    if (ArgumentIndex != Arguments.Num())
    {
        return FString();
    }
    const FRegexPattern BinaryPattern(TEXT("^\\s*(\\w+)\\s*(==|!=|<=|>=|[+*<>-])\\s*(\\w+)\\s*$"));
    const FString ReturnType = UBlueprintNativizationLibrary::GetPropertyType(Function->GetReturnProperty());
    FRegexMatcher Binary(BinaryPattern, Body);
    if (Binary.FindNext() && Values.Contains(Binary.GetCaptureGroup(1)) && Values.Contains(Binary.GetCaptureGroup(3)) &&
        Binary.GetCaptureGroup(1) != Binary.GetCaptureGroup(3) && Values.Num() == 2)
    {
        FString ResultType = ReturnType;
        if (bBuiltinOperands && ReturnType == TEXT("bool"))
        {
            ResultType.Reset();
        }
        return ResultType + TEXT("(") + Values[Binary.GetCaptureGroup(1)] + TEXT(" ") + Binary.GetCaptureGroup(2) + TEXT(" ") +
            Values[Binary.GetCaptureGroup(3)] + TEXT(")");
    }
    const FRegexPattern UnaryPattern(TEXT("^\\s*([!-])\\s*(\\w+)\\s*$"));
    FRegexMatcher Unary(UnaryPattern, Body);
    if (Unary.FindNext() && Values.Num() == 1 && Values.Contains(Unary.GetCaptureGroup(2)))
    {
        return ReturnType + TEXT("(") + Unary.GetCaptureGroup(1) + Values[Unary.GetCaptureGroup(2)] + TEXT(")");
    }
    if (Function->GetReturnProperty()->IsA<FStructProperty>() && Values.Num() == 1)
    {
        const FRegexPattern ConstructorPattern(TEXT("^\\s*") + ReturnType + TEXT("\\s*\\(\\s*(\\w+)\\s*\\)\\s*$"));
        FRegexMatcher Constructor(ConstructorPattern, Body);
        if (Constructor.FindNext() && Values.Contains(Constructor.GetCaptureGroup(1)))
        {
            return ReturnType + TEXT("(") + Values[Constructor.GetCaptureGroup(1)] + TEXT(")");
        }
    }
    return FString();
}

bool FNativeFunctionExpressions::Projection(UFunction* Function, FString& Input, TArray<TPair<FString, FString>>& Outputs)
{
    Input.Reset();
    Outputs.Reset();
    if (!Function || !Function->HasAllFunctionFlags(FUNC_Static | FUNC_BlueprintPure) || Function->GetReturnProperty() ||
        !Function->HasMetaData(TEXT("NativeBreakFunc")))
    {
        return false;
    }
    const auto Callable = FNativeApiCatalog::FindCallable(Function);
    if (!Callable)
    {
        return false;
    }
    FStructProperty* SourceProperty = nullptr;
    TSet<FString> OutputNames;
    for (TFieldIterator<FProperty> Parameter(Function); Parameter; ++Parameter)
    {
        if (!Parameter->HasAnyPropertyFlags(CPF_Parm))
        {
            continue;
        }
        if (Parameter->HasAnyPropertyFlags(CPF_OutParm) && !Parameter->HasAnyPropertyFlags(CPF_ConstParm))
        {
            OutputNames.Add(Parameter->GetName());
        }
        else if (!SourceProperty)
        {
            SourceProperty = CastField<FStructProperty>(*Parameter);
            Input = Parameter->GetName();
            if (!SourceProperty || !Parameter->HasAnyPropertyFlags(CPF_ConstParm))
            {
                return false;
            }
        }
        else
        {
            return false;
        }
    }
    if (!SourceProperty || OutputNames.IsEmpty())
    {
        return false;
    }
    const auto Structure = FNativeApiCatalog::FindType(SourceProperty->Struct);
    if (!Structure)
    {
        return false;
    }
    FString Body = Callable->GetStringField(TEXT("Body")).TrimStartAndEnd();
    if (!Body.RemoveFromStart(TEXT("{")) || !Body.RemoveFromEnd(TEXT("}")))
    {
        return false;
    }
    TArray<FString> Statements;
    Body.ParseIntoArray(Statements, TEXT(";"), true);
    const FRegexPattern AssignmentPattern(TEXT("^(\\w+)\\s*=\\s*") + Input + TEXT("\\.((\\w+)(?:\\(\\))?(?:\\.\\w+(?:\\(\\))?)*)$"));
    for (FString Statement : Statements)
    {
        Statement.TrimStartAndEndInline();
        if (Statement.IsEmpty() || Statement.StartsWith(TEXT("SCOPE_CYCLE_COUNTER(")))
        {
            continue;
        }
        FRegexMatcher Assignment(AssignmentPattern, Statement);
        if (!Assignment.FindNext() || !OutputNames.Remove(Assignment.GetCaptureGroup(1)))
        {
            return false;
        }
        const FString MemberName = Assignment.GetCaptureGroup(3);
        bool bAccessible = false;
        for (const auto& MemberValue : Structure->GetArrayField(TEXT("Members")))
        {
            const auto Member = MemberValue->AsObject();
            bAccessible |= Member->GetStringField(TEXT("Name")) == MemberName && Member->GetIntegerField(TEXT("Access")) == 1;
        }
        for (const auto& MethodValue : Structure->GetArrayField(TEXT("Callables")))
        {
            const auto Method = MethodValue->AsObject();
            bAccessible |= Method->GetStringField(TEXT("Name")) == MemberName && Method->GetIntegerField(TEXT("Access")) == 1 &&
                Method->GetBoolField(TEXT("Const")) && Method->GetBoolField(TEXT("AccessVerified")) &&
                Method->GetArrayField(TEXT("Parameters")).IsEmpty();
        }
        if (!bAccessible)
        {
            return false;
        }
        Outputs.Emplace(Assignment.GetCaptureGroup(1), Assignment.GetCaptureGroup(2));
    }
    return OutputNames.IsEmpty();
}
