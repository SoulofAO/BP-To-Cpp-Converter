/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "CustomCodeEditorWidget.h"
#include "Framework/Text/SlateTextRun.h"
#include "BlueprintNativizationSettings.h"

UColorEditableText::UColorEditableText(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
}

TSharedRef<SWidget> UColorEditableText::RebuildWidget()
{
    MyColorEditableText = SNew(SColorEditableText);

    return MyColorEditableText.ToSharedRef();
}

void UColorEditableText::SynchronizeProperties()
{
    Super::SynchronizeProperties();

    if (MyColorEditableText.IsValid())
    {
        MyColorEditableText->SetText(GetText());
    }
}

void UColorEditableText::ReleaseSlateResources(bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren);

    MyColorEditableText.Reset();
}

void UColorEditableText::SetColorEditableText(FText InText)
{
	if (MyColorEditableText.IsValid())
	{
		MyColorEditableText->SetText(InText);
	}
}

class FChucKSyntaxTokenizer : public ISyntaxTokenizer
{
	TArray<FString> Keywords;
	TArray<FString> Operators;
public:

	static TSharedRef<FChucKSyntaxTokenizer> Create()
	{
		return MakeShareable(new FChucKSyntaxTokenizer());
	}

	virtual ~FChucKSyntaxTokenizer()
	{

	}
protected:

	virtual void Process(TArray<FTokenizedLine>& OutTokenizedLines, const FString& Input) override
	{
		TArray<FTextRange> LineRanges;
		FTextRange::CalculateLineRangesFromString(Input, LineRanges);
		TokenizeLineRanges(Input, LineRanges, OutTokenizedLines);
	};

	FChucKSyntaxTokenizer()
	{
		const UBlueprintNativizationV2EditorSettings* Settings = GetDefault<UBlueprintNativizationV2EditorSettings>();

		for (FColorEditableKeywordGroup ColorEditableTextGroup : Settings->ColorEditableTextGroups)
		{
			for (FString Name : ColorEditableTextGroup.Names)
			{
				Keywords.Add(Name);
			}
		}

		for (const auto& Operator : Settings->ColorEditableTextOperators)
		{
			Operators.Add(Operator);
		}

	}
	static FORCEINLINE bool IsAlpha(TCHAR Char)
	{
		return (Char >= 'a' && Char <= 'z') || (Char >= 'A' && Char <= 'Z');
	}

	static FORCEINLINE bool IsDigit(TCHAR Char)
	{
		return Char >= '0' && Char <= '9';
	}

	static FORCEINLINE bool IsAlphaOrDigit(TCHAR Char)
	{
		return IsAlpha(Char) || IsDigit(Char);
	}

	void TokenizeLineRanges(const FString& Input, const TArray<FTextRange>& LineRanges, TArray<FTokenizedLine>& OutTokenizedLines)
	{
		// Tokenize line ranges
		for (const FTextRange& LineRange : LineRanges)
		{
			FTokenizedLine TokenizedLine;
			TokenizedLine.Range = LineRange;

			if (TokenizedLine.Range.IsEmpty())
			{
				TokenizedLine.Tokens.Emplace(FToken(ETokenType::Literal, TokenizedLine.Range));
			}
			else
			{
				int32 CurrentOffset = LineRange.BeginIndex;

				if (CurrentOffset < LineRange.EndIndex - 8 && FCString::Strncmp(&Input[CurrentOffset], TEXT("#include"), 8) == 0)
				{
					TokenizedLine.Tokens.Emplace(FToken(ETokenType::Syntax, FTextRange(CurrentOffset, LineRange.EndIndex)));
					OutTokenizedLines.Add(TokenizedLine);
					continue;
				}

				while (CurrentOffset < LineRange.EndIndex)
				{
					const TCHAR* CurrentString = &Input[CurrentOffset];
					const TCHAR CurrentChar = Input[CurrentOffset];

					//if current char is "." print something, we'll figure it out later
					if (CurrentChar == TEXT('.'))
					{
						//UE_LOG(LogTemp, Warning, TEXT("Current char is a dot"));
						//continue;
					}

					bool bHasMatchedSyntax = false;

					// Greedy matching for operators
					for (const FString& Operator : Operators)
					{
						if (FCString::Strncmp(CurrentString, *Operator, Operator.Len()) == 0)
						{
							const int32 SyntaxTokenEnd = CurrentOffset + Operator.Len();
							TokenizedLine.Tokens.Emplace(FToken(ETokenType::Syntax, FTextRange(CurrentOffset, SyntaxTokenEnd)));

							check(SyntaxTokenEnd <= LineRange.EndIndex);

							bHasMatchedSyntax = true;
							//UE_CLOG(bHasMatchedSyntax, LogTemp, Warning, TEXT("Matched Operator: %s"), *Operator);
							CurrentOffset = SyntaxTokenEnd;
							break;
						}
					}

					if (bHasMatchedSyntax)
					{
						continue;
					}

					int32 PeekOffset = CurrentOffset + 1;
					if (CurrentChar == TEXT('#'))
					{

						while (PeekOffset < LineRange.EndIndex)
						{
							const TCHAR PeekChar = Input[PeekOffset];


							if (!IsAlpha(PeekChar))
							{
								break;
							}

							PeekOffset++;
						}
					}
					else if (IsAlpha(CurrentChar))
					{
						// Match Identifiers,
						// They start with a letter and contain
						// letters or numbers
						while (PeekOffset < LineRange.EndIndex)
						{
							const TCHAR PeekChar = Input[PeekOffset];

							if (!IsAlphaOrDigit(PeekChar))
							{
								break;
							}

							PeekOffset++;
						}
					}


					const int32 CurrentStringLength = PeekOffset - CurrentOffset;

					// Check if it is an reserved keyword
					for (const FString& Keyword : Keywords)
					{
						if (FCString::Strncmp(CurrentString, *Keyword, CurrentStringLength) == 0)
						{
							const int32 SyntaxTokenEnd = CurrentOffset + CurrentStringLength;
							TokenizedLine.Tokens.Emplace(FToken(ETokenType::Syntax, FTextRange(CurrentOffset, SyntaxTokenEnd)));

							check(SyntaxTokenEnd <= LineRange.EndIndex);

							bHasMatchedSyntax = true;
							CurrentOffset = SyntaxTokenEnd;
							break;
						}
					}

					if (bHasMatchedSyntax)
					{
						continue;
					}

					// If none matched, consume the character(s) as text
					const int32 TextTokenEnd = CurrentOffset + CurrentStringLength;
					TokenizedLine.Tokens.Emplace(FToken(ETokenType::Literal, FTextRange(CurrentOffset, TextTokenEnd)));
					CurrentOffset = TextTokenEnd;
				}
			}
			OutTokenizedLines.Add(TokenizedLine);
		}
	};
};


