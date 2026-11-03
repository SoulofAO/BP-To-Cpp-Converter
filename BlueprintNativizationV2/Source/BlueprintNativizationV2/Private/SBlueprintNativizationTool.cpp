/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#include "SBlueprintNativizationTool.h"
#include "EngineAssetPolicy.h"
#include "BlueprintNativeApiCacheLibrary.h"
#include "NativeCacheTask.h"
#include "NativeModuleCache.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "InputCoreTypes.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Widgets/Notifications/SProgressBar.h"

#include "BlueprintNativizationToolSession.h"
#include "BlueprintNativizationSettings.h"
#include "CustomCodeEditorWidget.h"
#include "ContentBrowserModule.h"
#include "ContentBrowserDataSubsystem.h"
#include "Editor.h"
#include "GameProjectGenerationModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/BlueprintSupport.h"
#include "AssetThumbnail.h"
#include "IContentBrowserSingleton.h"
#include "IDetailsView.h"
#include "PropertyEditorModule.h"
#include "Styling/AppStyle.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"
#include "Misc/MessageDialog.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Engine/Blueprint.h"
#include "Engine/UserDefinedEnum.h"
#include "StructUtils/UserDefinedStruct.h"
#include "ISettingsModule.h"
#include "UObject/UnrealType.h"

static FARFilter MakeContentFilter(const FString& Path);

static TSharedRef<SWidget> MakeWorkspacePanel(const TSharedRef<SWidget>& Content)
{
    TSharedPtr<SBorder> Surface;
    SAssignNew(Surface, SBorder).BorderImage(FAppStyle::GetBrush(TEXT("WhiteBrush")))
        .BorderBackgroundColor(FLinearColor(0.025f, 0.025f, 0.025f)).Padding(4)[Content];
    TSharedPtr<SBorder> Outline;
    SAssignNew(Outline, SBorder).BorderImage(FAppStyle::GetBrush(TEXT("WhiteBrush")))
        .BorderBackgroundColor(FLinearColor(0.16f, 0.16f, 0.16f)).Padding(1)[Surface.ToSharedRef()];
    return Outline.ToSharedRef();
}

