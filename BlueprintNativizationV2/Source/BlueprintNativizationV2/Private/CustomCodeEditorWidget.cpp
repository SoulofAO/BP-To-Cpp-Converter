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
		// Tokenize line ranges (now supports // single-line comments and /* ... */ block comments)
		int32 NumLines = LineRanges.Num();
		int32 LineIndex = 0;

		while (LineIndex < NumLines)
		{
			const FTextRange LineRange = LineRanges[LineIndex];
			FTokenizedLine TokenizedLine;
			TokenizedLine.Range = LineRange;

			if (TokenizedLine.Range.IsEmpty())
			{
				TokenizedLine.Tokens.Emplace(FToken(ETokenType::Literal, TokenizedLine.Range));
				OutTokenizedLines.Add(TokenizedLine);
				++LineIndex;
				continue;
			}

			int32 CurrentOffset = LineRange.BeginIndex;

			// quick include check (preserve existing behaviour)
			if (CurrentOffset < LineRange.EndIndex - 8 && FCString::Strncmp(&Input[CurrentOffset], TEXT("#include"), 8) == 0)
			{
				TokenizedLine.Tokens.Emplace(FToken(ETokenType::Syntax, FTextRange(CurrentOffset, LineRange.EndIndex)));
				OutTokenizedLines.Add(TokenizedLine);
				++LineIndex;
				continue;
			}

			while (CurrentOffset < LineRange.EndIndex)
			{
				const TCHAR* CurrentString = &Input[CurrentOffset];
				const TCHAR CurrentChar = Input[CurrentOffset];

				// 1) Check for single-line comment "//" (take rest of line)
				if (CurrentOffset + 1 < Input.Len() && CurrentChar == TEXT('/') && Input[CurrentOffset + 1] == TEXT('/'))
				{
					const int32 CommentEnd = LineRange.EndIndex;
					TokenizedLine.Tokens.Emplace(FToken(ETokenType::Syntax, FTextRange(CurrentOffset, CommentEnd)));
					// we're done with this line
					CurrentOffset = CommentEnd;
					break;
				}

				// 2) Check for block comment "/* ... */"
				if (CurrentOffset + 1 < Input.Len() && CurrentChar == TEXT('/') && Input[CurrentOffset + 1] == TEXT('*'))
				{
					// Find end "*/" in the whole Input
					int32 CommentEnd = INDEX_NONE;
					for (int32 Pos = CurrentOffset + 2; Pos + 1 < Input.Len(); ++Pos)
					{
						if (Input[Pos] == TEXT('*') && Input[Pos + 1] == TEXT('/'))
						{
							CommentEnd = Pos + 2; // end is after "*/"
							break;
						}
					}
					if (CommentEnd == INDEX_NONE)
					{
						// not found -> goes to EOF
						CommentEnd = Input.Len();
					}

					// If block comment ends within the same line -> simple
					if (CommentEnd <= LineRange.EndIndex)
					{
						TokenizedLine.Tokens.Emplace(FToken(ETokenType::Syntax, FTextRange(CurrentOffset, CommentEnd)));
						CurrentOffset = CommentEnd;
						continue; // continue tokenizing remainder of this line (if any)
					}

					// Block comment spans beyond this line:
					//  - add comment token for current line from CurrentOffset to line end
					TokenizedLine.Tokens.Emplace(FToken(ETokenType::Syntax, FTextRange(CurrentOffset, LineRange.EndIndex)));
					OutTokenizedLines.Add(TokenizedLine);

					// Now fill intermediate full lines and the final partial line that contains CommentEnd
					int32 NextLineIndex = LineIndex + 1;
					// handle fully-covered intermediate lines
					while (NextLineIndex < NumLines && LineRanges[NextLineIndex].EndIndex <= CommentEnd)
					{
						FTokenizedLine MidLine;
						MidLine.Range = LineRanges[NextLineIndex];
						// the whole line is inside the block comment
						MidLine.Tokens.Emplace(FToken(ETokenType::Syntax, MidLine.Range));
						OutTokenizedLines.Add(MidLine);
						++NextLineIndex;
					}

					// Now NextLineIndex is either first line that ends after CommentEnd (partial)
					// or == NumLines (comment runs to EOF)
					if (NextLineIndex >= NumLines)
					{
						// comment runs to EOF and we've already added all fully covered lines
						// move to after last added line
						LineIndex = NumLines;
						break; // outer while will end
					}
					else
					{
						// NextLineIndex contains the line where the block comment ends (partial line)
						const FTextRange EndLineRange = LineRanges[NextLineIndex];

						// create a TokenizedLine for the end line and add the comment part
						FTokenizedLine EndLine;
						EndLine.Range = EndLineRange;

						const int32 CommentPartEndInThisLine = FMath::Clamp(CommentEnd, EndLineRange.BeginIndex, EndLineRange.EndIndex);
						EndLine.Tokens.Emplace(FToken(ETokenType::Syntax, FTextRange(EndLineRange.BeginIndex, CommentPartEndInThisLine)));

						// Now we need to continue tokenizing the remainder of this end line starting at CommentPartEndInThisLine
						int32 SavedLineIndex = NextLineIndex;
						int32 SavedCurrentOffset = CommentPartEndInThisLine;

						// Tokenize the remainder of the end line (reuse much of the original logic)
						int32 TempOffset = SavedCurrentOffset;
						while (TempOffset < EndLineRange.EndIndex)
						{
							const TCHAR* TempString = &Input[TempOffset];
							const TCHAR TempChar = Input[TempOffset];

							// Greedy matching for operators
							bool bHasMatchedSyntax = false;
							for (const FString& Operator : Operators)
							{
								if (FCString::Strncmp(TempString, *Operator, Operator.Len()) == 0)
								{
									const int32 SyntaxTokenEnd = TempOffset + Operator.Len();
									EndLine.Tokens.Emplace(FToken(ETokenType::Syntax, FTextRange(TempOffset, SyntaxTokenEnd)));
									check(SyntaxTokenEnd <= EndLineRange.EndIndex);
									bHasMatchedSyntax = true;
									TempOffset = SyntaxTokenEnd;
									break;
								}
							}
							if (bHasMatchedSyntax)
							{
								continue;
							}

							int32 PeekOffset = TempOffset + 1;
							if (TempChar == TEXT('#'))
							{
								while (PeekOffset < EndLineRange.EndIndex)
								{
									const TCHAR PeekChar = Input[PeekOffset];
									if (!IsAlpha(PeekChar))
									{
										break;
									}
									PeekOffset++;
								}
							}
							else if (IsAlpha(TempChar))
							{
								while (PeekOffset < EndLineRange.EndIndex)
								{
									const TCHAR PeekChar = Input[PeekOffset];
									if (!IsAlphaOrDigit(PeekChar))
									{
										break;
									}
									PeekOffset++;
								}
							}

							const int32 CurrentStringLength = PeekOffset - TempOffset;

							// Check if it is an reserved keyword
							for (const FString& Keyword : Keywords)
							{
								if (FCString::Strncmp(TempString, *Keyword, CurrentStringLength) == 0)
								{
									const int32 SyntaxTokenEnd = TempOffset + CurrentStringLength;
									EndLine.Tokens.Emplace(FToken(ETokenType::Syntax, FTextRange(TempOffset, SyntaxTokenEnd)));
									check(SyntaxTokenEnd <= EndLineRange.EndIndex);
									bHasMatchedSyntax = true;
									TempOffset = SyntaxTokenEnd;
									break;
								}
							}
							if (bHasMatchedSyntax)
							{
								continue;
							}

							// If none matched, consume the character(s) as text
							const int32 TextTokenEnd = TempOffset + CurrentStringLength;
							EndLine.Tokens.Emplace(FToken(ETokenType::Literal, FTextRange(TempOffset, TextTokenEnd)));
							TempOffset = TextTokenEnd;
						}

						// add the end line (with comment part + tokenized remainder)
						OutTokenizedLines.Add(EndLine);

						// continue processing after the end line
						LineIndex = SavedLineIndex + 1;
						break; // break the while(CurrentOffset < LineRange.EndIndex) for the original line
					}
				} // end block comment handling

				// If we reached here then no comment started at CurrentOffset (or after handling comment we have moved)
				// Continue original tokenization path:
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
			} // end while CurrentOffset < LineRange.EndIndex

			// If we didn't already add this line (e.g. in multi-line block comment path), add TokenizedLine now.
			// Note: in case of block comment that spanned multiple lines we already added the current TokenizedLine above.
			// We detect that by checking if OutTokenizedLines is empty or last added range != this line's range.
			if (!(OutTokenizedLines.Num() > 0 && OutTokenizedLines.Last().Range == TokenizedLine.Range))
			{
				OutTokenizedLines.Add(TokenizedLine);
				++LineIndex;
			}
			// else: LineIndex was adjusted already in block comment handling
		}
	}
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
			TextBlockStyle.SetColorAndOpacity(FSlateColor(Settings->IncludeColor));
			RunInfo.Name = TEXT("SyntaxHighlight.Include");
		}
		else if (TokenText.StartsWith("//") || TokenText.StartsWith("/*"))
		{
			TextBlockStyle.SetColorAndOpacity(FSlateColor(Settings->CommentsColor));
			RunInfo.Name = TEXT("SyntaxHighlight.Comment");
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
