/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#include "GeneratedCodeCleanup.h"

struct FGeneratedToken
{
    FString Text;
    int32 Start = 0;
    int32 End = 0;
};

static TArray<FGeneratedToken> TokenizeGeneratedCode(const FString& Content)
{
    TArray<FGeneratedToken> Tokens;
    for (int32 Position = 0; Position < Content.Len();)
    {
        const TCHAR Character = Content[Position];
        if (FChar::IsWhitespace(Character))
        {
            ++Position;
            continue;
        }
        const int32 Start = Position++;
        if (Character == TEXT('"') || Character == TEXT('\''))
        {
            while (Position < Content.Len())
            {
                const TCHAR QuotedCharacter = Content[Position++];
                if (QuotedCharacter == TEXT('\\') && Position < Content.Len())
                {
                    ++Position;
                }
                else if (QuotedCharacter == Character)
                {
                    break;
                }
            }
        }
        else if (Character == TEXT('/') && Position < Content.Len() && Content[Position] == TEXT('/'))
        {
            while (Position < Content.Len() && Content[Position] != TEXT('\n'))
            {
                ++Position;
            }
            continue;
        }
        else if (Character == TEXT('/') && Position < Content.Len() && Content[Position] == TEXT('*'))
        {
            ++Position;
            while (Position + 1 < Content.Len() && !(Content[Position] == TEXT('*') && Content[Position + 1] == TEXT('/')))
            {
                ++Position;
            }
            Position = FMath::Min(Position + 2, Content.Len());
            continue;
        }
        else if (FChar::IsAlnum(Character) || Character == TEXT('_'))
        {
            while (Position < Content.Len() && (FChar::IsAlnum(Content[Position]) || Content[Position] == TEXT('_')))
            {
                ++Position;
            }
        }
        Tokens.Add({Content.Mid(Start, Position - Start), Start, Position});
    }
    return Tokens;
}

FString FGeneratedCodeCleanup::RewriteNativeEventBody(const FString& Body,
    const TMap<FString, FString>& Implementations, int32& ReplacedCalls, const FString& GeneratedClass,
    const FString& OriginalParentClass)
{
    FString Result;
    ReplacedCalls = 0;
    const TArray<FGeneratedToken> Tokens = TokenizeGeneratedCode(Body);
    TSet<FString> ThisAliases = {TEXT("this")};
    TMap<int32, FString> AliasDeclarations;
    if (!GeneratedClass.IsEmpty())
    {
        for (int32 Position = 0; Position + 10 < Tokens.Num(); ++Position)
        {
            if (Tokens[Position + 1].Text == TEXT("*") && Tokens[Position + 3].Text == TEXT("=") &&
                Tokens[Position + 4].Text == TEXT("(") && Tokens[Position + 5].Text == Tokens[Position].Text &&
                Tokens[Position + 6].Text == TEXT("*") && Tokens[Position + 7].Text == TEXT(")") &&
                Tokens[Position + 8].Text == TEXT("this") && Tokens[Position + 9].Text == TEXT(";"))
            {
                const FString Alias = Tokens[Position + 2].Text;
                bool bCallsEvent = false;
                for (int32 CallPosition = Position + 10; CallPosition + 4 < Tokens.Num(); ++CallPosition)
                {
                    bCallsEvent |= Tokens[CallPosition].Text == Alias && Tokens[CallPosition + 1].Text == TEXT("-") &&
                        Tokens[CallPosition + 2].Text == TEXT(">") && Implementations.Contains(Tokens[CallPosition + 3].Text) &&
                        Tokens[CallPosition + 4].Text == TEXT("(");
                }
                if (bCallsEvent)
                {
                    ThisAliases.Add(Alias);
                    AliasDeclarations.Add(Position, GeneratedClass + TEXT("* ") + Alias + TEXT(" = const_cast<") +
                        GeneratedClass + TEXT("*>(this);"));
                }
            }
        }
    }
    int32 PreviousEnd = 0;
    for (int32 TokenIndex = 0; TokenIndex < Tokens.Num(); ++TokenIndex)
    {
        const FGeneratedToken& Token = Tokens[TokenIndex];
        const FString Gap = Body.Mid(PreviousEnd, Token.Start - PreviousEnd);
        if (Gap.Contains(TEXT("//")) || Gap.Contains(TEXT("/*")))
        {
            Result += TEXT("\n");
        }
        else
        {
            Result += Gap;
        }
        if (const FString* Declaration = AliasDeclarations.Find(TokenIndex))
        {
            Result += *Declaration;
            TokenIndex += 9;
            PreviousEnd = Tokens[TokenIndex].End;
            continue;
        }
        const FString* Implementation = Implementations.Find(Token.Text);
        if (!OriginalParentClass.IsEmpty() && Token.Text == TEXT("Super") &&
            TokenIndex + 2 < Tokens.Num() && Tokens[TokenIndex + 1].Text == TEXT(":") &&
            Tokens[TokenIndex + 2].Text == TEXT(":") &&
            (TokenIndex == 0 || Tokens[TokenIndex - 1].Text != TEXT(":")))
        {
            Result += OriginalParentClass;
            PreviousEnd = Token.End;
            continue;
        }
        const bool bMemberCall = TokenIndex > 0 &&
            (Tokens[TokenIndex - 1].Text == TEXT(".") || Tokens[TokenIndex - 1].Text == TEXT(":") ||
                (Tokens[TokenIndex - 1].Text == TEXT(">") &&
                    (TokenIndex < 3 || !ThisAliases.Contains(Tokens[TokenIndex - 3].Text))));
        if (Implementation && !bMemberCall && TokenIndex + 1 < Tokens.Num() && Tokens[TokenIndex + 1].Text == TEXT("("))
        {
            Result += *Implementation;
            ++ReplacedCalls;
        }
        else
        {
            Result += Token.Text;
        }
        PreviousEnd = Token.End;
    }
    return Result;
}