static TSharedRef<SWidget> MakeAnalysisPanel(const FString& Title, TAttribute<FString> Report, TAttribute<bool> IsReady = false,
    TSharedPtr<SWidget> Action = nullptr)
{
    TSharedPtr<STextBlock> Heading;
    SAssignNew(Heading, STextBlock).Text_Lambda([Title, IsReady]()
        {
            if (IsReady.Get())
            {
                return FText::FromString(FString::Format(TEXT("\u2713  {0}"), { Title }));
            }
            return FText::FromString(Title);
        })
        .ColorAndOpacity_Lambda([IsReady]()
        {
            if (IsReady.Get())
            {
                return FSlateColor(FLinearColor(0.15f, 0.8f, 0.3f));
            }
            return FSlateColor::UseForeground();
        })
        .Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")));
    TSharedPtr<STextBlock> Summary;
    SAssignNew(Summary, STextBlock).AutoWrapText(true).Text_Lambda([Report]()
    {
        FString Text = Report.Get();
        int32 Separator = Text.Find(TEXT("\n\n"));
        if (Separator != INDEX_NONE)
        {
            Text = Text.Left(Separator);
        }
        return FText::FromString(Text);
    });
    TSharedPtr<STextBlock> DetailText;
    SAssignNew(DetailText, STextBlock).AutoWrapText(true).Text_Lambda([Report]()
    {
        const FString Text = Report.Get();
        const int32 Separator = Text.Find(TEXT("\n\n"));
        if (Separator != INDEX_NONE)
        {
            return FText::FromString(Text.Mid(Separator + 2));
        }
        return FText::GetEmpty();
    });
    TSharedPtr<STextBlock> DetailLabel;
    SAssignNew(DetailLabel, STextBlock).Text(FText::FromString(TEXT("Details")));
    TSharedPtr<SExpandableArea> DetailsArea;
    SAssignNew(DetailsArea, SExpandableArea).InitiallyCollapsed(true)
        .Visibility_Lambda([Report]()
        {
            if (Report.Get().Contains(TEXT("\n\n")))
            {
                return EVisibility::Visible;
            }
            return EVisibility::Collapsed;
        })
        .HeaderContent()[DetailLabel.ToSharedRef()]
        .BodyContent()[DetailText.ToSharedRef()];

    TSharedPtr<SVerticalBox> Panel;
    SAssignNew(Panel, SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(12, 10)[Heading.ToSharedRef()]
        + SVerticalBox::Slot().AutoHeight().Padding(12, 4, 12, 12)[Summary.ToSharedRef()]
        + SVerticalBox::Slot().AutoHeight().Padding(12, 0, 12, 8)[DetailsArea.ToSharedRef()];
    if (Action.IsValid())
    {
        Panel->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(12, 0, 12, 12)[Action.ToSharedRef()];
    }
    return MakeWorkspacePanel(Panel.ToSharedRef());
}

SBlueprintNativizationTool::~SBlueprintNativizationTool()
{
    Session->OperationProgress = nullptr;
    GConfig->SetFloat(TEXT("BlueprintNativizationWorkspace"), TEXT("ContentHeight"), ContentHeight, GEditorPerProjectIni);
    GConfig->SetFloat(TEXT("BlueprintNativizationWorkspace"), TEXT("WorkspaceHeight"), WorkspaceHeight, GEditorPerProjectIni);
    GConfig->SetFloat(TEXT("BlueprintNativizationWorkspace"), TEXT("AssetWidth"), AssetWidth, GEditorPerProjectIni);
    GConfig->SetFloat(TEXT("BlueprintNativizationWorkspace"), TEXT("SourceWidth"), SourceWidth, GEditorPerProjectIni);
    GConfig->SetFloat(TEXT("BlueprintNativizationWorkspace"), TEXT("SettingsWidth"), SettingsWidth, GEditorPerProjectIni);
    GConfig->SetInt(TEXT("BlueprintNativizationWorkspace"), TEXT("FontSize"), FontSize, GEditorPerProjectIni);
}

TSharedRef<SWidget> SBlueprintNativizationTool::MakeText(const FString& Text)
{
    TSharedPtr<STextBlock> Label;
    SAssignNew(Label, STextBlock).Text(FText::FromString(Text)).AutoWrapText(true);
    return Label.ToSharedRef();
}

void SBlueprintNativizationTool::SetPreviewFontSize(int32 NewFontSize)
{
    const int32 ClampedFontSize = FMath::Clamp(NewFontSize, 6, 24);
    if (FontSize == ClampedFontSize)
    {
        return;
    }
    FontSize = ClampedFontSize;
    CodeEditor->SetCodeFont(FCoreStyle::GetDefaultFontStyle(TEXT("Mono"), FontSize));
    LineNumbers->SetCodeFont(FCoreStyle::GetDefaultFontStyle(TEXT("Mono"), FontSize));
    GConfig->SetInt(TEXT("BlueprintNativizationWorkspace"), TEXT("FontSize"), FontSize, GEditorPerProjectIni);
    GConfig->Flush(false, GEditorPerProjectIni);
}

void SBlueprintNativizationTool::Construct(const FArguments& Arguments)
{
    PreparationButtonStyle = FAppStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Button"));
    PreparationButtonStyle.SetNormal(FSlateRoundedBoxBrush(FLinearColor(0.9f, 0.9f, 0.9f), 3.0f));
    PreparationButtonStyle.SetHovered(FSlateRoundedBoxBrush(FLinearColor::White, 3.0f));
    PreparationButtonStyle.SetPressed(FSlateRoundedBoxBrush(FLinearColor(0.7f, 0.7f, 0.7f), 3.0f));
    Session.Reset(NewObject<UBlueprintNativizationToolSession>());
    Session->LoadSetup();
    Session->OperationProgress = [this](int32 Completed, int32 Total, const FString& Detail)
    {
        return UpdateOperationProgress(Completed, Total, Detail);
    };
    Session->Status = TEXT("Select Blueprints, structures or enums from the Content Browser.");
    GConfig->GetFloat(TEXT("BlueprintNativizationWorkspace"), TEXT("ContentHeight"), ContentHeight, GEditorPerProjectIni);
    GConfig->GetFloat(TEXT("BlueprintNativizationWorkspace"), TEXT("WorkspaceHeight"), WorkspaceHeight, GEditorPerProjectIni);
    GConfig->GetFloat(TEXT("BlueprintNativizationWorkspace"), TEXT("AssetWidth"), AssetWidth, GEditorPerProjectIni);
    GConfig->GetFloat(TEXT("BlueprintNativizationWorkspace"), TEXT("SourceWidth"), SourceWidth, GEditorPerProjectIni);
    GConfig->GetFloat(TEXT("BlueprintNativizationWorkspace"), TEXT("SettingsWidth"), SettingsWidth, GEditorPerProjectIni);
    GConfig->GetInt(TEXT("BlueprintNativizationWorkspace"), TEXT("FontSize"), FontSize, GEditorPerProjectIni);
    FontSize = FMath::Clamp(FontSize, 6, 24);

    FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
    FDetailsViewArgs DetailsArguments;
    DetailsArguments.bHideSelectionTip = true;
    DetailsArguments.bAllowSearch = false;
    DetailsArguments.NameAreaSettings = FDetailsViewArgs::HideNameArea;
    Details = PropertyEditor.CreateDetailView(DetailsArguments);
    Details->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateLambda([this](const FPropertyAndParent& Property)
    {
        const FString Category = Property.Property.GetMetaData(TEXT("Category"));
        if (Property.Property.GetFName() == GET_MEMBER_NAME_CHECKED(UBlueprintNativizationToolSession, ModuleNameOverride))
        {
            return false;
        }
        if (Property.Property.GetFName() == GET_MEMBER_NAME_CHECKED(UBlueprintNativizationToolSession, bContinueOnError))
        {
            return Mode == EToolMode::Setup || Mode == EToolMode::Migration;
        }
        return (Mode == EToolMode::Setup && Category == TEXT("Output")) ||
            (Mode == EToolMode::Migration && Category == TEXT("Apply"));
    }));
    Details->SetObject(Session.Get());
    Details->OnFinishedChangingProperties().AddLambda([this](const FPropertyChangedEvent&)
    {
        Session->RemoveExcludedTargets();
        Session->SaveSetup();
        RefreshContentClassFilter();
        FARFilter Filter = MakeContentFilter(ContentPath);
        Filter.PackagePaths = { GEditor->GetEditorSubsystem<UContentBrowserDataSubsystem>()->ConvertInternalPathToVirtual(FName(*ContentPath)) };
        ContentFilter.ExecuteIfBound(Filter);
        RefreshFunctions();
        RebuildNavigation();
    });
    Details->SetIsPropertyEditingEnabledDelegate(FIsPropertyEditingEnabled::CreateLambda([this]()
    {
        return !Session->IsBusy();
    }));

    TSharedPtr<STextBlock> FunctionLabel;
    SAssignNew(FunctionLabel, STextBlock).Text_Lambda([this]()
    {
        if (Session->FunctionName.IsEmpty())
        {
            return FText::FromString(TEXT("Select a function"));
        }
        return FText::FromString(Session->FunctionName);
    });
    SAssignNew(FunctionSelector, SComboBox<TSharedPtr<FString>>)
        .OptionsSource(&Functions)
        .OnGenerateWidget(this, &SBlueprintNativizationTool::MakeOption)
        .IsEnabled_Lambda([this]() { return !Session->IsBusy() && Session->bGenerateOneFunction; })
        .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Choice, ESelectInfo::Type SelectionType)
        {
            if (Choice.IsValid())
            {
                Session->FunctionName = *Choice;
            }
        })
        [FunctionLabel.ToSharedRef()];

    SAssignNew(DocumentList, SListView<TSharedPtr<FString>>)
        .ListItemsSource(&FilteredDocuments)
        .Clipping(EWidgetClipping::ClipToBounds)
        .SelectionMode(ESelectionMode::Single)
        .OnGenerateRow(this, &SBlueprintNativizationTool::MakeDocumentRow)
        .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Document, ESelectInfo::Type SelectionType)
        {
            if (Document.IsValid() && SelectionType != ESelectInfo::Direct)
            {
                SelectDocument(Documents.IndexOfByKey(Document));
            }
        });
    DocumentList->SetTag(TEXT("PreviewDocumentList"));
    SAssignNew(AssetPanel, SVerticalBox);
    SAssignNew(ModePanel, SVerticalBox);
    TSharedPtr<SScrollBar> CodeVerticalScroll;
    SAssignNew(CodeVerticalScroll, SScrollBar).Orientation(Orient_Vertical);
    TSharedPtr<SScrollBar> CodeHorizontalScroll;
    SAssignNew(CodeHorizontalScroll, SScrollBar).Orientation(Orient_Horizontal);
    SAssignNew(LineNumbers, SColorEditableText)
        .TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("NormalText")))
        .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Mono"), FontSize))
        .IsReadOnly(true).AutoWrapText(false);
    SAssignNew(CodeEditor, SColorEditableText)
        .VScrollBar(CodeVerticalScroll)
        .HScrollBar(CodeHorizontalScroll)
        .TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("NormalText")))
        .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Mono"), FontSize))
        .AutoWrapText(false)
        .IsReadOnly_Lambda([this]() { return SelectedDocument == INDEX_NONE || Session->IsBusy(); })
        .OnCursorMoved_Lambda([this](const FTextLocation& Location)
        {
            CursorLine = Location.GetLineIndex() + 1;
            CursorColumn = Location.GetOffset() + 1;
        })
        .OnVScrollBarUserScrolled_Lambda([this](float Offset)
        {
            LineNumbers->SynchronizeVerticalScroll(Offset);
        })
        .OnTextChanged_Lambda([this](const FText& Text)
        {
            if (!bUpdatingEditor)
            {
                Session->SetDocumentText(SelectedDocument, Text.ToString());
                UpdateLineNumbers();
            }
        });

    CodeEditor->MouseWheelHandler = [this](const FGeometry&, const FPointerEvent& MouseEvent)
    {
        if (MouseEvent.IsControlDown())
        {
            if (MouseEvent.GetWheelDelta() > 0.0f)
            {
                SetPreviewFontSize(FontSize + 1);
            }
            else if (MouseEvent.GetWheelDelta() < 0.0f)
            {
                SetPreviewFontSize(FontSize - 1);
            }
            return FReply::Handled();
        }
        return FReply::Unhandled();
    };
    LineNumbers->MouseWheelHandler = [this](const FGeometry&, const FPointerEvent& MouseEvent)
    {
        return CodeEditor->OnMouseWheel(CodeEditor->GetCachedGeometry(), MouseEvent);
    };
    CodeEditor->SetTag(TEXT("SourceEditor"));
    CodeEditor->UseMutedSyntaxColors();
    TSharedPtr<SSearchBox> Search;
    SAssignNew(Search, SSearchBox).HintText(FText::FromString(TEXT("Find in document")))
        .OnTextChanged_Lambda([this](const FText& Text) { CodeEditor->SetSearchText(Text); })
        .OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type Commit)
        {
            if (Commit == ETextCommit::OnEnter)
            {
                CodeEditor->AdvanceSearch();
            }
        });
    TSharedPtr<SSpinBox<int32>> FontControl;
    SAssignNew(FontControl, SSpinBox<int32>).MinValue(6).MaxValue(24)
        .Value_Lambda([this]() { return FontSize; })
        .OnValueChanged_Lambda([this](int32 Size)
        {
            SetPreviewFontSize(Size);
        });
    TSharedPtr<SHorizontalBox> CodeToolbar;
    SAssignNew(CodeToolbar, SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(1)[Search.ToSharedRef()]
        + SHorizontalBox::Slot().AutoWidth().Padding(4, 0)[MakeAction(TEXT("Next"), FSimpleDelegate::CreateLambda([this]() { CodeEditor->AdvanceSearch(); }))];
    TSharedPtr<SBox> NumberColumn;
    SAssignNew(NumberColumn, SBox).WidthOverride(54)[LineNumbers.ToSharedRef()];
    TSharedPtr<SHorizontalBox> EditorRow;
    SAssignNew(EditorRow, SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth()[NumberColumn.ToSharedRef()]
        + SHorizontalBox::Slot().FillWidth(1)[CodeEditor.ToSharedRef()]
        + SHorizontalBox::Slot().AutoWidth()[CodeVerticalScroll.ToSharedRef()];
    TSharedPtr<SVerticalBox> ScrollableEditor;
    SAssignNew(ScrollableEditor, SVerticalBox)
        + SVerticalBox::Slot().FillHeight(1)[EditorRow.ToSharedRef()]
        + SVerticalBox::Slot().AutoHeight()[CodeHorizontalScroll.ToSharedRef()];
    TSharedPtr<STextBlock> PreviewState;
    SAssignNew(PreviewState, STextBlock).AutoWrapText(true).Text_Lambda([this]()
    {
        if (Session->HasPartialResult())
        {
            return FText::FromString(TEXT("Partial preview: failed objects and their dependents were excluded. Review the report before exporting."));
        }
        if (Session->HasGenerationErrors())
        {
            return FText::FromString(TEXT("Generation failed. Diagnostic source may be incomplete or invalid. Review the error log before exporting."));
        }
        if (!Session->HasResult())
        {
            return FText::FromString(TEXT("No preview yet. Choose input assets, then Generate preview."));
        }
        if (Session->IsPreviewStale())
        {
            return FText::FromString(TEXT("Inputs changed. Regenerate before export."));
        }
        if (Session->HasUnsavedChanges())
        {
            return FText::FromString(TEXT("Unsaved preview"));
        }
        return FText::FromString(TEXT("Saved"));
    });
        TSharedPtr<SCheckBox> Scope;
        SAssignNew(Scope, SCheckBox)
            .IsChecked_Lambda([this]()
            {
                if (Session->bGenerateOneFunction)
                {
                    return ECheckBoxState::Checked;
                }
                return ECheckBoxState::Unchecked;
            })
            .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { Session->bGenerateOneFunction = State == ECheckBoxState::Checked; })
            [MakeText(TEXT("Selected function only"))];


    TSharedPtr<SHorizontalBox> FontRow;
    SAssignNew(FontRow, SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[MakeText(TEXT("Font Size"))]
        + SHorizontalBox::Slot().AutoWidth().Padding(12, 0)[FontControl.ToSharedRef()];
    TSharedPtr<SVerticalBox> EditorSettings;
    SAssignNew(EditorSettings, SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(12, 10, 12, 6)[Scope.ToSharedRef()]
        + SVerticalBox::Slot().AutoHeight().Padding(12, 4, 12, 10)[FunctionSelector.ToSharedRef()]
        + SVerticalBox::Slot().AutoHeight().Padding(12, 6, 12, 12)[FontRow.ToSharedRef()];
    TSharedPtr<SBox> SettingsMenu;
    SAssignNew(SettingsMenu, SBox).WidthOverride(280)[EditorSettings.ToSharedRef()];
    TSharedPtr<SImage> SettingsIcon;
    SAssignNew(SettingsIcon, SImage).Image(FAppStyle::GetBrush(TEXT("Icons.Settings")));
    TSharedPtr<SComboButton> SettingsButton;
    SAssignNew(SettingsButton, SComboButton).HasDownArrow(false)
        .ToolTipText(FText::FromString(TEXT("Preview settings")))
        .ButtonContent()[SettingsIcon.ToSharedRef()]
        .MenuContent()[SettingsMenu.ToSharedRef()];
    SettingsButton->SetTag(TEXT("PreviewSettings"));
    CodeToolbar->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)[SettingsButton.ToSharedRef()];
    TSharedPtr<STextBlock> DocumentHeader;
    SAssignNew(DocumentHeader, STextBlock).Text_Lambda([this]()
    {
        if (Documents.IsValidIndex(SelectedDocument))
        {
            FString Name = *Documents[SelectedDocument];
            if (Session->IsDocumentModified(SelectedDocument))
            {
                Name += TEXT(" *");
            }
            return FText::FromString(Name);
        }
        return FText::FromString(TEXT("Preview"));
    });
    TSharedPtr<SSearchBox> DocumentSearchInput;
    SAssignNew(DocumentSearchInput, SSearchBox).HintText(FText::FromString(TEXT("Search files")))
        .OnTextChanged_Lambda([this](const FText& Text)
        {
            DocumentSearch = Text.ToString();
            FilterDocuments();
        });
    TSharedPtr<STextBlock> DocumentCount;
    SAssignNew(DocumentCount, STextBlock).Text_Lambda([this]()
    {
        return FText::FromString(FString::Format(TEXT("Files ({0} / {1})"), { FilteredDocuments.Num(), Documents.Num() }));
    });
    TSharedPtr<SVerticalBox> DocumentNavigation;
    SAssignNew(DocumentNavigation, SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(4)[DocumentCount.ToSharedRef()]
        + SVerticalBox::Slot().AutoHeight().Padding(4)[DocumentSearchInput.ToSharedRef()]
        + SVerticalBox::Slot().FillHeight(1).Padding(4)[DocumentList.ToSharedRef()];
    TSharedPtr<SSplitter> PreviewSplitter;
    SAssignNew(PreviewSplitter, SSplitter).PhysicalSplitterHandleSize(5).HitDetectionSplitterHandleSize(9)
        .Clipping(EWidgetClipping::ClipToBounds)
        + SSplitter::Slot().Value(0.25f).MinSize(180)[DocumentNavigation.ToSharedRef()]
        + SSplitter::Slot().Value(0.75f).MinSize(220)[ScrollableEditor.ToSharedRef()];
    TSharedPtr<SVerticalBox> SourcePanel;
    SAssignNew(SourcePanel, SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(8)[DocumentHeader.ToSharedRef()]
        + SVerticalBox::Slot().AutoHeight().Padding(8, 4)[CodeToolbar.ToSharedRef()]
        + SVerticalBox::Slot().AutoHeight().Padding(8)[PreviewState.ToSharedRef()]
        + SVerticalBox::Slot().FillHeight(1).Padding(8)[PreviewSplitter.ToSharedRef()];

    TSharedPtr<SScrollBox> NavigationScroll;
    SAssignNew(NavigationScroll, SScrollBox) + SScrollBox::Slot()[AssetPanel.ToSharedRef()];
    TSharedPtr<SButton> CollectDependencies;
    SAssignNew(CollectDependencies, SButton).ButtonStyle(FAppStyle::Get(), TEXT("PrimaryButton"))
        .ContentPadding(FMargin(16, 8)).IsEnabled_Lambda([this]() { return !Session->IsBusy() && !Session->Targets.IsEmpty(); })
        .OnClicked_Lambda([this]()
        {
            RunOperation(TEXT("Collect dependencies"), FSimpleDelegate::CreateLambda([this]()
            {
                Session->GetDependencies();
                RefreshFunctions();
                RebuildNavigation();
                if (!bOperationCancellationRequested)
                {
                    RecordStatus();
                }
            }), true);
            return FReply::Handled();
        })[MakeText(TEXT("Get Dependencies"))];
    CollectDependencies->SetTag(TEXT("Get Dependencies"));
    TSharedPtr<SSearchBox> AssetSearch;
    SAssignNew(AssetSearch, SSearchBox)
        .HintText(FText::FromString(TEXT("Search objects by name")))
        .ToolTipText(FText::FromString(TEXT("Filter the selected objects by name. The generation list remains unchanged.")))
        .IsEnabled_Lambda([this]() { return !Session->IsBusy() && !bOperationRunning; })
        .OnTextChanged_Lambda([this](const FText& Text)
        {
            AssetNameSearch = Text.ToString();
            RebuildNavigation();
        });
    AssetSearch->SetTag(TEXT("SelectedAssetSearch"));
    TSharedPtr<SVerticalBox> NavigationPanel;
    SAssignNew(NavigationPanel, SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(12, 10, 12, 4)[AssetSearch.ToSharedRef()]
        + SVerticalBox::Slot().FillHeight(1)[NavigationScroll.ToSharedRef()]
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(12, 14, 12, 6)[CollectDependencies.ToSharedRef()]
        + SVerticalBox::Slot().AutoHeight().Padding(12, 4)[MakeText(TEXT("Collect dependencies here, then review the list. Generate does not add dependencies."))];
    TSharedPtr<SSplitter> AssetSplitter;
    SAssignNew(AssetSplitter, SSplitter).PhysicalSplitterHandleSize(5).HitDetectionSplitterHandleSize(9)
        + SSplitter::Slot().Value(AssetWidth).MinSize(250).OnSlotResized_Lambda([this](float Value) { AssetWidth = Value; })[MakeWorkspacePanel(MakeContentBrowser())]
        + SSplitter::Slot().Value(1.0f - AssetWidth).MinSize(220)[MakeWorkspacePanel(NavigationPanel.ToSharedRef())];
    AssetsWorkspace = AssetSplitter;
    PreviewWorkspace = MakeWorkspacePanel(SourcePanel.ToSharedRef());
    SAssignNew(StageBody, SBox);

    TSharedPtr<SHorizontalBox> Toolbar;
    SAssignNew(Toolbar, SHorizontalBox);
    const TArray<FString> ModeNames = { TEXT("Setup"), TEXT("Assets"), TEXT("Preview"), TEXT("Export"), TEXT("Migration"), TEXT("Tools") };
    for (int32 ModeIndex = 0; ModeIndex < ModeNames.Num(); ++ModeIndex)
    {
        const EToolMode ButtonMode = static_cast<EToolMode>(ModeIndex);
        TSharedPtr<SCheckBox> ModeButton;
        SAssignNew(ModeButton, SCheckBox)
            .IsEnabled_Lambda([this]() { return !bOperationRunning; })
            .ForegroundColor_Lambda([this, ButtonMode]()
            {
                if (ButtonMode != EToolMode::Setup &&
                    (!ReadySections.Contains(TEXT("Module")) || !Session->IsPreparedModuleLoaded()))
                {
                    return FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f));
                }
                return FSlateColor::UseForeground();
            })
            .Style(&FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>(TEXT("ToggleButtonCheckbox")))
            .IsChecked_Lambda([this, ButtonMode]()
            {
                if (Mode == ButtonMode)
                {
                    return ECheckBoxState::Checked;
                }
                return ECheckBoxState::Unchecked;
            })
            .OnCheckStateChanged_Lambda([this, ButtonMode](ECheckBoxState State) { SetMode(ButtonMode); })
            [MakeText(ModeNames[ModeIndex])];
        ModeButton->SetTag(FName(*ModeNames[ModeIndex]));
        if (ButtonMode == EToolMode::Tools)
        {
            Toolbar->AddSlot().FillWidth(1)[MakeText(TEXT(""))];
        }
        Toolbar->AddSlot().AutoWidth().Padding(4)[ModeButton.ToSharedRef()];
    }
    TSharedPtr<STextBlock> PrimaryLabel;
    SAssignNew(PrimaryLabel, STextBlock).Text_Lambda([this]()
    {
        if (Mode == EToolMode::Export)
        {
            return FText::FromString(TEXT("Export"));
        }
        if (Mode == EToolMode::Migration)
        {
            return FText::FromString(TEXT("Review migration"));
        }
        if (Mode == EToolMode::Setup)
        {
            return FText::FromString(TEXT("Select assets"));
        }
        if (Mode == EToolMode::Assets)
        {
            return FText::FromString(TEXT("Continue to Preview"));
        }
        return FText::FromString(TEXT("GENERATE"));
    });
    SAssignNew(PrimaryAction, SButton).HAlign(HAlign_Center).ContentPadding(FMargin(12, 8)).ButtonStyle(FAppStyle::Get(), TEXT("PrimaryButton"))
        .ButtonColorAndOpacity_Lambda([this]()
        {
            if (!ReadySections.Contains(TEXT("Module")) || !Session->IsPreparedModuleLoaded())
            {
                return FSlateColor(FLinearColor(0.055f, 0.055f, 0.055f));
            }
            if (Mode == EToolMode::Setup)
            {
                if (IsSetupReady())
                {
                    return FSlateColor(FLinearColor(0.12f, 0.48f, 0.9f));
                }
                return FSlateColor(FLinearColor(0.055f, 0.055f, 0.055f));
            }
            return FSlateColor(FLinearColor::White);
        })
        .ForegroundColor_Lambda([this]()
        {
            if (!ReadySections.Contains(TEXT("Module")) || !Session->IsPreparedModuleLoaded())
            {
                return FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f));
            }
            if (Mode == EToolMode::Setup && IsSetupReady())
            {
                return FSlateColor(FLinearColor(0.01f, 0.01f, 0.01f));
            }
            return FSlateColor::UseForeground();
        })
        .IsEnabled_Lambda([this]()
        {
            if (Session->IsBusy() || bOperationRunning)
            {
                return false;
            }
            if (FNativeCacheTask::IsRunning() && Mode != EToolMode::Setup && Mode != EToolMode::Assets)
            {
                return false;
            }
            if (Mode == EToolMode::Preview)
            {
                return !Session->Targets.IsEmpty();
            }
            if (Mode == EToolMode::Export)
            {
                FString Reason;
                return Session->CanExport(Reason);
            }
            if (Mode == EToolMode::Migration)
            {
                return !Session->MigrationCacheFile.FilePath.IsEmpty();
            }
            return true;
        })
        .OnClicked_Lambda([this]()
        {
            if (Mode == EToolMode::Preview)
            {
                RunOperation(TEXT("Generate preview"), FSimpleDelegate::CreateSP(this, &SBlueprintNativizationTool::Generate), !Session->bGenerateOneFunction);
            }
            else if (Mode == EToolMode::Export)
            {
                RunOperation(TEXT("Export source"), FSimpleDelegate::CreateSP(this, &SBlueprintNativizationTool::Save));
            }
            else if (Mode == EToolMode::Migration)
            {
                RunOperation(TEXT("Migrate assets"), FSimpleDelegate::CreateSP(this, &SBlueprintNativizationTool::ApplyCache));
            }
            else if (Mode == EToolMode::Setup)
            {
                SetMode(EToolMode::Assets);
            }
            else if (Mode == EToolMode::Assets)
            {
                SetMode(EToolMode::Preview);
            }
            return FReply::Handled();
        })
        .Visibility_Lambda([this]()
        {
            if (Mode == EToolMode::Tools)
            {
                return EVisibility::Collapsed;
            }
            return EVisibility::Visible;
        })[PrimaryLabel.ToSharedRef()];
    PrimaryAction->SetTag(TEXT("Generate preview"));


    SAssignNew(MessageHistory, SVerticalBox);
    SAssignNew(MessageScroll, SScrollBox) + SScrollBox::Slot()[MessageHistory.ToSharedRef()];
    TSharedPtr<SSearchBox> OutputLogSearchInput;
    SAssignNew(OutputLogSearchInput, SSearchBox)
        .HintText(FText::FromString(TEXT("Search Output Log")))
        .OnTextChanged_Lambda([this](const FText& Text)
        {
            OutputLogSearch = Text.ToString();
            RefreshOutputLog();
            MessageScroll->ScrollToStart();
        });
    OutputLogSearchInput->SetTag(TEXT("OutputLogSearch"));
    TSharedPtr<SHorizontalBox> MessageHeader;
    SAssignNew(MessageHeader, SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[MakeText(TEXT("Output Log"))]
        + SHorizontalBox::Slot().FillWidth(1).Padding(12, 0)[OutputLogSearchInput.ToSharedRef()]
        + SHorizontalBox::Slot().AutoWidth()[MakeAction(TEXT("Clear log"), FSimpleDelegate::CreateLambda([this]()
        {
            OutputLogHistory.Reset();
            RefreshOutputLog();
        }))];
    TSharedPtr<SVerticalBox> MessagesArea;
    SAssignNew(MessagesArea, SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(8)[MessageHeader.ToSharedRef()]
        + SVerticalBox::Slot().FillHeight(1).Padding(8)[MessageScroll.ToSharedRef()];
    TSharedPtr<STextBlock> StageTitle;
    SAssignNew(StageTitle, STextBlock).Tag(TEXT("StageTitle"))
        .Font(FAppStyle::GetFontStyle(TEXT("HeadingExtraSmall"))).Text_Lambda([this]()
    {
        switch (Mode)
        {
        case EToolMode::Setup: return FText::FromString(TEXT("1. Prepare your project"));
        case EToolMode::Assets: return FText::FromString(TEXT("2. Choose what to convert"));
        case EToolMode::Preview: return FText::FromString(TEXT("3. Generate and review C++"));
        case EToolMode::Export: return FText::FromString(TEXT("4. Save the generated code"));
        case EToolMode::Migration: return FText::FromString(TEXT("5. Migrate existing assets (optional)"));
        default: return FText::FromString(TEXT("Additional tools"));
        }
    });
    TSharedPtr<STextBlock> StageDescription;
    SAssignNew(StageDescription, STextBlock).Tag(TEXT("StageDescription")).AutoWrapText(true).Text_Lambda([this]()
    {
        switch (Mode)
        {
        case EToolMode::Setup:
            return FText::FromString(TEXT("Prepare the module and API caches. Settings are saved automatically."));
        case EToolMode::Assets:
            return FText::FromString(TEXT("Choose objects, collect dependencies, then review the final list."));
        case EToolMode::Preview:
            return FText::FromString(TEXT("Generate and edit your C++ preview before saving files."));
        case EToolMode::Export:
            return FText::FromString(TEXT("Review the destination and file changes before exporting."));
        case EToolMode::Migration:
            return FText::FromString(TEXT("After building and restarting, review type mappings before updating existing assets."));
        default:
            return FText::FromString(TEXT("Refresh functions, reset generated names and configure the converter."));
        }
    });
    TSharedPtr<STextBlock> OperationContext;
    SAssignNew(OperationContext, STextBlock).Tag(TEXT("OperationContext")).AutoWrapText(true).Text_Lambda([this]()
    {
        FString PreviewStatus = TEXT("Not generated");
        if (Session->HasResult())
        {
            PreviewStatus = TEXT("Ready for review");
            if (Session->IsPreviewStale())
            {
                PreviewStatus = TEXT("Regeneration required");
            }
            else if (!Session->HasUnsavedChanges())
            {
                PreviewStatus = TEXT("Exported");
            }
        }
        return FText::FromString(FString::Format(TEXT("Module: {0}   |   Objects: {1}   |   Preview: {2}"),
            { Session->GetModuleName(), Session->Targets.Num(), PreviewStatus }));
    });
    TSharedPtr<STextBlock> NextStep;
    SAssignNew(NextStep, STextBlock).Tag(TEXT("NextStep")).AutoWrapText(true)
        .Font_Lambda([this]()
        {
            if (Mode == EToolMode::Export)
            {
                return FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 20);
            }
            return FAppStyle::GetFontStyle(TEXT("NormalFont"));
        })
        .ColorAndOpacity_Lambda([this]()
        {
            if (Mode == EToolMode::Export)
            {
                return FSlateColor(FLinearColor(1.0f, 0.85f, 0.0f));
            }
            return FSlateColor::UseForeground();
        })
        .Text_Lambda([this]()
    {
        if (Mode == EToolMode::Preview && Session->Targets.IsEmpty())
        {
            return FText::FromString(TEXT("Choose objects on the Assets step before generating."));
        }
        if (Mode == EToolMode::Preview && Session->HasResult() && !Session->IsPreviewStale())
        {
            return FText::FromString(TEXT("Review the code, then continue to Export."));
        }
        if (Mode == EToolMode::Export && Session->HasResult() && !Session->HasUnsavedChanges() && !Session->IsPreviewStale())
        {
            return FText::FromString(TEXT("Files saved. Build the project and restart the editor before migration."));
        }
        if (Mode == EToolMode::Migration && Session->MigrationCacheFile.FilePath.IsEmpty())
        {
            return FText::FromString(TEXT("Select a .bpncache file. Enable Save Migration Cache in Setup before export to create one."));
        }
        return FText::GetEmpty();
    });
    NextStep->SetVisibility(TAttribute<EVisibility>::CreateLambda([this, NextStepWidget = NextStep.Get()]()
    {
        if (bOperationRunning || FNativeCacheTask::IsRunning() || NextStepWidget->GetText().IsEmpty())
        {
            return EVisibility::Collapsed;
        }
        return EVisibility::Visible;
    }));
    TSharedPtr<SWidget> BackButton = MakeAction(TEXT("Back"), FSimpleDelegate::CreateLambda([this]()
    {
        SetMode(static_cast<EToolMode>(static_cast<int32>(Mode) - 1));
    }));
    BackButton->SetVisibility(TAttribute<EVisibility>::CreateLambda([this]()
    {
        if (Mode == EToolMode::Setup || Mode == EToolMode::Tools)
        {
            return EVisibility::Collapsed;
        }
        return EVisibility::Visible;
    }));
    TSharedPtr<SWidget> ExportButton = MakeAction(TEXT("Continue to Export"), FSimpleDelegate::CreateLambda([this]() { SetMode(EToolMode::Export); }));
    ExportButton->SetVisibility(TAttribute<EVisibility>::CreateLambda([this]()
    {
        if (Mode == EToolMode::Preview && Session->HasResult() && !Session->IsPreviewStale())
        {
            return EVisibility::Visible;
        }
        return EVisibility::Collapsed;
    }));
    TSharedPtr<SHorizontalBox> ActionBar;
    TSharedPtr<STextBlock> OperationStatus;
    SAssignNew(OperationStatus, STextBlock).AutoWrapText(true).Text_Lambda([this]() { return FText::FromString(OperationLabel); })
        .ToolTipText_Lambda([this]() { return FText::FromString(OperationLabel); });
    TSharedPtr<SProgressBar> OperationProgress;
    SAssignNew(OperationProgress, SProgressBar).Percent_Lambda([this]() { return OperationFraction; });
    TSharedPtr<SVerticalBox> OperationDisplay;
    SAssignNew(OperationDisplay, SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[OperationStatus.ToSharedRef()]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)[OperationProgress.ToSharedRef()];
    TSharedPtr<SButton> CancelOperation;
    SAssignNew(CancelOperation, SButton).IsEnabled_Lambda([this]()
        {
            return FNativeCacheTask::IsRunning() || (bOperationCanCancel && !bOperationCancellationRequested);
        })
        .ToolTipText_Lambda([this]()
        {
            if (bOperationRunning && !bOperationCanCancel)
            {
                return FText::FromString(TEXT("Cancellation is unavailable for this operation."));
            }
            return FText::FromString(TEXT("Cancel at the next safe operation boundary."));
        })
        .OnClicked_Lambda([this]()
        {
            if (FNativeCacheTask::IsRunning())
            {
                FNativeCacheTask::Cancel();
            }
            else
            {
                bOperationCancellationRequested = true;
                OperationLabel = TEXT("Cancellation requested...");
            }
            return FReply::Handled();
        })[MakeText(TEXT("Cancel"))];
    TSharedPtr<SHorizontalBox> OperationBar;
    SAssignNew(OperationBar, SHorizontalBox)
        .Visibility_Lambda([this]()
        {
            if (bOperationRunning || FNativeCacheTask::IsRunning())
            {
                return EVisibility::Visible;
            }
            return EVisibility::Collapsed;
        })
        + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[OperationDisplay.ToSharedRef()]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0)[CancelOperation.ToSharedRef()];
    TSharedPtr<SVerticalBox> ActionStatus;
    SAssignNew(ActionStatus, SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[NextStep.ToSharedRef()]
        + SVerticalBox::Slot().AutoHeight()[OperationBar.ToSharedRef()];
    SAssignNew(ActionBar, SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().Padding(4)[BackButton.ToSharedRef()]
        + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(8)[ActionStatus.ToSharedRef()]
        + SHorizontalBox::Slot().AutoWidth().Padding(4)[ExportButton.ToSharedRef()]
        + SHorizontalBox::Slot().AutoWidth().Padding(4)[PrimaryAction.ToSharedRef()];
    TSharedPtr<SVerticalBox> Workspace;
    SAssignNew(Workspace, SVerticalBox)
        + SVerticalBox::Slot().FillHeight(1)[StageBody.ToSharedRef()]
        + SVerticalBox::Slot().AutoHeight().Padding(6)[ActionBar.ToSharedRef()];
    TSharedPtr<SSplitter> MainSplitter;
    SAssignNew(MainSplitter, SSplitter).Orientation(Orient_Vertical).PhysicalSplitterHandleSize(5).HitDetectionSplitterHandleSize(9)
        + SSplitter::Slot().Value(WorkspaceHeight).MinSize(260).OnSlotResized_Lambda([this](float Value) { WorkspaceHeight = Value; })[Workspace.ToSharedRef()]
        + SSplitter::Slot().Value(1.0f - WorkspaceHeight).MinSize(100)[MakeWorkspacePanel(MessagesArea.ToSharedRef())];
    TSharedPtr<SVerticalBox> Layout;
    SAssignNew(Layout, SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(8)[Toolbar.ToSharedRef()]
        + SVerticalBox::Slot().AutoHeight().Padding(16, 4)[StageTitle.ToSharedRef()]
        + SVerticalBox::Slot().AutoHeight().Padding(16, 4)[StageDescription.ToSharedRef()]
        + SVerticalBox::Slot().AutoHeight().Padding(16, 8)[OperationContext.ToSharedRef()]
        + SVerticalBox::Slot().FillHeight(1)[MainSplitter.ToSharedRef()];
    ChildSlot.Padding(FMargin(12))[Layout.ToSharedRef()];
    SetMode(EToolMode::Setup);
    RebuildNavigation();
}

TSharedRef<SWidget> SBlueprintNativizationTool::MakeAction(const FString& Label, FSimpleDelegate Action, bool bLongOperation)
{
    const FButtonStyle* ButtonStyle = &FAppStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Button"));
    const bool bCustomButtonStyle = IsPreparationAction(Label) || Label == TEXT("Add selected") || Label == TEXT("Add folder");
    if (bCustomButtonStyle)
    {
        ButtonStyle = &PreparationButtonStyle;
    }
    TSharedPtr<SButton> Button;
    SAssignNew(Button, SButton).ButtonStyle(ButtonStyle).ContentPadding(FMargin(10, 6))
        .ButtonColorAndOpacity_Lambda([this, Label, bCustomButtonStyle]()
        {
            if (ShouldHighlightPreparationAction(Label))
            {
                return FSlateColor(FLinearColor(0.12f, 0.48f, 0.9f));
            }
            if (bCustomButtonStyle)
            {
                return FSlateColor(FLinearColor(0.055f, 0.055f, 0.055f));
            }
            return FSlateColor(FLinearColor::White);
        })
        .ForegroundColor_Lambda([this, Label]()
        {
            if (Label != TEXT("Prepare module") && Label != TEXT("Refresh readiness") &&
                (!ReadySections.Contains(TEXT("Module")) || !Session->IsPreparedModuleLoaded()))
            {
                return FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f));
            }
            if (ShouldHighlightPreparationAction(Label))
            {
                return FSlateColor(FLinearColor(0.01f, 0.01f, 0.01f));
            }
            return FSlateColor::UseForeground();
        })
        .IsEnabled_Lambda([this, bLongOperation]() { return !Session->IsBusy() && !bOperationRunning && (!bLongOperation || !FNativeCacheTask::IsRunning()); })
        .OnClicked_Lambda([this, Label, Action, bLongOperation]()
        {
            if (bLongOperation)
            {
                RunOperation(Label, Action, Label == TEXT("Analyze Blueprint Code"));
            }
            else
            {
                Action.ExecuteIfBound();
            }
            return FReply::Handled();
        })[MakeText(Label)];
    Button->SetTag(FName(*Label));
    return Button.ToSharedRef();
}

bool SBlueprintNativizationTool::IsPreparationAction(const FString& Label) const
{
    return Label == TEXT("Prepare module") || Label == TEXT("Build Project API Cache") ||
        Label == TEXT("Build Engine API Cache") || Label == TEXT("Analyze native API") || Label == TEXT("Try Fix project API") || Label == TEXT("Analyze Blueprint Code") || Label == TEXT("TryFix");
}

bool SBlueprintNativizationTool::ShouldHighlightPreparationAction(const FString& Label) const
{
    if (Label != TEXT("Prepare module") &&
        (!ReadySections.Contains(TEXT("Module")) || !Session->IsPreparedModuleLoaded()))
    {
        return false;
    }
    if (Mode == EToolMode::Assets)
    {
        if (Label == TEXT("Add selected"))
        {
            return ContentSelection.IsBound() && !ContentSelection.Execute().IsEmpty();
        }
        if (Label == TEXT("Add folder"))
        {
            return !ContentPath.IsEmpty();
        }
    }
    if (Mode != EToolMode::Setup || !IsPreparationAction(Label))
    {
        return false;
    }
    if (Label == TEXT("Prepare module"))
    {
        return !ReadySections.Contains(TEXT("Module"));
    }
    if (Label == TEXT("Build Project API Cache"))
    {
        return !ReadySections.Contains(TEXT("Project API cache"));
    }
    if (Label == TEXT("Build Engine API Cache"))
    {
        return !ReadySections.Contains(TEXT("Engine API cache"));
    }
    if (Label == TEXT("Analyze native API"))
    {
        return !Session->IsNativeApiReady();
    }
    if (Label == TEXT("Analyze Blueprint Code"))
    {
        return !Session->IsBlueprintCodeAnalysisCurrent();
    }
    if (Label == TEXT("TryFix"))
    {
        return false;
    }
    return Session->CanRepairNativeApi();
}

bool SBlueprintNativizationTool::IsSetupReady() const
{
    return ReadySections.Contains(TEXT("Module")) && Session->IsPreparedModuleLoaded() && ReadySections.Contains(TEXT("Project API cache")) &&
        ReadySections.Contains(TEXT("Engine API cache")) && Session->IsNativeApiReady() &&
        !bOperationRunning && !FNativeCacheTask::IsRunning();
}

void SBlueprintNativizationTool::RunOperation(const FString& Label, FSimpleDelegate Action, bool bCanCancel)
{
    if (bOperationRunning || FNativeCacheTask::IsRunning())
    {
        return;
    }
    TGuardValue<bool> OperationGuard(bOperationRunning, true);
    bOperationCanCancel = bCanCancel;
    bOperationCancellationRequested = false;
    OperationLabel = Label;
    OperationFraction.Reset();
    if (!UpdateOperationProgress(0, 0, Label))
    {
        bOperationCanCancel = false;
        RecordMessage(TEXT("Operation cancelled."), false);
        return;
    }
    Action.ExecuteIfBound();
    if (FNativeCacheTask::IsRunning())
    {
        bCacheTaskWasRunning = true;
        FNativeCacheTask::GetProgress(OperationLabel, OperationFraction);
    }
    else if (Label == TEXT("Build Project API Cache") || Label == TEXT("Build Engine API Cache"))
    {
        const FString Report = FNativeCacheTask::GetStatus();
        const bool bSucceeded = Report.Contains(TEXT(" cache: Succeeded"));
        RecordMessage(Report, !bSucceeded);
    }
    else if (bOperationCancellationRequested)
    {
        RecordMessage(TEXT("Operation cancelled."), false);
    }
    bOperationCanCancel = false;
}

bool SBlueprintNativizationTool::UpdateOperationProgress(int32 Completed, int32 Total, const FString& Detail)
{
    if (!bOperationRunning)
    {
        return true;
    }
    OperationLabel = Detail;
    OperationFraction.Reset();
    if (Total > 0)
    {
        OperationFraction = FMath::Clamp(static_cast<float>(Completed) / Total, 0.0f, 1.0f);
        OperationLabel = FString::Format(TEXT("{0} / {1}: {2}"), { Completed, Total, Detail });
    }
    FPlatformApplicationMisc::PumpMessages(true);
    FSlateApplication::Get().Tick();
    return !bOperationCancellationRequested;
}

TSharedRef<SWidget> SBlueprintNativizationTool::MakeOption(TSharedPtr<FString> Option)
{
    return MakeText(*Option);
}

void SBlueprintNativizationTool::Tick(const FGeometry& Geometry, double CurrentTime, float DeltaTime)
{
    SCompoundWidget::Tick(Geometry, CurrentTime, DeltaTime);
    if (CurrentTime < NextCacheStatusUpdate)
    {
        return;
    }
    NextCacheStatusUpdate = CurrentTime + 1.0;
    const bool bRunning = FNativeCacheTask::IsRunning();
    CacheTaskReport = FNativeCacheTask::GetStatus();
    const FString CurrentCacheValidationStatus = FNativeModuleCache::GetCacheInvalidReason(false) + TEXT("\n") +
        FNativeModuleCache::GetCacheInvalidReason(true);
    if (CacheValidationStatus != CurrentCacheValidationStatus)
    {
        CacheValidationStatus = CurrentCacheValidationStatus;
        PreparationReport = Session->GetPreparationReport(&ReadySections);
    }
    if (bRunning)
    {
        FNativeCacheTask::GetProgress(OperationLabel, OperationFraction);
    }
    if (bCacheTaskWasRunning && !bRunning)
    {
        PreparationReport = Session->GetPreparationReport(&ReadySections);
        RecordMessage(CacheTaskReport, CacheTaskReport.Contains(TEXT(" cache: Failed")));
    }
    bCacheTaskWasRunning = bRunning;
}

TSharedRef<SWidget> SBlueprintNativizationTool::MakeUtilityButtons()
{
    TSharedPtr<SVerticalBox> Menu;
    SAssignNew(Menu, SVerticalBox);
    Menu->AddSlot().AutoHeight().Padding(0, 3)[MakeAction(TEXT("Refresh functions"), FSimpleDelegate::CreateSP(this, &SBlueprintNativizationTool::RefreshFunctions))];
    Menu->AddSlot().AutoHeight().Padding(0, 3)[MakeAction(TEXT("Reset names"), FSimpleDelegate::CreateLambda([this]() { Session->ResetNames(); RecordMessage(Session->Status, false); }))];
    Menu->AddSlot().AutoHeight().Padding(0, 3)[MakeAction(TEXT("Plugin settings"), FSimpleDelegate::CreateLambda([]()
    {
        FModuleManager::LoadModuleChecked<ISettingsModule>(TEXT("Settings"))
            .ShowViewer(TEXT("Editor"), TEXT("Plugins"), TEXT("BlueprintNativizationV2PluginEditorSettings"));
    }))];
    TSharedPtr<SBox> ButtonColumn;
    SAssignNew(ButtonColumn, SBox).WidthOverride(260)[Menu.ToSharedRef()];
    return ButtonColumn.ToSharedRef();
}

void SBlueprintNativizationTool::SetMode(EToolMode NewMode)
{
    StageBody->SetContent(SNullWidget::NullWidget);
    Mode = NewMode;
    if (Mode == EToolMode::Setup)
    {
        PrimaryAction->SetButtonStyle(&PreparationButtonStyle);
    }
    else
    {
        PrimaryAction->SetButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("PrimaryButton")));
    }
    ModePanel->ClearChildren();
    TSharedPtr<SVerticalBox> ReadinessPanel;
    SAssignNew(ReadinessPanel, SVerticalBox);
    TSharedPtr<SWidget> ModuleStatus;
    TSharedPtr<SVerticalBox> ModulePanel;
    if (Mode == EToolMode::Setup)
    {
        SAssignNew(ModulePanel, SVerticalBox);
        ReadinessPanel->AddSlot().AutoHeight().Padding(0, 0, 0, 10)[ModulePanel.ToSharedRef()];
        PreparationReport = Session->GetPreparationReport(&ReadySections);
        const TArray<FString> Sections = { TEXT("Module"), TEXT("Project API cache"), TEXT("Engine API cache") };
        for (const FString& Section : Sections)
        {
            TSharedPtr<SWidget> CacheAction;
            if (Section == TEXT("Project API cache"))
            {
                CacheAction = MakeAction(TEXT("Build Project API Cache"), FSimpleDelegate::CreateLambda([this]()
                {
                    UBlueprintNativeApiCacheLibrary::BuildProjectCache();
                    PreparationReport = Session->GetPreparationReport(&ReadySections);
                }), true);
            }
            else if (Section == TEXT("Engine API cache"))
            {
                CacheAction = MakeAction(TEXT("Build Engine API Cache"), FSimpleDelegate::CreateLambda([this]()
                {
                    UBlueprintNativeApiCacheLibrary::BuildEditorCache();
                    PreparationReport = Session->GetPreparationReport(&ReadySections);
                }), true);
            }
            TSharedRef<SWidget> StatusPanel = MakeAnalysisPanel(Section,
                TAttribute<FString>::CreateLambda([this, Section]()
                {
                    int32 Start = PreparationReport.Find(Section);
                    if (Start == INDEX_NONE)
                    {
                        return FString(TEXT("Refresh readiness to check current settings."));
                    }
                    FString Text = PreparationReport.Mid(Start);
                    int32 End = Text.Find(TEXT("\n\n"));
                    if (End != INDEX_NONE)
                    {
                        Text = Text.Left(End);
                    }
                    int32 FirstLine = Text.Find(TEXT("\n"));
                    if (FirstLine != INDEX_NONE)
                    {
                        Text = Text.Mid(FirstLine + 1);
                    }
                    FirstLine = Text.Find(TEXT("\n"));
                    if (FirstLine != INDEX_NONE)
                    {
                        Text.InsertAt(FirstLine, TEXT("\n"));
                    }
                    return Text;
                }), TAttribute<bool>::CreateLambda([this, Section]()
                {
                    return ReadySections.Contains(Section) && Session->IsPreparedModuleLoaded();
                }), CacheAction);
            if (Section == TEXT("Module"))
            {
                ModuleStatus = StatusPanel;
            }
            else
            {
                ReadinessPanel->AddSlot().AutoHeight().Padding(0, 0, 0, 10)[StatusPanel];
            }
        }
        if (Mode == EToolMode::Setup)
        {
            TSharedPtr<SVerticalBox> NativeApiPanel;
            SAssignNew(NativeApiPanel, SVerticalBox);
            NativeApiPanel->AddSlot().AutoHeight().Padding(12, 10)[MakeText(TEXT("Native C++ API check"))];
            TSharedPtr<STextBlock> NativeApiCheckStatus;
            SAssignNew(NativeApiCheckStatus, STextBlock).AutoWrapText(true)
                .Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))
                .Text_Lambda([this]() { return FText::FromString(Session->GetNativeApiCheckStatus()); })
                .ColorAndOpacity_Lambda([this]()
                {
                    if (Session->IsNativeApiReady())
                    {
                        return FSlateColor(FLinearColor(0.15f, 0.8f, 0.3f));
                    }
                    if (!Session->IsNativeApiChecked() || Session->GetNativeApiReviewIdentifier().IsEmpty())
                    {
                        return FSlateColor(FLinearColor(1.0f, 0.65f, 0.15f));
                    }
                    return FSlateColor(FLinearColor(1.0f, 0.35f, 0.3f));
                });
            NativeApiPanel->AddSlot().AutoHeight().Padding(12, 0, 12, 8)[NativeApiCheckStatus.ToSharedRef()];
            NativeApiPanel->AddSlot().AutoHeight().Padding(12, 0, 12, 8)[MakeText(TEXT("Check that converted C++ can inherit native classes and call their required methods."))];
            TSharedPtr<STextBlock> NativeApiSummary;
            SAssignNew(NativeApiSummary, STextBlock).AutoWrapText(true)
                .Text_Lambda([this]() { return FText::FromString(Session->GetNativeApiSummary()); });
            NativeApiPanel->AddSlot().AutoHeight().Padding(12, 0, 12, 6)[NativeApiSummary.ToSharedRef()];
            TSharedPtr<SWrapBox> NativeApiActions;
            SAssignNew(NativeApiActions, SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8, 8));
            NativeApiActions->AddSlot()[MakeAction(TEXT("Analyze native API"),
                FSimpleDelegate::CreateLambda([this]()
                {
                    FString NativeApiReport;
                    Session->ReviewNativeApi(NativeApiReport);
                    RecordStatus();
                }), true)];
            NativeApiActions->AddSlot()[MakeAction(TEXT("View native API review"),
                FSimpleDelegate::CreateLambda([this]()
                {
                    FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Session->GetNativeApiReport()));
                }))];
            TSharedPtr<SButton> RepairButton;
            SAssignNew(RepairButton, SButton).ButtonStyle(&PreparationButtonStyle).ContentPadding(FMargin(10, 6))
                .IsEnabled_Lambda([this]() { return !bOperationRunning && !FNativeCacheTask::IsRunning() && Session->CanRepairNativeApi(); })
                .ButtonColorAndOpacity_Lambda([this]()
                {
                    if (ShouldHighlightPreparationAction(TEXT("Try Fix project API")))
                    {
                        return FSlateColor(FLinearColor(0.12f, 0.48f, 0.9f));
                    }
                    return FSlateColor(FLinearColor(0.055f, 0.055f, 0.055f));
                })
                .ForegroundColor_Lambda([this]()
                {
                    if (ShouldHighlightPreparationAction(TEXT("Try Fix project API")))
                    {
                        return FSlateColor(FLinearColor(0.01f, 0.01f, 0.01f));
                    }
                    return FSlateColor::UseForeground();
                })
                .OnClicked_Lambda([this]()
                {
                    const FString ReviewIdentifier = Session->GetNativeApiReviewIdentifier();
                    const FString Confirmation = Session->GetNativeApiReport() +
                        TEXT("\nApply these project source changes? Originals will be backed up. Rebuild, restart and rebuild the Project API cache afterwards.");
                    if (FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(Confirmation)) == EAppReturnType::Yes)
                    {
                        RunOperation(TEXT("Try Fix project API"), FSimpleDelegate::CreateLambda([this, ReviewIdentifier]()
                        {
                            Session->ApplyNativeApiFixes(ReviewIdentifier);
                            RecordStatus();
                        }));
                    }
                    return FReply::Handled();
                })[MakeText(TEXT("Try Fix project API"))];
            NativeApiActions->AddSlot()[RepairButton.ToSharedRef()];
            NativeApiPanel->AddSlot().AutoHeight().Padding(12, 6, 12, 12)[NativeApiActions.ToSharedRef()];
            TSharedPtr<SVerticalBox> NativeApiExplanation;
            SAssignNew(NativeApiExplanation, SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0, 6)[MakeText(TEXT("Analyze checks class and method access across module boundaries. Review shows the reasons and proposed source edits. Try Fix attempts to add module API exports or make required private methods protected."))]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 6)[MakeText(TEXT("Try Fix asks for confirmation and backs up originals in Saved/BlueprintNativization/SourceRepairs. Afterwards, rebuild the project, restart the editor and rebuild the Project API cache before exporting."))];
            TSharedPtr<SExpandableArea> NativeApiHelp;
            SAssignNew(NativeApiHelp, SExpandableArea).InitiallyCollapsed(true)
                .HeaderContent()[MakeText(TEXT("Why is this needed?"))]
                .BodyContent()[NativeApiExplanation.ToSharedRef()];
            NativeApiPanel->AddSlot().AutoHeight().Padding(12, 0, 12, 12)[NativeApiHelp.ToSharedRef()];
            ReadinessPanel->AddSlot().AutoHeight().Padding(0, 6)[MakeWorkspacePanel(NativeApiPanel.ToSharedRef())];
        }
        ReadinessPanel->AddSlot().AutoHeight().Padding(0, 6)[MakeAnalysisPanel(TEXT("Blueprint code analysis"),
            TAttribute<FString>::CreateLambda([this]() { return Session->GetBlueprintCodeAnalysisReport(); }),
            TAttribute<bool>::CreateLambda([this]() { return Session->IsBlueprintCodeAnalysisReady(); }),
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth()[MakeAction(TEXT("Analyze Blueprint Code"), FSimpleDelegate::CreateLambda([this]()
            {
                Session->AnalyzeBlueprintCode();
                RecordMessage(Session->Status, !Session->bLastOperationSucceeded);
            }), true)]
            + SHorizontalBox::Slot().AutoWidth().Padding(8, 0)[MakeAction(TEXT("TryFix"), FSimpleDelegate::CreateLambda([this]()
            {
                Session->TryFixBlueprintCode();
                RecordMessage(Session->Status, !Session->bLastOperationSucceeded);
            }), true)])];
        ReadinessPanel->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(12, 6)[MakeAction(TEXT("Refresh readiness"), FSimpleDelegate::CreateLambda([this]()
        {
            PreparationReport = Session->GetPreparationReport(&ReadySections);
        }))];
    }
    if (Mode == EToolMode::Assets)
    {
        StageBody->SetContent(AssetsWorkspace.ToSharedRef());
        return;
    }
    if (Mode == EToolMode::Preview)
    {
        StageBody->SetContent(PreviewWorkspace.ToSharedRef());
        return;
    }
    if (Mode == EToolMode::Tools)
    {
        ModePanel->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(12)[MakeUtilityButtons()];
    }
    if (Mode == EToolMode::Setup)
    {
        if (!FGameProjectGenerationModule::Get().ProjectHasCodeFiles())
        {
            TSharedPtr<STextBlock> BlueprintProjectWarning;
            SAssignNew(BlueprintProjectWarning, STextBlock).AutoWrapText(true).ColorAndOpacity(FLinearColor(1.0f, 0.65f, 0.15f))
                .Text(FText::FromString(TEXT("Blueprint-only projects require C++ setup before conversion. Add an empty C++ class in Unreal Editor, then build the project with the C++ compiler and Windows SDK installed. Prepare module updates existing .Target.cs files but does not create missing targets. Your Blueprint gameplay logic does not need to be rewritten manually.")));
            ModulePanel->AddSlot().AutoHeight().Padding(12, 6, 12, 12)[BlueprintProjectWarning.ToSharedRef()];
        }
        ModulePanel->AddSlot().AutoHeight().Padding(12, 6)[MakeText(TEXT("C++ module"))];

        TSharedPtr<SEditableTextBox> ModuleNameEditor;
        SAssignNew(ModuleNameEditor, SEditableTextBox).Tag(TEXT("ModuleNameOverride")).ToolTipText(FText::FromString(TEXT("Leave empty to use the plugin default. Prepare module registers it in this project.")))
            .Text_Lambda([this]() { return FText::FromString(Session->ModuleNameOverride); })
            .HintText_Lambda([]() { return FText::FromString(UBlueprintNativizationSettingsLibrary::GetModuleName()); })
            .OnTextChanged_Lambda([this](const FText& Text)
            {
                Session->ModuleNameOverride = Text.ToString();
                Session->SaveSetup();
                PreparationReport = Session->GetPreparationReport(&ReadySections);
            });
        ModulePanel->AddSlot().AutoHeight().Padding(12, 6)[ModuleNameEditor.ToSharedRef()];
        ModulePanel->AddSlot().AutoHeight().Padding(12, 6)[ModuleStatus.ToSharedRef()];
        TSharedPtr<STextBlock> ModuleBuildWarning;
        SAssignNew(ModuleBuildWarning, STextBlock).AutoWrapText(true)
            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 20))
            .ColorAndOpacity(FLinearColor(1.0f, 0.45f, 0.05f))
            .Visibility_Lambda([this]()
            {
                if (ReadySections.Contains(TEXT("Module")) && !Session->IsPreparedModuleLoaded())
                {
                    return EVisibility::Visible;
                }
                return EVisibility::Collapsed;
            })
            .Text(FText::FromString(TEXT("Recompile the project before building API caches.\nClose Unreal Editor, build the project's Editor target, then reopen the project.")));
        ModulePanel->AddSlot().AutoHeight().Padding(12, 6)[ModuleBuildWarning.ToSharedRef()];
        ModulePanel->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(12, 6)[MakeAction(TEXT("Prepare module"), FSimpleDelegate::CreateLambda([this]()
        {
            Session->PrepareModule();
            PreparationReport = Session->GetPreparationReport(&ReadySections);
            RecordStatus();
        }), true)];
        ModePanel->AddSlot().AutoHeight().Padding(12, 10)[MakeText(TEXT("Export settings"))];
        ModePanel->AddSlot().AutoHeight().Padding(12, 6)[Details.ToSharedRef()];
        ModePanel->AddSlot().AutoHeight().Padding(12)[MakeText(TEXT("C++ behavior and component creation; serialized Blueprint configuration."))];

        ModePanel->AddSlot().AutoHeight().Padding(12, 6)[MakeAnalysisPanel(TEXT("Next steps"),
            TAttribute<FString>(TEXT("Build API caches, then analyze native API access. Review any proposed fixes before exporting. You can select assets at any time.")))];
    }
    if (Mode == EToolMode::Export)
    {
        ExportReport.Reset();
        if (!Session->ReviewExport(ExportReport))
        {
            ExportReport = Session->Status;
        }
        ModePanel->AddSlot().AutoHeight().Padding(12)[MakeAnalysisPanel(TEXT("Export overview"),
            TAttribute<FString>::CreateLambda([this]() { return ExportReport; }))];
        ModePanel->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(12)[MakeAction(TEXT("Refresh export analysis"), FSimpleDelegate::CreateLambda([this]()
        {
            if (!Session->ReviewExport(ExportReport))
            {
                ExportReport = Session->Status;
            }
        }), true)];
        ModePanel->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(12)[MakeAction(TEXT("Open Setup"), FSimpleDelegate::CreateLambda([this]() { SetMode(EToolMode::Setup); }))];
    }
    if (Mode == EToolMode::Migration)
    {
        ModePanel->AddSlot().AutoHeight()[Details.ToSharedRef()];
        ModePanel->AddSlot().AutoHeight().Padding(12)[MakeText(TEXT("Rebuild and restart the editor before applying migration."))];
        ModePanel->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(12)[MakeAction(TEXT("Analyze migration"), FSimpleDelegate::CreateLambda([this]()
        {
            ReviewedMigrationFile = Session->MigrationCacheFile.FilePath;
            if (ReviewedMigrationFile.IsEmpty())
            {
                MigrationReport = TEXT("Select a migration cache file first.");
                return;
            }
            Session->ReviewMigration(MigrationReport);
        }), true)];
        ModePanel->AddSlot().AutoHeight().Padding(12)[MakeAnalysisPanel(TEXT("Type mappings"),
            TAttribute<FString>::CreateLambda([this]()
            {
                if (MigrationReport.IsEmpty() || ReviewedMigrationFile != Session->MigrationCacheFile.FilePath)
                {
                    return FString(TEXT("Not analyzed\n\nSelect a cache and click Analyze migration to inspect resolved and missing types. Analysis does not change assets."));
                }
                return MigrationReport;
            }))];
        ModePanel->AddSlot().AutoHeight().Padding(12)[MakeAnalysisPanel(TEXT("Affected assets"),
            TAttribute<FString>(TEXT("Blueprints, type references and converted structures/enums\n\nApplying may reparent and clear Blueprint graphs, replace references and delete converted type assets. Dependent Blueprints may also change. The mapping report is not a complete asset diff.")))];
    }
    Details->ForceRefresh();
    TSharedPtr<SScrollBox> SettingsScroll;
    SAssignNew(SettingsScroll, SScrollBox) + SScrollBox::Slot()[ModePanel.ToSharedRef()];
    TSharedPtr<SScrollBox> ReadinessScroll;
    SAssignNew(ReadinessScroll, SScrollBox) + SScrollBox::Slot().Padding(4)[ReadinessPanel.ToSharedRef()];
    if (Mode == EToolMode::Setup)
    {
        TSharedPtr<SSplitter> SetupSplitter;
        SAssignNew(SetupSplitter, SSplitter).PhysicalSplitterHandleSize(5).HitDetectionSplitterHandleSize(9)
            + SSplitter::Slot().Value(0.46f).MinSize(320)[MakeWorkspacePanel(SettingsScroll.ToSharedRef())]
            + SSplitter::Slot().Value(0.54f).MinSize(320)[MakeWorkspacePanel(ReadinessScroll.ToSharedRef())];
        TSharedPtr<SBox> StagePadding;
        SAssignNew(StagePadding, SBox).Padding(0, 8, 0, 12)[SetupSplitter.ToSharedRef()];
        StageBody->SetContent(StagePadding.ToSharedRef());
    }
    else
    {
        StageBody->SetContent(SettingsScroll.ToSharedRef());
    }
}

