/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright 2026 AO All Rights Reserved.
 */

#include "UObject/Package.h"
#include "BlueprintComponentRepair.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlueprintUnmatchedComponentTest,
    "Project.BlueprintNativization.Repair.UnmatchedConstructionComponent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintUnmatchedComponentTest::RunTest(const FString&)
{
    TStrongObjectPtr<UBlueprint> Blueprint(FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(),
        GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("ComponentRepairFixture")),
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));
    USCS_Node* LightNode = Blueprint->SimpleConstructionScript->CreateNode(USpotLightComponent::StaticClass(), TEXT("SpotLight"));
    Blueprint->SimpleConstructionScript->AddNode(LightNode);
    FKismetEditorUtilities::CompileBlueprint(Blueprint.Get());
    TStrongObjectPtr<UBlueprint> ChildBlueprint(FKismetEditorUtilities::CreateBlueprint(Blueprint->GeneratedClass,
        GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("ComponentRepairChild")),
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));
    FKismetEditorUtilities::CompileBlueprint(ChildBlueprint.Get());
    const UWorld::InitializationValues Initialization = UWorld::InitializationValues().AllowAudioPlayback(false)
        .RequiresHitProxies(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false)
        .ShouldSimulatePhysics(false).SetTransactional(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Initialization);
    GEngine->CreateNewWorldContext(EWorldType::Editor).SetCurrentWorld(World);
    AActor* Actor = World->SpawnActor<AActor>(ChildBlueprint->GeneratedClass);
    if (!TestNotNull(TEXT("Blueprint actor spawns"), Actor))
    {
        World->DestroyWorld(false);
        GEngine->DestroyWorldContext(World);
        return false;
    }
    TestTrue(TEXT("Inherited SCS components are accepted"), FBlueprintComponentRepair::FindUnmatchedConstructionComponents(Actor).IsEmpty());
    UPointLightComponent* InstanceLight = NewObject<UPointLightComponent>(Actor, TEXT("InstanceLight"));
    Actor->AddInstanceComponent(InstanceLight);
    Actor->AddOwnedComponent(InstanceLight);
    InstanceLight->SetIntensity(123.0f);
    InstanceLight->RegisterComponent();
    UPointLightComponent* ObsoleteLight = NewObject<UPointLightComponent>(Actor, TEXT("ObsoleteLight"));
    ObsoleteLight->CreationMethod = EComponentCreationMethod::SimpleConstructionScript;
    Actor->AddOwnedComponent(ObsoleteLight);
    ObsoleteLight->RegisterComponent();
    const TArray<UActorComponent*> Unmatched = FBlueprintComponentRepair::FindUnmatchedConstructionComponents(Actor);
    TestEqual(TEXT("Only the unmatched SCS component is reported"), Unmatched.Num(), 1);
    TestTrue(TEXT("The unmatched component is identified without mutation"), Unmatched.Contains(ObsoleteLight));
    TestTrue(TEXT("Inspection preserves the creation method"), ObsoleteLight->CreationMethod == EComponentCreationMethod::SimpleConstructionScript);
    Actor->RerunConstructionScripts();
    TestTrue(TEXT("Standard reconstruction resolves the mismatch"), FBlueprintComponentRepair::FindUnmatchedConstructionComponents(Actor).IsEmpty());
    TestFalse(TEXT("The obsolete SCS component is not retained"), Actor->GetComponents().Contains(ObsoleteLight));
    TestTrue(TEXT("The true instance component survives reconstruction"), Actor->GetComponents().Contains(InstanceLight));
    TestEqual(TEXT("Instance component values survive"), InstanceLight->Intensity, 123.0f);
    TestTrue(TEXT("The current inherited SCS component exists"), Actor->FindComponentByClass<USpotLightComponent>() != nullptr);
    Actor->Destroy();
    World->DestroyWorld(false);
    GEngine->DestroyWorldContext(World);
    return true;
}
#endif