static int32 FindGeneratedDelimiter(const TArray<FGeneratedToken>& Tokens, int32 Opening)
{
    const FString OpeningText = Tokens[Opening].Text;
    FString ClosingText;
    if (OpeningText == TEXT("("))
    {
        ClosingText = TEXT(")");
    }
    else if (OpeningText == TEXT("["))
    {
        ClosingText = TEXT("]");
    }
    else
    {
        ClosingText = TEXT("}");
    }
    int32 Depth = 1;
    for (int32 Position = Opening + 1; Position < Tokens.Num(); ++Position)
    {
        if (Tokens[Position].Text == OpeningText)
        {
            ++Depth;
        }
        else if (Tokens[Position].Text == ClosingText && --Depth == 0)
        {
            return Position;
        }
    }
    return INDEX_NONE;
}

static TArray<FString> SplitGeneratedArguments(const FString& Arguments)
{
    TArray<FString> Result;
    const TArray<FGeneratedToken> Tokens = TokenizeGeneratedCode(Arguments);
    int32 Start = 0;
    int32 Depth = 0;
    for (const FGeneratedToken& Token : Tokens)
    {
        if (Token.Text == TEXT("(") || Token.Text == TEXT("[") || Token.Text == TEXT("{") || Token.Text == TEXT("<"))
        {
            ++Depth;
        }
        else if (Token.Text == TEXT(")") || Token.Text == TEXT("]") || Token.Text == TEXT("}") || Token.Text == TEXT(">"))
        {
            --Depth;
        }
        else if (Token.Text == TEXT(",") && Depth == 0)
        {
            Result.Add(Arguments.Mid(Start, Token.Start - Start).TrimStartAndEnd());
            Start = Token.End;
        }
    }
    if (!Arguments.TrimStartAndEnd().IsEmpty())
    {
        Result.Add(Arguments.Mid(Start).TrimStartAndEnd());
    }
    return Result;
}

FString FGeneratedCodeCleanup::RenameGeneratedParameters(const FString& Content, const TMap<FString, FString>& Names)
{
    const TArray<FGeneratedToken> Tokens = TokenizeGeneratedCode(Content);
    FString Result = Content;
    for (int32 Position = Tokens.Num() - 1; Position >= 0; --Position)
    {
        if (Position > 0 && (Tokens[Position - 1].Text == TEXT(".") ||
            (Tokens[Position - 1].Text == TEXT(":") && Position > 1 && Tokens[Position - 2].Text == TEXT(":")) ||
            (Tokens[Position - 1].Text == TEXT(">") && Position > 1 && Tokens[Position - 2].Text == TEXT("-"))))
        {
            continue;
        }
        if (const FString* Name = Names.Find(Tokens[Position].Text))
        {
            Result = Result.Left(Tokens[Position].Start) + *Name + Result.Mid(Tokens[Position].End);
        }
    }
    return Result;
}