void SBlueprintNativizationTool::RebuildNavigation()
{
    AssetPanel->ClearChildren();
    AssetThumbnails.Reset();
    if (!AssetThumbnailPool.IsValid())
    {
        AssetThumbnailPool = MakeShared<FAssetThumbnailPool>(128);
    }
    const FString Search = AssetNameSearch.TrimStartAndEnd();
    TArray<UObject*> VisibleTargets;
    for (UObject* Target : Session->Targets)
    {
        if (Search.IsEmpty() || GetNameSafe(Target).Contains(Search, ESearchCase::IgnoreCase))
        {
            VisibleTargets.Add(Target);
        }
    }
    FString TargetCount = FString::Format(TEXT("Objects to process ({0})"), {Session->Targets.Num()});
    if (!Search.IsEmpty())
    {
        TargetCount = FString::Format(TEXT("Objects to process ({0} / {1})"), {VisibleTargets.Num(), Session->Targets.Num()});
    }
    AssetPanel->AddSlot().AutoHeight().Padding(12)[MakeText(TargetCount)];
    AssetPanel->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(12, 4)[MakeAction(TEXT("Use selected assets"), FSimpleDelegate::CreateSP(this, &SBlueprintNativizationTool::UseContentBrowserSelection))];
    AssetPanel->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(12, 4)[MakeAction(TEXT("Clear list"), FSimpleDelegate::CreateLambda([this]()
    {
        Session->Targets.Reset();
        Session->DependencyWarnings.Reset();
        RefreshFunctions();
        RebuildNavigation();
    }))];
    if (Session->Targets.IsEmpty())
    {
        AssetPanel->AddSlot().AutoHeight().Padding(12)[MakeText(TEXT("Select assets in the Content Browser to begin."))];
    }
    else if (VisibleTargets.IsEmpty())
    {
        AssetPanel->AddSlot().AutoHeight().Padding(12)[MakeText(TEXT("No objects match this name."))];
    }
    for (UObject* Target : VisibleTargets)
    {
        const FString AssetPath = GetPathNameSafe(Target);
        TSharedPtr<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(Target, 48, 48, AssetThumbnailPool);
        AssetThumbnails.Add(Thumbnail);
        TSharedPtr<SBox> ThumbnailBox;
        SAssignNew(ThumbnailBox, SBox).WidthOverride(48).HeightOverride(48)[Thumbnail->MakeThumbnailWidget()];
        TSharedPtr<SHorizontalBox> AssetContent;
        SAssignNew(AssetContent, SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(4, 2, 8, 2)[ThumbnailBox.ToSharedRef()]
            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[MakeText(GetNameSafe(Target))];
        TSharedPtr<SButton> AssetSelection;
        SAssignNew(AssetSelection, SButton).ButtonStyle(FAppStyle::Get(), TEXT("NoBorder"))
            .ContentPadding(0).HAlign(HAlign_Fill)
            .ToolTipText(FText::FromString(AssetPath))
            .OnClicked_Lambda([this, AssetPath]()
            {
                SelectedAssetPath = AssetPath;
                return FReply::Handled();
            })[AssetContent.ToSharedRef()];
        TSharedPtr<SHorizontalBox> AssetRow;
        SAssignNew(AssetRow, SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1)[AssetSelection.ToSharedRef()]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0)[MakeAction(TEXT("Remove"), FSimpleDelegate::CreateLambda([this, Target]()
            {
                Session->Targets.Remove(Target);
                if (Session->Targets.IsEmpty())
                {
                    Session->DependencyWarnings.Reset();
                }
                RefreshFunctions();
                RebuildNavigation();
            }))];
        TSharedPtr<SBorder> RowBackground;
        SAssignNew(RowBackground, SBorder).BorderImage(FAppStyle::GetBrush(TEXT("WhiteBrush"))).Padding(0)
            .BorderBackgroundColor_Lambda([this, AssetPath]()
            {
                if (SelectedAssetPath == AssetPath)
                {
                    return FLinearColor(0.075f, 0.13f, 0.19f);
                }
                return FLinearColor(0.012f, 0.012f, 0.012f);
            })[AssetRow.ToSharedRef()];
        AssetPanel->AddSlot().AutoHeight().Padding(12, 1)[RowBackground.ToSharedRef()];
    }

    TSharedPtr<STextBlock> DependencyWarning;
    SAssignNew(DependencyWarning, STextBlock).AutoWrapText(true).ColorAndOpacity(FLinearColor(1.0f, 0.65f, 0.15f))
        .Text_Lambda([this]() { return FText::FromString(Session->DependencyWarnings); });
    AssetPanel->AddSlot().AutoHeight().Padding(12, 6)[DependencyWarning.ToSharedRef()];

}

