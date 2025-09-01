/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */


#pragma once

#include "CoreMinimal.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Framework/Text/BaseTextLayoutMarshaller.h"
#include "Framework/Text/ITextDecorator.h"
#include "Framework/Text/SlateTextRun.h"
#include "Components/MultiLineEditableText.h"
#include "Framework/Text/ITextDecorator.h"
#include "Framework/Text/SlateTextRun.h"
#include "Framework/Text/SyntaxHighlighterTextLayoutMarshaller.h"
#include "Framework/Text/SyntaxTokenizer.h"
#include "CustomCodeEditorWidget.generated.h"

class FColoredTextLayoutMarshaller : public FSyntaxHighlighterTextLayoutMarshaller
{
public:
    FColoredTextLayoutMarshaller(TSharedPtr<ISyntaxTokenizer> InTokenizer, FTextBlockStyle NewDefaultStyle);

    static TSharedRef<FColoredTextLayoutMarshaller> Create(FTextBlockStyle NewDefaultStyle)
    {
        return MakeShared<FColoredTextLayoutMarshaller>(CreateTokenizer(), NewDefaultStyle);
    }

    static TSharedPtr<ISyntaxTokenizer> CreateTokenizer();

    virtual void ParseTokens(const FString& SourceString, FTextLayout& TargetTextLayout, TArray<ISyntaxTokenizer::FTokenizedLine> TokenizedLines) override;

    virtual FTextLayout::FNewLineData ProcessTokenizedLine(const ISyntaxTokenizer::FTokenizedLine& TokenizedLine, const int32& LineNumber, const FString& SourceString);

    FTextBlockStyle DefaultStyle;

};



class SColorEditableText : public SMultiLineEditableText
{
public:
    void Construct(const FArguments& InArgs)
    {
        // Create our custom marshaller
        TSharedRef<FColoredTextLayoutMarshaller> Marshaller = FColoredTextLayoutMarshaller::Create(*InArgs._TextStyle);

        // Call the parent constructor with our marshaller
        SMultiLineEditableText::Construct(
            SMultiLineEditableText::FArguments()
            .Text(InArgs._Text)
            .HintText(InArgs._HintText)
            .SearchText(InArgs._SearchText)
            .Marshaller(Marshaller)
            .WrapTextAt(InArgs._WrapTextAt)
            .AutoWrapText(InArgs._AutoWrapText)
            .WrappingPolicy(InArgs._WrappingPolicy)
            .TextStyle(InArgs._TextStyle)
            .Font(InArgs._Font)
            .Margin(InArgs._Margin)
            .LineHeightPercentage(InArgs._LineHeightPercentage)
            .ApplyLineHeightToBottomLine(InArgs._ApplyLineHeightToBottomLine)
            .Justification(InArgs._Justification)
            .IsReadOnly(InArgs._IsReadOnly)
            .OnTextChanged(InArgs._OnTextChanged)
            .OnTextCommitted(InArgs._OnTextCommitted)
            .AllowMultiLine(InArgs._AllowMultiLine)
            .SelectAllTextWhenFocused(InArgs._SelectAllTextWhenFocused)
            .SelectWordOnMouseDoubleClick(InArgs._SelectWordOnMouseDoubleClick)
            .ClearTextSelectionOnFocusLoss(InArgs._ClearTextSelectionOnFocusLoss)
            .RevertTextOnEscape(InArgs._RevertTextOnEscape)
            .ClearKeyboardFocusOnCommit(InArgs._ClearKeyboardFocusOnCommit)
            .AllowContextMenu(InArgs._AllowContextMenu)
            .OnCursorMoved(InArgs._OnCursorMoved)
            .ContextMenuExtender(InArgs._ContextMenuExtender)
            .ModiferKeyForNewLine(InArgs._ModiferKeyForNewLine)
            .VirtualKeyboardOptions(InArgs._VirtualKeyboardOptions)
            .VirtualKeyboardTrigger(InArgs._VirtualKeyboardTrigger)
            .VirtualKeyboardDismissAction(InArgs._VirtualKeyboardDismissAction)
            .TextShapingMethod(InArgs._TextShapingMethod)
            .TextFlowDirection(InArgs._TextFlowDirection)
            .OverflowPolicy(InArgs._OverflowPolicy)
            .OnKeyCharHandler(InArgs._OnKeyCharHandler)
        );
    }
};



UCLASS(meta = (DisplayName = "Color Editable Text"))
class BLUEPRINTNATIVIZATIONV2_API UColorEditableText : public UMultiLineEditableText
{
    GENERATED_BODY()

public:
    UColorEditableText(const FObjectInitializer& ObjectInitializer);

    // Rebuilds the Slate widget
    virtual TSharedRef<SWidget> RebuildWidget() override;

    // Synchronizes properties like text, style, etc.
    virtual void SynchronizeProperties() override;

    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

    UFUNCTION(BlueprintCallable, Category = "Widget", meta = (DisplayName = "SetColorEditableText "))
    void SetColorEditableText(FText InText);

private:
    TSharedPtr<SColorEditableText> MyColorEditableText;
};