FColoredTextLayoutMarshaller::FColoredTextLayoutMarshaller(TSharedPtr<ISyntaxTokenizer> InTokenizer, FTextBlockStyle NewDefaultStyle)
    : FSyntaxHighlighterTextLayoutMarshaller(MoveTemp(InTokenizer))
{
	DefaultStyle = NewDefaultStyle;
}

TSharedPtr<ISyntaxTokenizer> FColoredTextLayoutMarshaller::CreateTokenizer()
{
    return FChucKSyntaxTokenizer::Create();
}

void FColoredTextLayoutMarshaller::ParseTokens(const FString& SourceString, FTextLayout& TargetTextLayout, TArray<ISyntaxTokenizer::FTokenizedLine> TokenizedLines)
{
    TArray<FTextLayout::FNewLineData> LinesToAdd;
    LinesToAdd.Reserve(TokenizedLines.Num());
    int32 LineNo = 0;

    for (auto& TokenizedLine : TokenizedLines)
    {
        LinesToAdd.Add(ProcessTokenizedLine(TokenizedLine, LineNo, SourceString));
        LineNo++;
    }

    TargetTextLayout.AddLines(LinesToAdd);

}

FTextLayout::FNewLineData FColoredTextLayoutMarshaller::ProcessTokenizedLine(const ISyntaxTokenizer::FTokenizedLine& TokenizedLine, const int32& LineNumber, const FString& SourceString)
{
    TSharedRef<FString> ModelString = MakeShareable(new FString());
    TArray<TSharedRef<IRun>> Runs;

    for (const ISyntaxTokenizer::FToken& Token : TokenizedLine.Tokens)
    {
        const FString TokenText = SourceString.Mid(Token.Range.BeginIndex, Token.Range.Len());

        const FTextRange ModelRange(ModelString->Len(), ModelString->Len() + TokenText.Len());
        ModelString->Append(TokenText);

        FRunInfo RunInfo(TEXT("SyntaxHighlight.Normal"));
		FTextBlockStyle TextBlockStyle = DefaultStyle;
		const UBlueprintNativizationV2EditorSettings* Settings = GetDefault<UBlueprintNativizationV2EditorSettings>();

		FLinearColor OutLinearColor;
        if (UBlueprintNativizationSettingsLibrary::FindColorByNameInColorTextGroup(TokenText, OutLinearColor))
        {
            TextBlockStyle.SetColorAndOpacity(FSlateColor(OutLinearColor));
            RunInfo.Name = TEXT("SyntaxHighlight.Keyword");
        }
		else if (TokenText.StartsWith("#include"))
		{
			TextBlockStyle.SetColorAndOpacity(FSlateColor(FLinearColor(1.0, 0.5, 0.0)));
			RunInfo.Name = TEXT("SyntaxHighlight.Include");
		}
        else if (Token.Type == ISyntaxTokenizer::ETokenType::Syntax)
        {
            RunInfo.Name = TEXT("SyntaxHighlight.Syntax");
        }
        else if (Token.Type == ISyntaxTokenizer::ETokenType::Literal)
        {
            RunInfo.Name = TEXT("SyntaxHighlight.Literal");
        }

        TSharedRef<ISlateRun> Run = FSlateTextRun::Create(RunInfo, ModelString, TextBlockStyle, ModelRange);
        Runs.Add(Run);
    }

    return FTextLayout::FNewLineData(MoveTemp(ModelString), MoveTemp(Runs));
}