UBlueprintNativizationToolSession* SBlueprintNativizationTool::GetSession() const
{
    return Session.Get();
}

bool SBlueprintNativizationTool::ConfirmDiscard()
{
    if (!Session->HasUnsavedChanges())
    {
        return true;
    }
    return FMessageDialog::Open(EAppMsgType::YesNo,
        FText::FromString(TEXT("Discard the unsaved preview and manual edits? Export the source first to keep it."))) == EAppReturnType::Yes;
}

bool SBlueprintNativizationTool::CanClose()
{
    return !Session->IsBusy() && !bOperationRunning && ConfirmDiscard();
}

void SBlueprintNativizationTool::SetBlueprintContext(UBlueprint* Blueprint, FName FunctionName)
{
    if (Session->IsBusy() || !ConfirmDiscard())
    {
        return;
    }
    const FString ExclusionReason = FEngineAssetPolicy::GetExclusionReason(Blueprint, Session->bExcludeEngineAssetsFromConversion);
    if (!ExclusionReason.IsEmpty())
    {
        Session->Status = ExclusionReason;
        RecordMessage(ExclusionReason, true);
        return;
    }
    Session->Targets = { Blueprint };
    Session->bGenerateOneFunction = !FunctionName.IsNone();
    Session->FunctionName.Reset();
    if (!FunctionName.IsNone())
    {
        Session->FunctionName = FunctionName.ToString();
    }
    RefreshFunctions();
    RebuildNavigation();
    SetMode(EToolMode::Preview);
    if (Session->bGenerateOneFunction)
    {
        Session->Generate();
        RefreshDocuments();
        RecordStatus();
    }
}

