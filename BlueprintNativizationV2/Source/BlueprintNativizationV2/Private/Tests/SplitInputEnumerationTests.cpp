/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright 2026 AO All Rights Reserved.
 */

#if WITH_DEV_AUTOMATION_TESTS
#include "BlueprintNativizationSubsystem.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Engine.h"
#include "Engine/Scene.h"
#include "GameFramework/Actor.h"
#include "K2Node_MakeArray.h"
#include "K2Node_MakeSet.h"
#include "K2Node_Select.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "NativeApiCatalog.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSplitInputEnumerationTest,
    "Project.BlueprintNativization.Generation.SplitInputEnumeration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSplitInputEnumerationTest::RunTest(const FString&)
{
    TStrongObjectPtr<UBlueprint> Blueprint(FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(),
        GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("SplitInputEnumeration")),
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));
    UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint.Get(), TEXT("SplitInputs"),
        UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    FBlueprintEditorUtils::AddUbergraphPage(Blueprint.Get(), Graph);
    UNativizationV2Subsystem* Subsystem = GEngine->GetEngineSubsystem<UNativizationV2Subsystem>();
    Subsystem->LoadTranslatorObjects();
    FNativeApiCatalog::BeginGeneration();
    const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
    FString Probe = TEXT("#include \"CoreMinimal.h\"\n#include \"Engine/Scene.h\"\n");
    for (int32 CaseIndex = 0; CaseIndex < 3; ++CaseIndex)
    {
        UK2Node* Node = nullptr;
        UEdGraphPin* Output = nullptr;
        UEdGraphPin* SplitInput = nullptr;
        FString ReturnType;
        FString FunctionName;
        if (CaseIndex == 0)
        {
            UK2Node_Select* Select = NewObject<UK2Node_Select>(Graph);
            Select->AllocateDefaultPins();
            FEdGraphPinType RotatorType;
            RotatorType.PinCategory = UEdGraphSchema_K2::PC_Struct;
            RotatorType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
            Select->GetReturnValuePin()->PinType = RotatorType;
            Select->GetIndexPin()->PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
            Select->GetIndexPin()->DefaultValue = TEXT("0");
            TArray<UEdGraphPin*> Options;
            Select->GetOptionPins(Options);
            for (UEdGraphPin* Option : Options)
            {
                Option->PinType = RotatorType;
                Option->DefaultValue = TEXT("(Pitch=10,Yaw=20,Roll=30)");
            }
            Node = Select;
            Output = Select->GetReturnValuePin();
            SplitInput = Options[0];
            ReturnType = TEXT("FRotator");
            FunctionName = TEXT("VerifySplitSelect");
        }
        else
        {
            FEdGraphPinType ElementType;
            ElementType.PinCategory = UEdGraphSchema_K2::PC_Struct;
            if (CaseIndex == 1)
            {
                Node = NewObject<UK2Node_MakeArray>(Graph);
                ElementType.PinSubCategoryObject = FWeightedBlendable::StaticStruct();
                ReturnType = TEXT("TArray<FWeightedBlendable>");
                FunctionName = TEXT("VerifySplitArray");
            }
            else
            {
                Node = NewObject<UK2Node_MakeSet>(Graph);
                ElementType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
                ReturnType = TEXT("TSet<FVector>");
                FunctionName = TEXT("VerifySplitSet");
            }
            Node->AllocateDefaultPins();
            for (UEdGraphPin* Pin : Node->Pins)
            {
                Pin->PinType = ElementType;
                if (Pin->Direction == EGPD_Output)
                {
                    Output = Pin;
                    Output->PinType.ContainerType = EPinContainerType::Array;
                    if (CaseIndex == 2)
                    {
                        Output->PinType.ContainerType = EPinContainerType::Set;
                    }
                }
                else
                {
                    SplitInput = Pin;
                }
            }
        }
        Node->CreateNewGuid();
        Graph->AddNode(Node);
        Schema->SplitPin(SplitInput, false);
        TestTrue(TEXT("The fixture contains a genuinely split input"), !SplitInput->SubPins.IsEmpty());
        for (UEdGraphPin* Child : SplitInput->SubPins)
        {
            if (CaseIndex == 1 && Child->PinName.ToString().EndsWith(TEXT("_Weight")))
            {
                Child->DefaultValue = TEXT("0.75");
            }
            else if (CaseIndex == 2)
            {
                Child->DefaultValue = TEXT("3.0");
            }
        }
        const int32 OriginalPinCount = Node->Pins.Num();
        const int32 OriginalChildCount = SplitInput->SubPins.Num();
        const FGenerateResultStruct Result = Subsystem->FindTranslatorForNode(Node)->GenerateInputParameterCodeForNode(
            Node, Output, 0, {}, Subsystem);
        TestEqual(TEXT("Translation retains the original pin count"), Node->Pins.Num(), OriginalPinCount);
        TestEqual(TEXT("Translation retains split child pins"), SplitInput->SubPins.Num(), OriginalChildCount);
        Probe += ReturnType + TEXT(" ") + FunctionName + TEXT("()\n{\n") +
            FString::Join(Result.Preparations.Array(), TEXT("\n")) + TEXT("\nreturn ") + Result.Code + TEXT(";\n}\n");
    }
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("SplitInputEnumerationTests");
    IFileManager::Get().MakeDirectory(*Directory, true);
    TestTrue(TEXT("The generated value probe is saved"), FFileHelper::SaveStringToFile(Probe,
        *(Directory / TEXT("GeneratedValues.cpp")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    TestEqual(TEXT("Split inputs generate without source errors"), FNativeApiCatalog::GetErrors().Num(), 0);
    return true;
}
#endif
