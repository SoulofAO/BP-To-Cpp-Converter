/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#if WITH_DEV_AUTOMATION_TESTS
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationUniqueNameSubsystem.h"
#include "Blueprint/AIAsyncTaskBlueprintProxy.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"
#include "K2Node_AsyncAction.h"
#include "K2Node_CallFunction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "TranslatorObjects/Latens/AsyncTaskTranslatorObject.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAsyncCallbackNamesTest,
    "Project.BlueprintNativization.Generation.AsyncCallbackNames",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAsyncCallbackNamesTest::RunTest(const FString&)
{
    TStrongObjectPtr<UBlueprint> Blueprint(FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(),
        GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("CallbackNames")),
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));
    FEdGraphPinType MemberType;
    MemberType.PinCategory = UEdGraphSchema_K2::PC_Int;
    for (const FName Name : {FName(TEXT("MovementResult")), FName(TEXT("MoveCallbackMovementResult")),
        FName(TEXT("AsyncOnSuccessMovementResult")), FName(TEXT("AsyncOnFailMovementResult"))})
    {
        FBlueprintEditorUtils::AddMemberVariable(Blueprint.Get(), Name, MemberType);
    }
    FKismetEditorUtilities::CompileBlueprint(Blueprint.Get());
    UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint.Get(), TEXT("CallbackGraph"),
        UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    FBlueprintEditorUtils::AddUbergraphPage(Blueprint.Get(), Graph);
    UNativizationV2Subsystem* Subsystem = GEngine->GetEngineSubsystem<UNativizationV2Subsystem>();
    Subsystem->LoadTranslatorObjects();
    UBlueprintNativizationUniqueNameSubsystem* NameRegistry = GEngine->GetEngineSubsystem<UBlueprintNativizationUniqueNameSubsystem>();
    NameRegistry->GetUniqueVariableName(FindFProperty<FProperty>(Blueprint->GeneratedClass, TEXT("MovementResult")));
    TSet<FString> MoveParameters;
    for (int32 NodeIndex = 0; NodeIndex < 3; ++NodeIndex)
    {
        UK2Node* Node = nullptr;
        UTranslatorBPToCppObject* Translator = nullptr;
        if (NodeIndex < 2)
        {
            Node = NewObject<UK2Node>(Graph, LoadClass<UK2Node>(nullptr, TEXT("/Script/AIGraph.K2Node_AIMoveTo")));
            Node->AllocateDefaultPins();
            Translator = LoadClass<UTranslatorBPToCppObject>(nullptr, TEXT("/Script/BlueprintNativizationV2.AIMoveToTranslatorObject"))->GetDefaultObject<UTranslatorBPToCppObject>();
        }
        else
        {
            Node = NewObject<UK2Node_AsyncAction>(Graph);
            FObjectPropertyBase* ProxyClass = FindFProperty<FObjectPropertyBase>(Node->GetClass(), TEXT("ProxyClass"));
            ProxyClass->SetObjectPropertyValue_InContainer(Node, UAIAsyncTaskBlueprintProxy::StaticClass());
            Node->CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, TEXT("OnSuccess"));
            Node->CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, TEXT("OnFail"));
            Node->CreatePin(EGPD_Output, MemberType, TEXT("MovementResult"));
            Translator = GetMutableDefault<UAsyncTaskTranslatorObject>();
        }
        Node->CreateNewGuid();
        Graph->AddNode(Node);
        UK2Node_CallFunction* Consumer = NewObject<UK2Node_CallFunction>(Graph);
        Consumer->CreateNewGuid();
        Graph->AddNode(Consumer);
        UEdGraphPin* Result = Node->FindPinChecked(TEXT("MovementResult"));
        Result->MakeLinkTo(Consumer->CreatePin(EGPD_Input, Result->PinType, TEXT("Value")));
        TArray<FGenerateFunctionStruct> Entries;
        Translator->GenerateNewFunction(Node, {}, Entries, Subsystem);
        TestEqual(TEXT("Both delegate outcomes have callbacks"), Entries.Num(), 2);
        FString FirstParameter;
        for (const FGenerateFunctionStruct& Entry : Entries)
        {
            TestEqual(TEXT("The callback retains its parameter contract"), Entry.CustomParametersString.Num(), 1);
            const FString Declaration = Entry.CustomParametersString[0];
            int32 NameOffset = INDEX_NONE;
            Declaration.FindLastChar(TEXT(' '), NameOffset);
            const FString ParameterName = Declaration.Mid(NameOffset + 1);
            for (TFieldIterator<FProperty> Member(Blueprint->GeneratedClass, EFieldIteratorFlags::ExcludeSuper); Member; ++Member)
            {
                TestNotEqual(TEXT("Callback parameters and generated class members have distinct C++ names"),
                    ParameterName, NameRegistry->GetUniqueVariableName(*Member).ToString());
            }
            FGenerationPreparations Preparations;
            const FString Body = Translator->GenerateCodeFromNode(Node, Entry.EntryPinName, {}, {}, Preparations, Subsystem);
            TestTrue(TEXT("The callback body uses its registered parameter name"),
                Body.Contains(TEXT(" = ") + ParameterName + TEXT(";")));
            if (FirstParameter.IsEmpty())
            {
                FirstParameter = ParameterName;
            }
        }
        if (NodeIndex < 2)
        {
            TestFalse(TEXT("Distinct move node instances retain distinct name registrations"), MoveParameters.Contains(FirstParameter));
            MoveParameters.Add(FirstParameter);
        }
    }
    return true;
}

#endif