static FARFilter MakeContentFilter(const FString& Path)
{
    FARFilter Filter;
    Filter.PackagePaths.Add(FName(*Path));
    Filter.bRecursivePaths = true;
    Filter.ClassPaths = { UBlueprint::StaticClass()->GetClassPathName(), UUserDefinedStruct::StaticClass()->GetClassPathName(), UUserDefinedEnum::StaticClass()->GetClassPathName() };
    Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("UserDefinedStruct")));
    Filter.bRecursiveClasses = true;
    return Filter;
}

void SBlueprintNativizationTool::RefreshContentClassFilter()
{
    ExcludedContentClasses.Reset();
    TArray<FTopLevelAssetPath> ExcludedClassPaths;
    const UBlueprintNativizationV2EditorSettings* Settings = GetDefault<UBlueprintNativizationV2EditorSettings>();
    for (const TSoftClassPtr<UObject>& ExcludedClass : Settings->IgnoreClassToRefGenerate)
    {
        if (!ExcludedClass.IsNull())
        {
            ExcludedClassPaths.AddUnique(ExcludedClass.ToSoftObjectPath().GetAssetPath());
        }
    }
    FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetDerivedClassNames(ExcludedClassPaths, {}, ExcludedContentClasses);
    for (const FTopLevelAssetPath& ExcludedClassPath : ExcludedClassPaths)
    {
        ExcludedContentClasses.Add(ExcludedClassPath);
    }
}

