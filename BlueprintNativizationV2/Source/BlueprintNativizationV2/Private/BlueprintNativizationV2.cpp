/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright 2025 AO All Rights Reserved.
 */

#include "BlueprintNativizationV2.h"
#include "AutomationBlueprintFunctionLibrary.h"
#include "BlueprintEditorModule.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "EditorModeManager.h"
#include "ToolMenuSection.h"
#include "BlueprintActionMenuUtils.h"
#include "ConversionDetailsCustomizations.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_Event.h"
#include "SBlueprintNativizationTool.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "ToolMenus.h"
#include "Misc/CoreDelegates.h"
#if BPTOCPP_WITH_SEMANTIC_SEARCH
#include "Async/TaskGraphInterfaces.h"
#include "ISemanticSearchModule.h"
#endif


#define LOCTEXT_NAMESPACE "FBlueprintNativizationV2Module"

void FBlueprintNativizationV2Module::StartupModule()
{
#if BPTOCPP_WITH_SEMANTIC_SEARCH
    BackgroundIndexingShutdownRegistration = FCoreDelegates::OnEnginePreExit.AddLambda([]()
    {
        UE::SemanticSearch::ISemanticSearchModule* SemanticSearch =
            FModuleManager::GetModulePtr<UE::SemanticSearch::ISemanticSearchModule>(TEXT("SemanticSearch"));
        if (SemanticSearch && SemanticSearch->IsInitialized())
        {
            SemanticSearch->CancelIndexing();
            FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
        }
    });
#endif
    FTabManager::RegisterDefaultTabWindowSize(FTabId(TEXT("BlueprintNativizationTool")), FVector2D(1200, 760));
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TEXT("BlueprintNativizationTool"),
        FOnSpawnTab::CreateRaw(this, &FBlueprintNativizationV2Module::SpawnToolTab))
        .SetDisplayName(LOCTEXT("ToolTitle", "Blueprint to C++"))
        .SetMenuType(ETabSpawnerMenuType::Hidden);
    UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FBlueprintNativizationV2Module::RegisterMenus));
    FBlueprintEditorModule& BlueprintModule = FModuleManager::LoadModuleChecked<FBlueprintEditorModule>("Kismet");

    FunctionCustomizationHandle = BlueprintModule.RegisterFunctionCustomization(
        UK2Node_FunctionEntry::StaticClass(),
        FOnGetFunctionCustomizationInstance::CreateStatic(&FConversionDetailsFunctionCustomization::MakeInstance));

    EventCustomizationHandle = BlueprintModule.RegisterFunctionCustomization(
            UK2Node_Event::StaticClass(),
            FOnGetFunctionCustomizationInstance::CreateStatic(&FConversionDetailsFunctionCustomization::MakeInstance));

	PropertyCustomizationHandle = BlueprintModule.RegisterVariableCustomization(
		FProperty::StaticClass(),
        FOnGetVariableCustomizationInstance::CreateStatic(&FConversionDetailsPropertyCustomization::MakeInstance));

    LocalPropertyCustomizationHandle = BlueprintModule.RegisterLocalVariableCustomization(
        FProperty::StaticClass(),
        FOnGetVariableCustomizationInstance::CreateStatic(&FConversionDetailsPropertyCustomization::MakeInstance));
}

void FBlueprintNativizationV2Module::ShutdownModule()
{
    FCoreDelegates::OnEnginePreExit.Remove(BackgroundIndexingShutdownRegistration);
    UToolMenus::UnRegisterStartupCallback(this);
    UToolMenus::UnregisterOwner(this);
    if (TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId(TEXT("BlueprintNativizationTool"))))
    {
        Tab->RequestCloseTab();
    }
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TEXT("BlueprintNativizationTool"));
    FTabManager::UnregisterDefaultTabWindowSize(FTabId(TEXT("BlueprintNativizationTool")));
    Tool.Reset();
    if (FModuleManager::Get().IsModuleLoaded("Kismet"))
    {
        FBlueprintEditorModule& BlueprintModule = FModuleManager::GetModuleChecked<FBlueprintEditorModule>("Kismet");
        BlueprintModule.UnregisterFunctionCustomization(UK2Node_FunctionEntry::StaticClass(), FunctionCustomizationHandle);
        BlueprintModule.UnregisterFunctionCustomization(UK2Node_Event::StaticClass(), EventCustomizationHandle);
        BlueprintModule.UnregisterVariableCustomization(FProperty::StaticClass(), PropertyCustomizationHandle);
        BlueprintModule.UnregisterLocalVariableCustomization(FProperty::StaticClass(), LocalPropertyCustomizationHandle);
    }
}

void FBlueprintNativizationV2Module::RegisterMenus()
{
    FToolMenuOwnerScoped Owner(this);
    UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
    FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("Programming"));
    Section.AddMenuEntry(TEXT("OpenBlueprintNativizationTool"), LOCTEXT("OpenTool", "Blueprint to C++"),
        LOCTEXT("OpenToolDescription", "Generate, edit and save C++ source from Blueprint assets."), FSlateIcon(),
        FUIAction(FExecuteAction::CreateLambda([this]() { OpenTool(); })));
}

TSharedRef<SDockTab> FBlueprintNativizationV2Module::SpawnToolTab(const FSpawnTabArgs& Arguments)
{
    TSharedPtr<SBlueprintNativizationTool> Widget;
    SAssignNew(Widget, SBlueprintNativizationTool);
    Tool = Widget;
    TSharedPtr<SDockTab> Tab;
    SAssignNew(Tab, SDockTab).TabRole(ETabRole::NomadTab)
        .OnCanCloseTab(SDockTab::FCanCloseTab::CreateSP(Widget.ToSharedRef(), &SBlueprintNativizationTool::CanClose))
        [Widget.ToSharedRef()];
    return Tab.ToSharedRef();
}

TSharedPtr<SBlueprintNativizationTool> FBlueprintNativizationV2Module::OpenTool(UBlueprint* Blueprint, FName FunctionName)
{
    FGlobalTabmanager::Get()->TryInvokeTab(FTabId(TEXT("BlueprintNativizationTool")));
    TSharedPtr<SBlueprintNativizationTool> Widget = Tool.Pin();
    if (Widget.IsValid() && Blueprint)
    {
        Widget->SetBlueprintContext(Blueprint, FunctionName);
    }
    return Widget;
}


#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FBlueprintNativizationV2Module, BlueprintNativizationV2)