FString FGeneratedCodeCleanup::SimplifyImmediateInvocations(const FString& Content)
{
    FString Result = Content;
    int32 OperationIndex = 0;
    for (;;)
    {
        const TArray<FGeneratedToken> Tokens = TokenizeGeneratedCode(Result);
        TMap<int32, int32> LambdaBodies;
        struct FImmediateInvocation
        {
            int32 Capture = 0;
            int32 Parameters = 0;
            int32 ParametersEnd = 0;
            int32 Body = 0;
            int32 BodyEnd = 0;
            int32 Arguments = 0;
            int32 ArgumentsEnd = 0;
            int32 ExpressionStart = 0;
            int32 ExpressionEnd = 0;
        };
        TArray<FImmediateInvocation> Invocations;
        for (int32 Position = 0; Position + 2 < Tokens.Num(); ++Position)
        {
            if (Tokens[Position].Text != TEXT("["))
            {
                continue;
            }
            if (Position > 0)
            {
                const FString& Previous = Tokens[Position - 1].Text;
                if (FChar::IsAlnum(Previous[0]) || Previous == TEXT("]"))
                {
                    if (Previous != TEXT("return"))
                    {
                        continue;
                    }
                }
            }
            const int32 CaptureEnd = FindGeneratedDelimiter(Tokens, Position);
            if (CaptureEnd == INDEX_NONE || CaptureEnd + 1 >= Tokens.Num() || Tokens[CaptureEnd + 1].Text != TEXT("("))
            {
                continue;
            }
            const int32 Parameters = CaptureEnd + 1;
            const int32 ParametersEnd = FindGeneratedDelimiter(Tokens, Parameters);
            if (ParametersEnd == INDEX_NONE)
            {
                continue;
            }
            int32 Body = ParametersEnd + 1;
            while (Body < Tokens.Num() && Tokens[Body].Text != TEXT("{") && Tokens[Body].Text != TEXT(";"))
            {
                ++Body;
            }
            if (Body >= Tokens.Num() || Tokens[Body].Text != TEXT("{"))
            {
                continue;
            }
            const int32 BodyEnd = FindGeneratedDelimiter(Tokens, Body);
            if (BodyEnd == INDEX_NONE)
            {
                continue;
            }
            LambdaBodies.Add(Body, BodyEnd);
            int32 Arguments = BodyEnd + 1;
            int32 ExpressionStart = Position;
            const bool bGrouping = Position > 0 && Tokens[Position - 1].Text == TEXT("(") &&
                (Position < 2 || (!FChar::IsAlnum(Tokens[Position - 2].Text[0]) && Tokens[Position - 2].Text != TEXT(")") &&
                    Tokens[Position - 2].Text != TEXT("]") && Tokens[Position - 2].Text != TEXT(">") &&
                    Tokens[Position - 2].Text != TEXT("}")) || Tokens[Position - 2].Text == TEXT("return"));
            if (Arguments < Tokens.Num() && Tokens[Arguments].Text == TEXT(")") && bGrouping)
            {
                if (FindGeneratedDelimiter(Tokens, Position - 1) == Arguments)
                {
                    ExpressionStart = Position - 1;
                    ++Arguments;
                }
            }
            if (Arguments >= Tokens.Num() || Tokens[Arguments].Text != TEXT("("))
            {
                continue;
            }
            const int32 ArgumentsEnd = FindGeneratedDelimiter(Tokens, Arguments);
            if (ArgumentsEnd != INDEX_NONE)
            {
                int32 ExpressionEnd = ArgumentsEnd;
                if (ExpressionStart == Position && bGrouping && ArgumentsEnd + 1 < Tokens.Num() &&
                    FindGeneratedDelimiter(Tokens, Position - 1) == ArgumentsEnd + 1)
                {
                    ExpressionStart = Position - 1;
                    ExpressionEnd = ArgumentsEnd + 1;
                }
                Invocations.Add({Position, Parameters, ParametersEnd, Body, BodyEnd, Arguments, ArgumentsEnd, ExpressionStart, ExpressionEnd});
            }
        }
        if (Invocations.IsEmpty())
        {
            return Result;
        }
        const FImmediateInvocation& Invocation = Invocations.Last();
        struct FStatementScope
        {
            int32 Start = 0;
            int32 Parentheses = 0;
            int32 Initializers = 0;
        };
        TArray<FStatementScope> Scopes;
        Scopes.Add({});
        TArray<bool> Braces;
        int32 Parentheses = 0;
        int32 Initializers = 0;
        for (int32 Position = 0; Position < Invocation.ExpressionStart; ++Position)
        {
            const FString& Token = Tokens[Position].Text;
            if (Token == TEXT("("))
            {
                ++Parentheses;
            }
            else if (Token == TEXT(")"))
            {
                --Parentheses;
            }
            else if (Token == TEXT("{"))
            {
                FString Previous;
                if (Position > 0)
                {
                    Previous = Tokens[Position - 1].Text;
                }
                const bool bBlock = LambdaBodies.Contains(Position) || Previous == TEXT(")") || Previous == TEXT("const") || Previous == TEXT("else") ||
                    Previous == TEXT("do") || Previous == TEXT("try") || Previous == TEXT(";") || Previous == TEXT("}") || Previous == TEXT(":") ||
                    Previous.IsEmpty() || (Previous == TEXT("{") && Initializers == Scopes.Last().Initializers);
                Braces.Add(bBlock);
                if (bBlock)
                {
                    Scopes.Add({Tokens[Position].End, Parentheses, Initializers});
                }
                else
                {
                    ++Initializers;
                }
            }
            else if (Token == TEXT("}") && !Braces.IsEmpty())
            {
                const bool bBlock = Braces.Pop();
                if (bBlock && Scopes.Num() > 1)
                {
                    Scopes.Pop();
                    if (!LambdaBodies.FindKey(Position) && Parentheses == Scopes.Last().Parentheses &&
                        Initializers == Scopes.Last().Initializers)
                    {
                        Scopes.Last().Start = Tokens[Position].End;
                    }
                }
                else if (!bBlock)
                {
                    --Initializers;
                }
            }
            else if (Token == TEXT(";") && Parentheses == Scopes.Last().Parentheses && Initializers == Scopes.Last().Initializers)
            {
                Scopes.Last().Start = Tokens[Position].End;
            }
        }
        const int32 StatementStart = Scopes.Last().Start;
        const FString Body = Result.Mid(Tokens[Invocation.Body].End, Tokens[Invocation.BodyEnd].Start - Tokens[Invocation.Body].End);
        bool bReturns = false;
        for (int32 Position = Invocation.Body + 1; Position < Invocation.BodyEnd; ++Position)
        {
            if (const int32* NestedBodyEnd = LambdaBodies.Find(Position))
            {
                Position = *NestedBodyEnd;
            }
            else if (Tokens[Position].Text == TEXT("return"))
            {
                bReturns = true;
            }
        }
        const int32 InvocationStart = Tokens[Invocation.ExpressionStart].Start;
        const int32 InvocationEnd = Tokens[Invocation.ExpressionEnd].End;
        const FString Prefix = Result.Mid(StatementStart, InvocationStart - StatementStart).TrimStartAndEnd();
        const FString Captures = Result.Mid(Tokens[Invocation.Capture].End,
            Tokens[Invocation.Parameters - 1].Start - Tokens[Invocation.Capture].End).TrimStartAndEnd();
        const bool bStandalone = Prefix.IsEmpty() && Invocation.ExpressionEnd + 1 < Tokens.Num() &&
            Tokens[Invocation.ExpressionEnd + 1].Text == TEXT(";");
        if (bReturns && (Captures.IsEmpty() || Captures == TEXT("&")) && Invocation.ParametersEnd == Invocation.Parameters + 1 &&
            Invocation.ArgumentsEnd == Invocation.Arguments + 1 && Invocation.BodyEnd > Invocation.Body + 3 &&
            Tokens[Invocation.BodyEnd - 3].Text == TEXT("return") && Tokens[Invocation.BodyEnd - 1].Text == TEXT(";") &&
            Invocation.ExpressionEnd + 1 < Tokens.Num())
        {
            const TArray<FGeneratedToken> PrefixTokens = TokenizeGeneratedCode(Prefix);
            const bool bSingleArgumentCall = PrefixTokens.Num() > 1 && PrefixTokens.Last().Text == TEXT("(") &&
                Invocation.ExpressionEnd + 2 < Tokens.Num() && Tokens[Invocation.ExpressionEnd + 1].Text == TEXT(")") &&
                Tokens[Invocation.ExpressionEnd + 2].Text == TEXT(";");
            bool bSimpleAssignment = !PrefixTokens.IsEmpty() &&
                ((PrefixTokens.Last().Text == TEXT("=") && Tokens[Invocation.ExpressionEnd + 1].Text == TEXT(";")) || bSingleArgumentCall);
            for (int32 PrefixIndex = 1; PrefixIndex < PrefixTokens.Num(); ++PrefixIndex)
            {
                if (FChar::IsAlnum(PrefixTokens[PrefixIndex - 1].Text[0]) &&
                    FChar::IsAlnum(PrefixTokens[PrefixIndex].Text[0]))
                {
                    bSimpleAssignment = false;
                }
            }
            for (int32 PrefixIndex = 0; PrefixIndex < PrefixTokens.Num(); ++PrefixIndex)
            {
                const FGeneratedToken& PrefixToken = PrefixTokens[PrefixIndex];
                if (bSingleArgumentCall && PrefixIndex == PrefixTokens.Num() - 1)
                {
                    continue;
                }
                if (PrefixToken.Text != TEXT("=") && PrefixToken.Text != TEXT(".") && PrefixToken.Text != TEXT("-") &&
                    PrefixToken.Text != TEXT(">") && !FChar::IsAlnum(PrefixToken.Text[0]) && PrefixToken.Text[0] != TEXT('_'))
                {
                    bSimpleAssignment = false;
                }
            }
            int32 ReturnCount = 0;
            for (int32 BodyPosition = Invocation.Body + 1; BodyPosition < Invocation.BodyEnd; ++BodyPosition)
            {
                if (Tokens[BodyPosition].Text == TEXT("return"))
                {
                    ++ReturnCount;
                }
            }
            if (bSimpleAssignment && ReturnCount == 1)
            {
                const FString Value = Tokens[Invocation.BodyEnd - 2].Text;
                FString CallPrefix = Prefix;
                FString ReceiverBinding;
                if (bSingleArgumentCall && PrefixTokens.Num() > 3)
                {
                    int32 MemberAccess = PrefixTokens.Num() - 3;
                    FString BindingType = TEXT("auto& ");
                    if (PrefixTokens[MemberAccess].Text == TEXT(">") && MemberAccess > 0 && PrefixTokens[MemberAccess - 1].Text == TEXT("-"))
                    {
                        --MemberAccess;
                        BindingType = TEXT("const auto ");
                    }
                    if (PrefixTokens[MemberAccess].Text == TEXT(".") || PrefixTokens[MemberAccess].Text == TEXT("-"))
                    {
                        FString ReceiverName;
                        do
                        {
                            ReceiverName = FString::Format(TEXT("PreparedTarget{0}"), {++OperationIndex});
                        }
                        while (Result.Contains(ReceiverName));
                        ReceiverBinding = BindingType + ReceiverName + TEXT(" = ") + Prefix.Left(PrefixTokens[MemberAccess].Start) + TEXT(";\n");
                        CallPrefix = ReceiverName + Prefix.Mid(PrefixTokens[MemberAccess].Start);
                    }
                }
                const FString Initializer = Result.Mid(Tokens[Invocation.Body].End,
                    Tokens[Invocation.BodyEnd - 3].Start - Tokens[Invocation.Body].End);
                FString Suffix = TEXT(";\n}\n");
                int32 StatementEnd = Invocation.ExpressionEnd + 1;
                if (bSingleArgumentCall)
                {
                    Suffix = TEXT(");\n}\n");
                    ++StatementEnd;
                }
                Result = Result.Left(StatementStart) + TEXT("\n{\n") + ReceiverBinding + Initializer + TEXT("\n") + CallPrefix +
                    TEXT(" ") + Value + Suffix + Result.Mid(Tokens[StatementEnd].End);
                continue;
            }
        }
        if (Invocation.ParametersEnd + 1 == Invocation.Body && Invocation.Body + 2 < Invocation.BodyEnd &&
            Tokens[Invocation.Body + 1].Text == TEXT("return") && Tokens[Invocation.BodyEnd - 1].Text == TEXT(";") &&
            (Captures.IsEmpty() || Captures == TEXT("&")))
        {
            const FString Expression = Result.Mid(Tokens[Invocation.Body + 1].End,
                Tokens[Invocation.BodyEnd - 1].Start - Tokens[Invocation.Body + 1].End).TrimStartAndEnd();
            const TArray<FGeneratedToken> ExpressionTokens = TokenizeGeneratedCode(Expression);
            bool bSimpleExpression = Expression.Contains(TEXT(".Num(")) || Expression.Contains(TEXT(".IsEmpty(")) ||
                Expression.Contains(TEXT(".Contains(")) || Expression.Contains(TEXT(".Add(")) || Expression.Contains(TEXT(".Remove(")) ||
                Expression.Contains(TEXT(".IsValidIndex("));
            for (const FGeneratedToken& Token : ExpressionTokens)
            {
                if (Token.Text == TEXT(";") || Token.Text == TEXT("return") || Token.Text == TEXT("[") ||
                    Token.Text == TEXT("&") || Token.Text == TEXT("|") || Token.Text == TEXT("?") ||
                    Token.Text == TEXT("sizeof") || Token.Text == TEXT("decltype") || Token.Text == TEXT("noexcept"))
                {
                    bSimpleExpression = false;
                }
            }
            const TArray<FString> Parameters = SplitGeneratedArguments(Result.Mid(Tokens[Invocation.Parameters].End,
                Tokens[Invocation.ParametersEnd].Start - Tokens[Invocation.Parameters].End));
            const TArray<FString> Arguments = SplitGeneratedArguments(Result.Mid(Tokens[Invocation.Arguments].End,
                Tokens[Invocation.ArgumentsEnd].Start - Tokens[Invocation.Arguments].End));
            bSimpleExpression = bSimpleExpression && Parameters.Num() == Arguments.Num();
            TMap<FString, FString> ParameterExpressions;
            if (bSimpleExpression)
            {
                for (int32 ParameterIndex = 0; ParameterIndex < Parameters.Num(); ++ParameterIndex)
                {
                    const TArray<FGeneratedToken> ParameterTokens = TokenizeGeneratedCode(Parameters[ParameterIndex]);
                    const FString& Name = ParameterTokens.Last().Text;
                    const FString Type = Parameters[ParameterIndex].Left(ParameterTokens.Last().Start).TrimStartAndEnd();
                    int32 Uses = 0;
                    for (int32 TokenIndex = 0; TokenIndex < ExpressionTokens.Num(); ++TokenIndex)
                    {
                        if (ExpressionTokens[TokenIndex].Text == Name &&
                            (TokenIndex == 0 || ExpressionTokens[TokenIndex - 1].Text != TEXT(".")))
                        {
                            ++Uses;
                        }
                    }
                    const bool bGeneric = Type == TEXT("const auto&") || Type == TEXT("auto&") || Type == TEXT("auto&&");
                    const bool bScalar = Type == TEXT("int32") || Type == TEXT("int64") || Type == TEXT("bool") ||
                        Type == TEXT("float") || Type == TEXT("double");
                    bSimpleExpression = bSimpleExpression && Uses == 1 && (bGeneric || bScalar);
                    FString ArgumentExpression = TEXT("(") + Arguments[ParameterIndex] + TEXT(")");
                    if (bScalar)
                    {
                        ArgumentExpression = TEXT("static_cast<") + Type + TEXT(">") + ArgumentExpression;
                    }
                    ParameterExpressions.Add(Name, ArgumentExpression);
                }
            }
            if (bSimpleExpression)
            {
                Result = Result.Left(InvocationStart) + TEXT("(") + RenameGeneratedParameters(Expression, ParameterExpressions) +
                    TEXT(")") + Result.Mid(InvocationEnd);
                continue;
            }
        }
        if (bStandalone && !bReturns && (Captures.IsEmpty() || Captures == TEXT("&")))
        {
            const TArray<FString> Parameters = SplitGeneratedArguments(Result.Mid(Tokens[Invocation.Parameters].End,
                Tokens[Invocation.ParametersEnd].Start - Tokens[Invocation.Parameters].End));
            const TArray<FString> Arguments = SplitGeneratedArguments(Result.Mid(Tokens[Invocation.Arguments].End,
                Tokens[Invocation.ArgumentsEnd].Start - Tokens[Invocation.Arguments].End));
            if (Parameters.Num() == Arguments.Num())
            {
                TMap<FString, FString> ParameterNames;
                for (const FString& Parameter : Parameters)
                {
                    const TArray<FGeneratedToken> ParameterTokens = TokenizeGeneratedCode(Parameter);
                    if (!ParameterTokens.IsEmpty())
                    {
                        const FString& Name = ParameterTokens.Last().Text;
                        FString BindingName;
                        do
                        {
                            BindingName = FString::Format(TEXT("Operation{0}{1}"), {Name, ++OperationIndex});
                        }
                        while (Result.Contains(BindingName));
                        ParameterNames.Add(Name, BindingName);
                    }
                }
                FString Bindings;
                for (int32 ParameterIndex = 0; ParameterIndex < Parameters.Num(); ++ParameterIndex)
                {
                    Bindings += RenameGeneratedParameters(Parameters[ParameterIndex], ParameterNames) + TEXT(" = ") +
                        Arguments[ParameterIndex] + TEXT(";\n");
                }
                const FString Block = TEXT("{\n") + Bindings + RenameGeneratedParameters(Body, ParameterNames) + TEXT("\n}");
                Result = Result.Left(InvocationStart) + Block + Result.Mid(Tokens[Invocation.ExpressionEnd + 1].End);
                continue;
            }
        }
        FString OperationName;
        FString OperationPurpose = TEXT("EvaluatePreparedOperation");
        if (Body.Contains(TEXT("BlueprintFunction")))
        {
            OperationPurpose = TEXT("InvokeBlueprintFunction");
        }
        else if (Body.Contains(TEXT("PropertyTarget")) || Body.Contains(TEXT("MemberProperty")))
        {
            OperationPurpose = TEXT("ReadBlueprintProperty");
        }
        else if (Body.Contains(TEXT("StructValue")) || Body.Contains(TEXT("StructureValue")))
        {
            OperationPurpose = TEXT("MakeStructureValue");
        }
        else if (Body.Contains(TEXT("ConvertedValues")))
        {
            OperationPurpose = TEXT("ConvertContainerValues");
        }
        else if (Body.Contains(TEXT("SourceCallback")))
        {
            OperationPurpose = TEXT("CreateNativeCallback");
        }
        else if (Body.Contains(TEXT("switch")))
        {
            OperationPurpose = TEXT("SelectValue");
        }
        do
        {
            OperationName = FString::Format(TEXT("{0}{1}"), {OperationPurpose, ++OperationIndex});
        }
        while (Result.Contains(OperationName));
        const FString Definition = Result.Mid(Tokens[Invocation.Capture].Start,
            Tokens[Invocation.BodyEnd].End - Tokens[Invocation.Capture].Start);
        const FString Arguments = Result.Mid(Tokens[Invocation.Arguments].Start,
            Tokens[Invocation.ArgumentsEnd].End - Tokens[Invocation.Arguments].Start);
        Result = Result.Left(StatementStart) + TEXT("\nconst auto ") + OperationName + TEXT(" = ") + Definition + TEXT(";\n") +
            Result.Mid(StatementStart, InvocationStart - StatementStart) + OperationName + Arguments + Result.Mid(InvocationEnd);
    }
}

