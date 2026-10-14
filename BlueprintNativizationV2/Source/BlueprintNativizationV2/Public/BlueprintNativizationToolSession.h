#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
#include "BlueprintNativizationData.h"
#include "NativeApiPreflight.h"
#include "BlueprintNativizationSettings.h"
#include "BlueprintNativizationToolSession.generated.h"

UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UBlueprintNativizationToolSession : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category = "Output", meta = (ToolTip = "Optional module name for this session. Empty uses the plugin setting."))
    FString ModuleNameOverride;

    UPROPERTY(EditAnywhere, Category = "Output", meta = (DisplayName = "Exclude Engine Assets From Conversion", ToolTip = "Exclude Engine assets, Engine plugin assets and Blueprint descendants of Engine Blueprints from selection, dependencies and generation. Native C++ parents remain supported."))
    bool bExcludeEngineAssetsFromConversion = true;

    UPROPERTY(EditAnywhere, Category = "Output", meta = (DisplayName = "Continue On Error", ToolTip = "Continue generation and migration with the successful subset. Skip failed assets and assets that depend on them. Allow Export After Failed Generation separately permits diagnostic export of incomplete code."))
    bool bContinueOnError = true;

    UPROPERTY(EditAnywhere, Category = "Output", meta = (DisplayName = "Allow Export After Failed Generation", ToolTip = "Keep partial source after generation errors and allow diagnostic export. The code may not compile. Migration caches are never written for failed generation."))
    bool bAllowExportAfterFailedGeneration = false;

    FString GetModuleName() const;
    void RemoveExcludedTargets();
    void LoadSetup();
    void SaveSetup() const;
    bool IsModuleReady(FString& Reason) const;
    bool CanExport(FString& Reason) const;
    FString GetPreparationReport(TSet<FString>* ReadySections = nullptr) const;
    bool ReviewNativeApi(FString& Report);
    bool ApplyNativeApiFixes(const FString& ReviewIdentifier);
    FString GetNativeApiReport() const;
    FString GetNativeApiSummary() const;
    TArray<FString> GetNativeApiRepairFiles() const;
    FString GetNativeApiReviewIdentifier() const;
    bool CanRepairNativeApi() const;
    bool IsNativeApiReady() const;
    bool IsNativeApiChecked() const;
    FString GetNativeApiCheckStatus() const;
    UPROPERTY(EditAnywhere, Category = "Input")
    TArray<TObjectPtr<UObject>> Targets;

    UPROPERTY(EditAnywhere, Category = "Input")
    bool bGenerateOneFunction = false;

    UPROPERTY(EditAnywhere, Category = "Input")
    bool bIncludeDependencies = false;

    UPROPERTY()
    bool bLeaveAssetReferencesInBlueprint = true;

    UPROPERTY(EditAnywhere, Category = "Output", meta = (ToolTip = "Plain text discards localization data. New readable keys require new or migrated translations and flatten text histories. Preserve retains all original text data and translation identity."))
    ETextGenerationMode TextGenerationMode = ETextGenerationMode::PreserveTextData;

    UPROPERTY(EditAnywhere, Category = "Output", meta = (DisplayName = "Remove Unreal Prefixes From File Names", ToolTip = "Remove U, A, F and E type prefixes from generated filenames. C++ type names remain unchanged. Regenerate the preview after changing this option."))
    bool bRemoveUnrealPrefixesFromFileNames = true;

    UPROPERTY(EditAnywhere, Category = "Output", meta = (DisplayName = "Replace Default-Only Children With Parents", ToolTip = "Skip Blueprint children with no added structure or logic. Generated references use their parents and discard child class default overrides. Source assets are unchanged."))
    bool bReplaceDefaultOnlyChildrenWithParents = true;

    UPROPERTY(EditAnywhere, Category = "Output", meta = (ClampMin = "2", ToolTip = "Minimum occurrences before an independent guarded call becomes a named local lambda."))
    int32 RepeatedConstructionThreshold = 3;

    UPROPERTY(EditAnywhere, Category = "Output")
    FDirectoryPath OutputFolder;

    UPROPERTY(EditAnywhere, Category = "Output")
    bool bMergeWithExistingModule = false;

    UPROPERTY(EditAnywhere, Category = "Output")
    bool bSaveMigrationCache = false;

    UPROPERTY(EditAnywhere, Category = "Output", meta = (EditCondition = "bSaveMigrationCache"))
    FDirectoryPath MigrationCacheFolder;

    UPROPERTY(EditAnywhere, Category = "Apply", meta = (FilePathFilter = "bpncache"))
    FFilePath MigrationCacheFile;

    FString FunctionName;
    FString Status;
    FString DependencyWarnings;
    void AuditDependencies();
    bool bLastOperationSucceeded = false;
    TFunction<bool(int32, int32, const FString&)> OperationProgress;

    bool IsBusy() const;
    bool Generate();
    bool AnalyzeBlueprintCode();
    bool TryFixBlueprintCode();
    FString GetBlueprintCodeAnalysisReport() const;
    bool IsBlueprintCodeAnalysisCurrent() const;
    bool IsBlueprintCodeAnalysisReady() const;
    bool GetDependencies();
    bool Save();
    bool ApplyCache();
    bool PrepareModule();
    void ResetNames();
    void PrintNodes();
    TArray<FString> GetFunctionNames() const;
    TArray<FString> GetDocumentNames() const;
    FString GetDocumentText(int32 DocumentIndex) const;
    UBlueprint* GetDocumentBlueprint(int32 DocumentIndex) const;
    void SetDocumentText(int32 DocumentIndex, const FString& Text);
    bool HasResult() const;
    bool HasGenerationErrors() const;
    bool HasPartialResult() const;
    FString GenerationErrors;
    bool HasUnsavedChanges() const;
    bool IsDocumentModified(int32 DocumentIndex) const;
    bool IsPreviewStale() const;
    bool ReviewExport(FString& Summary);
    bool ReviewMigration(FString& Summary);
    TArray<FString> GetGeneratedObjects() const;

private:
    FString BlueprintCodeAnalysisReport;
    FString BlueprintCodeAnalysisSignature;
    bool bBlueprintCodeAnalysisReady = false;
    void InvalidateBlueprintCodeAnalysis(UBlueprint* Blueprint);
    UPROPERTY(Transient)
    TArray<TObjectPtr<UBlueprint>> AnalyzedBlueprints;
    TSet<FName> MissingDependencyOwners;

    UPROPERTY(Transient)
    FNativizationModuleResult Result;

    UPROPERTY(Transient)
    TMap<UField*, FName> GeneratedFields;

    TMap<FString, FString> GeneratedMemberNames;
    TMap<FString, FString> GeneratedDefaultOnlyClassReplacements;

    FString GeneratedModuleName;
    bool bFunctionResult = false;
    bool bOperationInProgress = false;
    TArray<FString> SavedDocuments;
    FString GeneratedInputSignature;
    bool bSourceSaved = false;
    FString GetInputSignature() const;
    bool Fail(const FString& Message);
    bool ValidateModuleName();
    bool CheckNativeApi(bool bIncludeProjectClients = false);
    FNativeApiReview NativeApiReview;
    bool bNativeApiChecked = false;
    FString NativeApiInputSignature;
};
