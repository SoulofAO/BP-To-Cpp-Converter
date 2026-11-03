/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "UObject/StrongObjectPtr.h"
#include "BlueprintNativizationToolSession.h"
#include "IContentBrowserSingleton.h"
#include "Widgets/Views/SListView.h"

class UBlueprint;
class UBlueprintNativizationToolSession;
class SColorEditableText;
class IDetailsView;
class SVerticalBox;
class STextBlock;
class SScrollBox;
class SBox;
class SButton;
class FAssetThumbnail;
class FAssetThumbnailPool;
template<typename ItemType> class SComboBox;

class BLUEPRINTNATIVIZATIONV2_API SBlueprintNativizationTool : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SBlueprintNativizationTool) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& Arguments);
    virtual void Tick(const FGeometry& Geometry, double CurrentTime, float DeltaTime) override;
    virtual ~SBlueprintNativizationTool() override;
    void SetBlueprintContext(UBlueprint* Blueprint, FName FunctionName);
    UBlueprintNativizationToolSession* GetSession() const;
    void RefreshDocuments();
    void SelectDocument(int32 DocumentIndex);
    bool CanClose();
    void AddContentAssets(const TArray<FAssetData>& Assets);
    void AddContentFolder(const FString& Path);

private:
    TStrongObjectPtr<UBlueprintNativizationToolSession> Session;
    TSharedPtr<IDetailsView> Details;
    TSharedPtr<SColorEditableText> CodeEditor;
    TSharedPtr<SComboBox<TSharedPtr<FString>>> FunctionSelector;
    TArray<TSharedPtr<FString>> Documents;
    TArray<TSharedPtr<FString>> FilteredDocuments;
    FString DocumentSearch;
    TSharedPtr<SListView<TSharedPtr<FString>>> DocumentList;
    TArray<TSharedPtr<FString>> Functions;
    int32 SelectedDocument = INDEX_NONE;
    bool bUpdatingEditor = false;
    enum class EToolMode { Setup, Assets, Preview, Export, Migration, Tools };
    EToolMode Mode = EToolMode::Setup;
    TSharedPtr<SBox> StageBody;
    TSharedPtr<SButton> PrimaryAction;
    TSharedPtr<SWidget> AssetsWorkspace;
    TSharedPtr<SWidget> PreviewWorkspace;
    TSharedPtr<FAssetThumbnailPool> AssetThumbnailPool;
    TArray<TSharedPtr<FAssetThumbnail>> AssetThumbnails;
    FString SelectedAssetPath;
    FString AssetNameSearch;
    TSharedPtr<SVerticalBox> AssetPanel;
    TSharedPtr<SVerticalBox> ModePanel;
    TSharedPtr<SColorEditableText> LineNumbers;
    TSharedPtr<SVerticalBox> MessageHistory;
    TSharedPtr<SScrollBox> MessageScroll;
    enum class EOutputLogSeverity { Info, Warning, Error };
    struct FOutputLogMessage
    {
        FString Text;
        EOutputLogSeverity Severity = EOutputLogSeverity::Info;
    };
    TArray<TArray<FOutputLogMessage>> OutputLogHistory;
    FString OutputLogSearch;
    int32 FontSize = 10;
    int32 CursorLine = 1;
    int32 CursorColumn = 1;
    float AssetWidth = 0.2f;
    float SourceWidth = 0.56f;
    float SettingsWidth = 0.24f;
    float ContentHeight = 0.65f;
    float WorkspaceHeight = 0.78f;
    FString PreparationReport;
    FString CacheValidationStatus;
    TSet<FString> ReadySections;
    FString CacheTaskReport;
    double NextCacheStatusUpdate = 0;
    bool bCacheTaskWasRunning = false;
    bool bOperationRunning = false;
    bool bOperationCanCancel = false;
    bool bOperationCancellationRequested = false;
    FString OperationLabel;
    TOptional<float> OperationFraction;
    FButtonStyle PreparationButtonStyle;
    FString ExportReport;
    FString MigrationReport;
    FString ReviewedMigrationFile;
    FString ContentPath = TEXT("/Game");
    FGetCurrentSelectionDelegate ContentSelection;
    FSetARFilterDelegate ContentFilter;
    FRefreshAssetViewDelegate ContentRefresh;
    bool bOnlySupportedContentTypes = true;
    TSet<FTopLevelAssetPath> ExcludedContentClasses;

    TSharedRef<SWidget> MakeAction(const FString& Label, FSimpleDelegate Action, bool bLongOperation = false);
    void RunOperation(const FString& Label, FSimpleDelegate Action, bool bCanCancel = false);
    bool UpdateOperationProgress(int32 Completed, int32 Total, const FString& Detail);
    bool ShouldHighlightPreparationAction(const FString& Label) const;
    bool IsSetupReady() const;
    bool IsPreparationAction(const FString& Label) const;
    TSharedRef<SWidget> MakeOption(TSharedPtr<FString> Option);
    void RefreshFunctions();
    void FilterDocuments();
    TSharedRef<ITableRow> MakeDocumentRow(TSharedPtr<FString> Document, const TSharedRef<STableViewBase>& OwnerTable);
    void Generate();
    void Save();
    void ApplyCache();
    void UseContentBrowserSelection();
    void RebuildNavigation();
    void SetMode(EToolMode NewMode);
    void UpdateLineNumbers();
    void SetPreviewFontSize(int32 NewFontSize);
    FReply ShowDocumentMenu(const FGeometry& Geometry, const FPointerEvent& MouseEvent, int32 DocumentIndex);
    void OpenDocumentBlueprint(int32 DocumentIndex);
    void RecordStatus();
    void RecordMessage(const FString& Message, bool bError);
    void RefreshOutputLog();
    bool ConfirmDiscard();
    TSharedRef<SWidget> MakeUtilityButtons();
    TSharedRef<SWidget> MakeContentBrowser();
    void RefreshContentClassFilter();
    bool ShouldFilterContentAsset(const FAssetData& Asset) const;
    TSharedRef<SWidget> MakeText(const FString& Text);
};