static TMap<FString, int32> CountGeneratedIdentifiers(const FString& Content)
{
    TMap<FString, int32> Counts;
    bool bQuoted = false;
    TCHAR Quote = 0;
    for (int32 Position = 0; Position < Content.Len(); ++Position)
    {
        const TCHAR Character = Content[Position];
        if (bQuoted)
        {
            if (Character == TEXT('\\'))
            {
                ++Position;
            }
            else if (Character == Quote)
            {
                bQuoted = false;
            }
            continue;
        }
        if (Character == TEXT('"') || Character == TEXT('\''))
        {
            bQuoted = true;
            Quote = Character;
            continue;
        }
        if (!FChar::IsAlpha(Character) && Character != TEXT('_'))
        {
            continue;
        }
        const int32 Start = Position;
        while (Position + 1 < Content.Len() && (FChar::IsAlnum(Content[Position + 1]) || Content[Position + 1] == TEXT('_')))
        {
            ++Position;
        }
        ++Counts.FindOrAdd(Content.Mid(Start, Position - Start + 1));
    }
    return Counts;
}

void FGeneratedCodeCleanup::RemoveRedundantOverrides(FString& Header, FString& Source)
{
    const TArray<FGeneratedToken> Tokens = TokenizeGeneratedCode(Source);
    for (int32 Position = Tokens.Num() - 15; Position >= 0; --Position)
    {
        if (Tokens[Position].Text != TEXT("void") || Tokens[Position + 2].Text != TEXT(":") ||
            Tokens[Position + 3].Text != TEXT(":") || Tokens[Position + 5].Text != TEXT("("))
        {
            continue;
        }
        const int32 ParametersEnd = FindGeneratedDelimiter(Tokens, Position + 5);
        if (ParametersEnd == INDEX_NONE || ParametersEnd + 7 >= Tokens.Num() || Tokens[ParametersEnd + 1].Text != TEXT("{") ||
            Tokens[ParametersEnd + 2].Text != TEXT("Super") || Tokens[ParametersEnd + 3].Text != TEXT(":") ||
            Tokens[ParametersEnd + 4].Text != TEXT(":") || Tokens[ParametersEnd + 5].Text != Tokens[Position + 4].Text ||
            Tokens[ParametersEnd + 6].Text != TEXT("("))
        {
            continue;
        }
        const int32 ArgumentsEnd = FindGeneratedDelimiter(Tokens, ParametersEnd + 6);
        if (ArgumentsEnd == INDEX_NONE || ArgumentsEnd + 2 >= Tokens.Num() ||
            Tokens[ArgumentsEnd + 1].Text != TEXT(";") || Tokens[ArgumentsEnd + 2].Text != TEXT("}"))
        {
            continue;
        }
        const FString Signature = Source.Mid(Tokens[Position + 4].Start, Tokens[ParametersEnd].End - Tokens[Position + 4].Start);
        const FString Declaration = TEXT("virtual void ") + Signature + TEXT(" override;");
        const int32 DeclarationStart = Header.Find(Declaration, ESearchCase::CaseSensitive);
        if (DeclarationStart == INDEX_NONE)
        {
            continue;
        }
        const TArray<FString> Parameters = SplitGeneratedArguments(Source.Mid(Tokens[Position + 5].End,
            Tokens[ParametersEnd].Start - Tokens[Position + 5].End));
        TArray<FString> Names;
        for (const FString& Parameter : Parameters)
        {
            const TArray<FGeneratedToken> ParameterTokens = TokenizeGeneratedCode(Parameter);
            if (!ParameterTokens.IsEmpty())
            {
                Names.Add(ParameterTokens.Last().Text);
            }
        }
        const TArray<FString> Arguments = SplitGeneratedArguments(Source.Mid(Tokens[ParametersEnd + 6].End,
            Tokens[ArgumentsEnd].Start - Tokens[ParametersEnd + 6].End));
        if (Names != Arguments)
        {
            continue;
        }
        Header.RemoveAt(DeclarationStart, Declaration.Len());
        Source.RemoveAt(Tokens[Position].Start, Tokens[ArgumentsEnd + 2].End - Tokens[Position].Start);
    }
}