bool SBlueprintNativizationTool::ShouldFilterContentAsset(const FAssetData& Asset) const
{
    if (!FEngineAssetPolicy::GetExclusionReason(Asset, Session->bExcludeEngineAssetsFromConversion).IsEmpty())
    {
        return true;
    }
    if (!bOnlySupportedContentTypes)
    {
        return false;
    }
    for (const FName ClassTag : {FBlueprintTags::GeneratedClassPath, FBlueprintTags::ParentClassPath, FBlueprintTags::NativeParentClassPath})
    {
        FString ClassPath;
        if (Asset.GetTagValue(ClassTag, ClassPath) && ExcludedContentClasses.Contains(FTopLevelAssetPath(FPackageName::ExportTextPathToObjectPath(ClassPath))))
        {
            return true;
        }
    }
    return false;
}

TSharedRef<SWidget> SBlueprintNativizationTool::MakeContentBrowser()
{
    RefreshContentClassFilter();
    IContentBrowserSingleton& Browser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get();
    FAssetPickerConfig AssetConfiguration;
    AssetConfiguration.Filter = MakeContentFilter(ContentPath);
    AssetConfiguration.Filter.PackagePaths = { GEditor->GetEditorSubsystem<UContentBrowserDataSubsystem>()->ConvertInternalPathToVirtual(FName(*ContentPath)) };
    AssetConfiguration.SelectionMode = ESelectionMode::Multi;
    AssetConfiguration.InitialAssetViewType = EAssetViewType::List;
    AssetConfiguration.bFocusSearchBoxWhenOpened = false;
    AssetConfiguration.GetCurrentSelectionDelegates.Add(&ContentSelection);
    AssetConfiguration.SetFilterDelegates.Add(&ContentFilter);
    AssetConfiguration.RefreshAssetViewDelegates.Add(&ContentRefresh);
    AssetConfiguration.OnShouldFilterAsset = FOnShouldFilterAsset::CreateSP(this, &SBlueprintNativizationTool::ShouldFilterContentAsset);
    AssetConfiguration.OnAssetDoubleClicked = FOnAssetDoubleClicked::CreateLambda([this](const FAssetData& Asset) { AddContentAssets({ Asset }); });
    TSharedRef<SWidget> AssetPicker = Browser.CreateAssetPicker(AssetConfiguration);
    FPathPickerConfig PathConfiguration;
    PathConfiguration.DefaultPath = ContentPath;
    PathConfiguration.bFocusSearchBoxWhenOpened = false;
    PathConfiguration.bAllowContextMenu = false;
    PathConfiguration.OnPathSelected = FOnPathSelected::CreateLambda([this](const FString& Path)
    {
        ContentPath = Path;
        RefreshContentClassFilter();
        FARFilter Filter = MakeContentFilter(Path);
        Filter.PackagePaths = { GEditor->GetEditorSubsystem<UContentBrowserDataSubsystem>()->ConvertInternalPathToVirtual(FName(*Path)) };
        ContentFilter.ExecuteIfBound(Filter);
    });
    TSharedPtr<SSplitter> BrowserSplitter;
    SAssignNew(BrowserSplitter, SSplitter).Orientation(Orient_Vertical).PhysicalSplitterHandleSize(5).HitDetectionSplitterHandleSize(9)
        + SSplitter::Slot().Value(0.35f).MinSize(70)[Browser.CreatePathPicker(PathConfiguration)]
        + SSplitter::Slot().Value(0.65f).MinSize(90)[AssetPicker];
    TSharedPtr<SWrapBox> ContentActions;
    SAssignNew(ContentActions, SWrapBox).UseAllottedSize(true)
        + SWrapBox::Slot().Padding(2)[MakeAction(TEXT("Add selected"), FSimpleDelegate::CreateLambda([this]()
        {
            if (ContentSelection.IsBound())
            {
                AddContentAssets(ContentSelection.Execute());
            }
        }))]
        + SWrapBox::Slot().Padding(2)[MakeAction(TEXT("Add folder"), FSimpleDelegate::CreateLambda([this]() { AddContentFolder(ContentPath); }))];
    TSharedPtr<SVerticalBox> BrowserPanel;
    TSharedPtr<SCheckBox> SupportedTypesFilter;
    SAssignNew(SupportedTypesFilter, SCheckBox)
        .ToolTipText(FText::FromString(TEXT("Hide Blueprint classes excluded by conversion settings, including their descendants. Graph node support is checked during generation.")))
        .IsChecked_Lambda([this]()
        {
            if (bOnlySupportedContentTypes)
            {
                return ECheckBoxState::Checked;
            }
            return ECheckBoxState::Unchecked;
        })
        .OnCheckStateChanged_Lambda([this](ECheckBoxState State)
        {
            bOnlySupportedContentTypes = State == ECheckBoxState::Checked;
            RefreshContentClassFilter();
            ContentRefresh.ExecuteIfBound(false);
        })[MakeText(TEXT("Only supported types"))];
    SAssignNew(BrowserPanel, SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(8)[MakeText(TEXT("Content"))]
        + SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 6)[SupportedTypesFilter.ToSharedRef()]
        + SVerticalBox::Slot().FillHeight(1)[BrowserSplitter.ToSharedRef()]
        + SVerticalBox::Slot().AutoHeight().Padding(4)[ContentActions.ToSharedRef()];
    return BrowserPanel.ToSharedRef();
}

