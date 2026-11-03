/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"
#include "RetainedBlueprintGeneration.h"
#include "NativeApiCatalog.h"
#include "Components/ActorComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Engine.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditorOnlyComponentCreationTest,
    "Project.BlueprintNativization.Defaults.EditorOnlyComponentCreation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorOnlyComponentCreationTest::RunTest(const FString&)
{
    TStrongObjectPtr<UBlueprint> Blueprint(FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(),
        GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("EditorOnlyComponentCreation")),
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));
    USCS_Node* ComponentNode = Blueprint->SimpleConstructionScript->CreateNode(USpringArmComponent::StaticClass(), TEXT("EditorOnlyComponent"));
    ComponentNode->ComponentTemplate->bIsEditorOnly = true;
    ComponentNode->ComponentTemplate->bAutoActivate = !GetDefault<USpringArmComponent>()->bAutoActivate;
    Blueprint->SimpleConstructionScript->AddNode(ComponentNode);
    FKismetEditorUtilities::CompileBlueprint(Blueprint.Get(), EBlueprintCompileOptions::SkipSave);
    const bool bPackageWasDirty = Blueprint->GetOutermost()->IsDirty();
    const int32 OriginalNodeCount = Blueprint->SimpleConstructionScript->GetAllNodes().Num();
    FNativeApiCatalog::BeginGeneration(true);
    FRetainedBlueprintGeneration::Begin({Blueprint.Get()});
    FRetainedBlueprintGeneration::BeginObject(Blueprint.Get());
    const FString Constructor = GEngine->GetEngineSubsystem<UNativizationV2Subsystem>()->GenerateConstructorCppCode(Blueprint.Get());
    FRetainedBlueprintGeneration::EndObject();
    const FString ComponentName = UBlueprintNativizationLibrary::GetUniquePropertyComponentGetterName(ComponentNode->ComponentTemplate, {});
    const int32 CreationPosition = Constructor.Find(ComponentName + TEXT(" = CreateEditorOnlyDefaultSubobject<"));
    const int32 InitializationGuardPosition = Constructor.Find(TEXT("if (IsValid(") + ComponentName + TEXT("))"));
    TestTrue(TEXT("Editor-only components use the editor-only native creation contract"), CreationPosition != INDEX_NONE);
    TestTrue(TEXT("Creation executes before any validity guard for component initialization"),
        InitializationGuardPosition != INDEX_NONE && CreationPosition != INDEX_NONE && CreationPosition < InitializationGuardPosition);
    TestEqual(TEXT("Generation preserves SCS nodes"), Blueprint->SimpleConstructionScript->GetAllNodes().Num(), OriginalNodeCount);
    TestEqual(TEXT("Generation preserves package dirty state"), Blueprint->GetOutermost()->IsDirty(), bPackageWasDirty);
    TestTrue(TEXT("Generation preserves the editor-only source flag"), ComponentNode->ComponentTemplate->bIsEditorOnly);
    return true;
}