FString FGeneratedCodeCleanup::RemoveUnusedScalarDeclarations(const FString& Declarations, const FString& Body)
{
    const TMap<FString, int32> BodyIdentifiers = CountGeneratedIdentifiers(Body);
    const TMap<FString, int32> DeclarationIdentifiers = CountGeneratedIdentifiers(Declarations);
    TArray<FString> Lines;
    Declarations.ParseIntoArrayLines(Lines, false);
    FString Result;
    for (const FString& Line : Lines)
    {
        FString Left;
        FString Initializer;
        bool bCanRemove = Line.Split(TEXT(" = "), &Left, &Initializer);
        FString Type;
        FString Name;
        bCanRemove = bCanRemove && Left.Split(TEXT(" "), &Type, &Name, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
        Initializer.TrimStartAndEndInline();
        bCanRemove = bCanRemove && (Type.EndsWith(TEXT("*")) || Type == TEXT("bool") || Type == TEXT("int32") ||
            Type == TEXT("int64") || Type == TEXT("float") || Type == TEXT("double"));
        bCanRemove = bCanRemove && (Initializer == TEXT("nullptr;") || Initializer == TEXT("false;") ||
            Initializer == TEXT("true;") || Initializer == TEXT("{};") || Initializer.LeftChop(1).IsNumeric());
        if (bCanRemove && !BodyIdentifiers.Contains(Name) && DeclarationIdentifiers.FindRef(Name) == 1)
        {
            continue;
        }
        Result += Line + TEXT("\n");
    }
    return Result;
}

FString FGeneratedCodeCleanup::RemoveUnusedForwardDeclarations(const FString& Header)
{
    const TMap<FString, int32> Identifiers = CountGeneratedIdentifiers(Header);
    TArray<FString> Lines;
    Header.ParseIntoArrayLines(Lines, false);
    FString Result;
    for (const FString& Line : Lines)
    {
        FString Declaration = Line.TrimStartAndEnd();
        if ((Declaration.RemoveFromStart(TEXT("class ")) || Declaration.RemoveFromStart(TEXT("struct "))) &&
            Declaration.RemoveFromEnd(TEXT(";")) && !Declaration.Contains(TEXT(" ")) && Identifiers.FindRef(Declaration) == 1)
        {
            continue;
        }
        Result += Line + TEXT("\n");
    }
    const TArray<FGeneratedToken> Tokens = TokenizeGeneratedCode(Result);
    TArray<TPair<int32, int32>> Removals;
    TMap<int32, FString> AccessByDepth;
    int32 Depth = 0;
    for (int32 Position = 0; Position < Tokens.Num(); ++Position)
    {
        if (Tokens[Position].Text == TEXT("{"))
        {
            ++Depth;
        }
        else if (Tokens[Position].Text == TEXT("}"))
        {
            AccessByDepth.Remove(Depth);
            --Depth;
        }
        else if (Position + 1 < Tokens.Num() && Tokens[Position + 1].Text == TEXT(":") &&
            (Tokens[Position].Text == TEXT("public") || Tokens[Position].Text == TEXT("private") || Tokens[Position].Text == TEXT("protected")))
        {
            const bool bEmptySection = Position + 3 < Tokens.Num() && Tokens[Position + 3].Text == TEXT(":") &&
                (Tokens[Position + 2].Text == TEXT("public") || Tokens[Position + 2].Text == TEXT("private") || Tokens[Position + 2].Text == TEXT("protected"));
            if (bEmptySection || AccessByDepth.FindRef(Depth) == Tokens[Position].Text)
            {
                Removals.Emplace(Tokens[Position].Start, Tokens[Position + 1].End);
            }
            else
            {
                AccessByDepth.Add(Depth, Tokens[Position].Text);
            }
        }
    }
    for (int32 RemovalIndex = Removals.Num() - 1; RemovalIndex >= 0; --RemovalIndex)
    {
        Result.RemoveAt(Removals[RemovalIndex].Key, Removals[RemovalIndex].Value - Removals[RemovalIndex].Key);
    }
    return Result;
}

FString FGeneratedCodeCleanup::RemoveTrailingVoidReturn(const FString& Body)
{
    FString Result = Body.TrimEnd();
    if (Result == TEXT("return;") || Result.EndsWith(TEXT("\nreturn;")))
    {
        Result.LeftChopInline(7);
    }
    return Result + TEXT("\n");
}

FString FGeneratedCodeCleanup::CollapseImmediateScalarInitialization(const FString& Body)
{
    TArray<FString> Lines;
    Body.ParseIntoArrayLines(Lines, false);
    FString Result;
    for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
    {
        FString Declaration = Lines[LineIndex].TrimStartAndEnd();
        FString Type;
        FString Name;
        if (LineIndex + 1 < Lines.Num() && Declaration.RemoveFromEnd(TEXT("{};")) &&
            Declaration.Split(TEXT(" "), &Type, &Name, ESearchCase::CaseSensitive, ESearchDir::FromEnd) &&
            (Type.EndsWith(TEXT("*")) || Type == TEXT("bool") || Type == TEXT("int32") || Type == TEXT("float") || Type == TEXT("double")))
        {
            const FString Assignment = Lines[LineIndex + 1].TrimStartAndEnd();
            if (Assignment.StartsWith(Name + TEXT(" = ")))
            {
                Result += Type + TEXT(" ") + Assignment + TEXT("\n");
                ++LineIndex;
                continue;
            }
        }
        Result += Lines[LineIndex] + TEXT("\n");
    }
    return Result;
}