void SBlueprintNativizationTool::AddContentFolder(const FString& Path)
{
    TArray<FAssetData> Assets;
    FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssets(MakeContentFilter(Path), Assets);
    Assets.Sort([](const FAssetData& First, const FAssetData& Second) { return First.GetObjectPathString() < Second.GetObjectPathString(); });
    AddContentAssets(Assets);
}

void SBlueprintNativizationTool::AddContentAssets(const TArray<FAssetData>& Assets)
{
    if (Session->IsBusy())
    {
        return;
    }
    RefreshContentClassFilter();
    const int32 PreviousCount = Session->Targets.Num();
    for (const FAssetData& Asset : Assets)
    {
        const FString AssetExclusionReason = FEngineAssetPolicy::GetExclusionReason(Asset, Session->bExcludeEngineAssetsFromConversion);
        if (!AssetExclusionReason.IsEmpty())
        {
            RecordMessage(AssetExclusionReason, true);
            continue;
        }
        if (ShouldFilterContentAsset(Asset))
        {
            continue;
        }
        UObject* Object = Asset.GetAsset();
        const FString ExclusionReason = FEngineAssetPolicy::GetExclusionReason(Object, Session->bExcludeEngineAssetsFromConversion);
        if (!ExclusionReason.IsEmpty())
        {
            RecordMessage(ExclusionReason, true);
            continue;
        }
        if (Object && (Object->IsA<UBlueprint>() || Object->IsA<UUserDefinedStruct>() || Object->IsA<UUserDefinedEnum>()))
        {
            Session->Targets.AddUnique(Object);
        }
    }
    Session->Status = FString::Format(TEXT("Added {0} assets. Generation list contains {1} objects."), { Session->Targets.Num() - PreviousCount, Session->Targets.Num() });
    RefreshFunctions();
    RebuildNavigation();
    RecordMessage(Session->Status, false);
}

