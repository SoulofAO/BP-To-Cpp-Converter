#include "BlueprintNativizationUniqueNameSubsystem.h"
#include "BlueprintNativizationLibrary.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_ExecutionSequence.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"
#include "GameFramework/HUD.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "UObject/Interface.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlueprintReflectedTypeNameTest,
    "Project.BlueprintNativization.Generation.ReflectedTypeNames",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintReflectedTypeNameTest::RunTest(const FString&)
{
    UPackage* Package = CreatePackage(*(TEXT("/Temp/ConverterTypeNames_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    TStrongObjectPtr<UBlueprint> Actor(FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), Package,
        TEXT("BP_SharedReflectionName"), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));
    TStrongObjectPtr<UBlueprint> Interface(FKismetEditorUtilities::CreateBlueprint(UInterface::StaticClass(), Package,
        TEXT("BPI_SharedReflectionName"), BPTYPE_Interface, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));
    TStrongObjectPtr<UBlueprint> HudInterface(FKismetEditorUtilities::CreateBlueprint(UInterface::StaticClass(), Package,
        TEXT("BPI_HUD"), BPTYPE_Interface, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));
    AHUD::StaticClass()->GetDefaultObject();
    UBlueprintNativizationUniqueNameSubsystem* Names = GEngine->GetEngineSubsystem<UBlueprintNativizationUniqueNameSubsystem>();
    Names->Reset();
    Names->BeginNameLookup();
    const FString InterfaceName = Names->GetUniqueFieldName(Interface->GeneratedClass).ToString();
    FString ActorName = Names->GetUniqueFieldName(Actor->GeneratedClass).ToString();
    ActorName.RemoveFromStart(TEXT("A"));
    TestNotEqual(TEXT("Actor and interface have distinct reflected names"), FName(*ActorName), FName(*InterfaceName));
    const FString HudName = Names->GetUniqueFieldName(HudInterface->GeneratedClass).ToString();
    TestNotEqual(TEXT("Generated interface does not reuse a native actor's reflected name"), FName(*HudName), FName(TEXT("HUD")));
    Names->EndNameLookup();
    Names->Reset();
    Names->BeginNameLookup();
    ActorName = Names->GetUniqueFieldName(Actor->GeneratedClass).ToString();
    ActorName.RemoveFromStart(TEXT("A"));
    const FString InterfaceAfterActor = Names->GetUniqueFieldName(Interface->GeneratedClass).ToString();
    TestNotEqual(TEXT("Reflection name uniqueness is independent of registration order"), FName(*ActorName), FName(*InterfaceAfterActor));
    Names->EndNameLookup();
    Names->Reset();
    TStrongObjectPtr<UFunction> Function(NewObject<UFunction>(Actor->GeneratedClass, TEXT("ParameterScopeFixture")));
    TUniquePtr<FDoubleProperty> Parameter = MakeUnique<FDoubleProperty>(Function.Get(), TEXT("CameraBoom"), RF_Public);
    Parameter->SetPropertyFlags(CPF_Parm);
    FRegisterVariable PersistentMembers(Actor->GeneratedClass, nullptr, nullptr, true);
    PersistentMembers.PropertyNamePairs.Add(FPropertyNamePair(nullptr, nullptr, TEXT("CameraBoom"), nullptr, FString()));
    Names->UniqueVarNamesArray.Add(MoveTemp(PersistentMembers));
    TestNotEqual(TEXT("Function parameters do not shadow synthesized persistent members"),
        Names->GetUniqueVariableName(Parameter.Get()), FName(TEXT("CameraBoom")));
    Names->Reset();
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlueprintContinuationTraversalTest,
    "Project.BlueprintNativization.Generation.ContinuationTraversal",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintContinuationTraversalTest::RunTest(const FString&)
{
    TStrongObjectPtr<UEdGraph> Graph(NewObject<UEdGraph>());
    UK2Node_ExecutionSequence* Entry = NewObject<UK2Node_ExecutionSequence>(Graph.Get());
    UK2Node_ExecutionSequence* Middle = NewObject<UK2Node_ExecutionSequence>(Graph.Get());
    UK2Node_ExecutionSequence* Last = NewObject<UK2Node_ExecutionSequence>(Graph.Get());
    Entry->AllocateDefaultPins();
    Middle->AllocateDefaultPins();
    Last->AllocateDefaultPins();
    Entry->GetThenPinGivenIndex(0)->MakeLinkTo(Middle->GetExecPin());
    Middle->GetThenPinGivenIndex(0)->MakeLinkTo(Last->GetExecPin());
    const TArray<UK2Node*> Continuation = UBlueprintNativizationLibrary::GetAllExecuteNodesInFunction(
        Entry, TArray<FGenerateFunctionStruct>(), UEdGraphSchema_K2::PN_Execute.ToString());
    TestTrue(TEXT("Continuation input does not stop downstream dependency discovery"), Continuation.Contains(Last));
    const TArray<UK2Node*> OutputEntry = UBlueprintNativizationLibrary::GetAllExecuteNodesInFunction(
        Entry, TArray<FGenerateFunctionStruct>(), Entry->GetThenPinGivenIndex(0)->PinName.ToString());
    TestTrue(TEXT("Entry output selection does not filter subsequent nodes"), OutputEntry.Contains(Last));
    return true;
}
#endif
