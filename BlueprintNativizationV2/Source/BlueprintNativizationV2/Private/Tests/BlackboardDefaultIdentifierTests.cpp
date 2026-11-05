/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright 2026 AO All Rights Reserved.
 */

#include "TranslatorObjects/NativeValueStructTranslator.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlackboardDefaultIdentifierTest,
    "Project.BlueprintNativization.Generation.BlackboardDefaultIdentifier",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlackboardDefaultIdentifierTest::RunTest(const FString&)
{
    FBlackboardKeySelector Selector{};
    Selector.SelectedKeyName = TEXT("TargetActor");
    FStructTranslationRequest Request;
    Request.Operation = EStructTranslationOperation::PropertyValue;
    Request.Structure = FBlackboardKeySelector::StaticStruct();
    Request.PropertyValue = &Selector;
    Request.OwnerClass = AActor::StaticClass();
    FStructTranslationResult DefaultResult;
    TestTrue(TEXT("The native selector default translates"), GetDefault<UNativeValueStructTranslator>()->Translate(Request, DefaultResult));
    TestFalse(TEXT("The native default identifier needs no reflected assignment"),
        FString::Join(DefaultResult.Preparations, TEXT("\n")).Contains(TEXT("SelectedKeyID")));
    FIntProperty* Identifier = FindFProperty<FIntProperty>(FBlackboardKeySelector::StaticStruct(), TEXT("SelectedKeyID"));
    if (!TestNotNull(TEXT("The selector exposes its saved identifier as an int32 property"), Identifier))
    {
        return false;
    }
    Identifier->SetPropertyValue_InContainer(&Selector, -1);
    FStructTranslationResult SavedResult;
    TestTrue(TEXT("A saved non-default identifier translates"), GetDefault<UNativeValueStructTranslator>()->Translate(Request, SavedResult));
    const FString SavedCode = FString::Join(SavedResult.Preparations, TEXT("\n"));
    TestTrue(TEXT("A non-default identifier retains the private field write"), SavedCode.Contains(TEXT("SelectedKeyID")));
    TestTrue(TEXT("The saved identifier is preserved exactly"), SavedCode.Contains(TEXT(", -1)")));
    return true;
}