void SBlueprintNativizationTool::UseContentBrowserSelection()
{
    TArray<FAssetData> Selection;
    FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().GetSelectedAssets(Selection);
    AddContentAssets(Selection);
}

void SBlueprintNativizationTool::RefreshFunctions()
{
    Functions.Reset();
    for (const FString& Function : Session->GetFunctionNames())
    {
        Functions.Add(MakeShared<FString>(Function));
    }
    FunctionSelector->RefreshOptions();
    if (!Session->GetFunctionNames().Contains(Session->FunctionName))
    {
        Session->FunctionName.Reset();
    }
    for (const TSharedPtr<FString>& Function : Functions)
    {
        if (*Function == Session->FunctionName)
        {
            FunctionSelector->SetSelectedItem(Function);
        }
    }
}

void SBlueprintNativizationTool::Generate()
{
    if (!ConfirmDiscard())
    {
        return;
    }
    const bool bGenerated = Session->Generate();
    RefreshDocuments();
    if (!bOperationCancellationRequested)
    {
        RecordStatus();
    }
    if (!bGenerated && !Session->HasResult() && !Session->IsNativeApiReady())
    {
        SetMode(EToolMode::Setup);
    }
}

void SBlueprintNativizationTool::RefreshDocuments()
{
    Documents.Reset();
    for (const FString& Name : Session->GetDocumentNames())
    {
        Documents.Add(MakeShared<FString>(Name));
    }
    FilterDocuments();
    SelectDocument(0);
    RebuildNavigation();
}

void SBlueprintNativizationTool::FilterDocuments()
{
    FilteredDocuments.Reset();
    for (const TSharedPtr<FString>& Document : Documents)
    {
        if (DocumentSearch.IsEmpty() || Document->Contains(DocumentSearch, ESearchCase::IgnoreCase))
        {
            FilteredDocuments.Add(Document);
        }
    }
    DocumentList->RequestListRefresh();
}

TSharedRef<ITableRow> SBlueprintNativizationTool::MakeDocumentRow(TSharedPtr<FString> Document, const TSharedRef<STableViewBase>& OwnerTable)
{
    const int32 DocumentIndex = Documents.IndexOfByKey(Document);
    TSharedPtr<STextBlock> Label;
    SAssignNew(Label, STextBlock).OverflowPolicy(ETextOverflowPolicy::Ellipsis).Text_Lambda([this, Document, DocumentIndex]()
    {
        FString Name = *Document;
        if (Session->IsDocumentModified(DocumentIndex))
        {
            Name += TEXT(" *");
        }
        return FText::FromString(Name);
    });
    TSharedPtr<SBorder> DocumentContent;
    SAssignNew(DocumentContent, SBorder)
        .BorderImage(FAppStyle::GetBrush(TEXT("NoBorder")))
        .Padding(6, 4)
        .Tag(FName(*FString::Format(TEXT("GeneratedDocument{0}"), { DocumentIndex })))
        .ToolTipText(FText::FromString(*Document))
        .OnMouseButtonUp(this, &SBlueprintNativizationTool::ShowDocumentMenu, DocumentIndex)
        [Label.ToSharedRef()];
    TSharedPtr<STableRow<TSharedPtr<FString>>> DocumentRow;
    SAssignNew(DocumentRow, STableRow<TSharedPtr<FString>>, OwnerTable)
        [DocumentContent.ToSharedRef()];
    return DocumentRow.ToSharedRef();
}

FReply SBlueprintNativizationTool::ShowDocumentMenu(const FGeometry& Geometry, const FPointerEvent& MouseEvent, int32 DocumentIndex)
{
    if (MouseEvent.GetEffectingButton() != EKeys::RightMouseButton || !Session->GetDocumentBlueprint(DocumentIndex))
    {
        return FReply::Unhandled();
    }
    SelectDocument(DocumentIndex);
    FMenuBuilder MenuBuilder(true, nullptr);
    MenuBuilder.AddMenuEntry(
        NSLOCTEXT("BlueprintNativizationTool", "OpenSourceBlueprint", "Open source Blueprint"),
        NSLOCTEXT("BlueprintNativizationTool", "OpenSourceBlueprintTooltip", "Open the Blueprint used to generate this document."),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("ClassIcon.Blueprint")),
        FUIAction(FExecuteAction::CreateSP(this, &SBlueprintNativizationTool::OpenDocumentBlueprint, DocumentIndex)));
    FSlateApplication::Get().PushMenu(AsShared(), FWidgetPath(), MenuBuilder.MakeWidget(),
        MouseEvent.GetScreenSpacePosition(), FPopupTransitionEffect::ContextMenu);
    return FReply::Handled();
}

void SBlueprintNativizationTool::OpenDocumentBlueprint(int32 DocumentIndex)
{
    if (UBlueprint* Blueprint = Session->GetDocumentBlueprint(DocumentIndex))
    {
        GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Blueprint);
    }
}

void SBlueprintNativizationTool::SelectDocument(int32 DocumentIndex)
{
    SelectedDocument = INDEX_NONE;
    if (Documents.IsValidIndex(DocumentIndex))
    {
        SelectedDocument = DocumentIndex;
    }
    if (SelectedDocument != INDEX_NONE && FilteredDocuments.Contains(Documents[SelectedDocument]))
    {
        DocumentList->SetSelection(Documents[SelectedDocument], ESelectInfo::Direct);
    }
    else
    {
        DocumentList->ClearSelection();
    }
    bUpdatingEditor = true;
    CodeEditor->SetText(FText::FromString(Session->GetDocumentText(SelectedDocument)));
    bUpdatingEditor = false;
    UpdateLineNumbers();
}

void SBlueprintNativizationTool::UpdateLineNumbers()
{
    const FString Text = Session->GetDocumentText(SelectedDocument);
    int32 LineCount = 1;
    for (TCHAR Character : Text)
    {
        if (Character == TEXT('\n'))
        {
            ++LineCount;
        }
    }
    FString Numbers;
    for (int32 LineIndex = 1; LineIndex <= LineCount; ++LineIndex)
    {
        if (LineIndex > 1)
        {
            Numbers += TEXT("\n");
        }
        Numbers += FString::Format(TEXT("{0}"), { LineIndex });
    }
    LineNumbers->SetText(FText::FromString(Numbers));
}

void SBlueprintNativizationTool::RecordStatus()
{
    RecordMessage(Session->Status, !Session->bLastOperationSucceeded);
}

void SBlueprintNativizationTool::RecordMessage(const FString& Message, bool bError)
{
    EOutputLogSeverity Severity = EOutputLogSeverity::Info;
    const bool bHasDiagnosticSections = Message.Contains(TEXT("\nErrors:\n")) || Message.Contains(TEXT("\nWarnings:\n"));
    if (bError && !bHasDiagnosticSections)
    {
        Severity = EOutputLogSeverity::Error;
    }
    TArray<FString> MessageLines;
    Message.ParseIntoArrayLines(MessageLines);
    TArray<FOutputLogMessage>& RecordedMessages = OutputLogHistory.AddDefaulted_GetRef();
    for (const FString& MessageLine : MessageLines)
    {
        const FString TrimmedLine = MessageLine.TrimStartAndEnd();
        if (TrimmedLine.Equals(TEXT("Errors:"), ESearchCase::IgnoreCase))
        {
            Severity = EOutputLogSeverity::Error;
        }
        else if (TrimmedLine.Equals(TEXT("Warnings:"), ESearchCase::IgnoreCase) ||
            TrimmedLine.StartsWith(TEXT("Warning:"), ESearchCase::IgnoreCase))
        {
            Severity = EOutputLogSeverity::Warning;
        }
        FOutputLogMessage& RecordedMessage = RecordedMessages.AddDefaulted_GetRef();
        RecordedMessage.Text = MessageLine;
        RecordedMessage.Severity = Severity;
    }
    if (OutputLogHistory.Num() > 20)
    {
        OutputLogHistory.RemoveAt(0);
    }
    RefreshOutputLog();
    MessageScroll->ScrollToEnd();
}

void SBlueprintNativizationTool::RefreshOutputLog()
{
    MessageHistory->ClearChildren();
    bool bHasVisibleMessages = false;
    for (const TArray<FOutputLogMessage>& RecordedMessages : OutputLogHistory)
    {
        for (const FOutputLogMessage& RecordedMessage : RecordedMessages)
        {
            FString Severity = TEXT("INFO");
            FLinearColor MessageColor(0.8f, 0.8f, 0.8f);
            FLinearColor BackgroundColor(0.035f, 0.035f, 0.035f);
            if (RecordedMessage.Severity == EOutputLogSeverity::Warning)
            {
                Severity = TEXT("WARNING");
                MessageColor = FLinearColor(1.0f, 0.85f, 0.2f);
                BackgroundColor = FLinearColor(0.12f, 0.09f, 0.025f);
            }
            else if (RecordedMessage.Severity == EOutputLogSeverity::Error)
            {
                Severity = TEXT("ERROR");
                MessageColor = FLinearColor(1.0f, 0.35f, 0.3f);
                BackgroundColor = FLinearColor(0.12f, 0.025f, 0.025f);
            }
            const FString DisplayMessage = FString::Format(TEXT("[{0}] {1}"), { Severity, RecordedMessage.Text });
            if (!OutputLogSearch.IsEmpty() && !DisplayMessage.Contains(OutputLogSearch, ESearchCase::IgnoreCase))
            {
                continue;
            }
            TSharedPtr<STextBlock> MessageText;
            SAssignNew(MessageText, STextBlock).AutoWrapText(true).ColorAndOpacity(MessageColor)
                .Text(FText::FromString(DisplayMessage)).HighlightText(FText::FromString(OutputLogSearch));
            TSharedPtr<SBorder> MessageBackground;
            SAssignNew(MessageBackground, SBorder).BorderImage(FAppStyle::GetBrush(TEXT("WhiteBrush")))
                .BorderBackgroundColor(BackgroundColor).Padding(8)[MessageText.ToSharedRef()];
            MessageHistory->AddSlot().AutoHeight().Padding(0, 0, 0, 4)[MessageBackground.ToSharedRef()];
            bHasVisibleMessages = true;
        }
    }
    if (!bHasVisibleMessages && !OutputLogSearch.IsEmpty())
    {
        MessageHistory->AddSlot().AutoHeight().Padding(8)[MakeText(TEXT("No matching log messages."))];
    }
}

void SBlueprintNativizationTool::Save()
{
    FString Summary;
    if (Session->ReviewExport(Summary) &&
        FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(Summary + TEXT("\n\nWrite these files?"))) == EAppReturnType::Yes)
    {
        Session->Save();
        Details->ForceRefresh();
    }
    RecordStatus();
}

void SBlueprintNativizationTool::ApplyCache()
{
    FString Summary;
    if (Session->ReviewMigration(Summary) &&
        FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(Summary + TEXT("\n\nApply this migration?"))) == EAppReturnType::Yes)
    {
        Session->ApplyCache();
    }
    RecordStatus();
}
