#if WITH_DEV_AUTOMATION_TESTS
#include "BlueprintNativizationSubsystem.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"
#include "K2Node_ConvertAsset.h"
#include "K2Node_CallFunction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "NativeApiCatalog.h"
#include "NativeGenerationContext.h"
#include "RetainedBlueprintGeneration.h"
#include "TranslatorObjects/ConvertAssetTranslatorObject.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConvertAssetGenerationTest,
    "Project.BlueprintNativization.Generation.ConvertAsset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConvertAssetGenerationTest::RunTest(const FString&)
{
    UPackage* Package = CreatePackage(TEXT("/Game/ConverterAssetConversionTests/Conversions"));
    TStrongObjectPtr<UBlueprint> Blueprint(FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(),
        Package, MakeUniqueObjectName(Package, UBlueprint::StaticClass(), TEXT("AssetConversions")),
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));
    UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint.Get(), TEXT("Conversions"),
        UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    FBlueprintEditorUtils::AddUbergraphPage(Blueprint.Get(), Graph);
    const TArray<FName> SourceCategories = {UEdGraphSchema_K2::PC_Object, UEdGraphSchema_K2::PC_Class,
        UEdGraphSchema_K2::PC_SoftObject, UEdGraphSchema_K2::PC_SoftClass, UEdGraphSchema_K2::PC_SoftClass};
    const TArray<FName> DestinationCategories = {UEdGraphSchema_K2::PC_SoftObject, UEdGraphSchema_K2::PC_SoftClass,
        UEdGraphSchema_K2::PC_Object, UEdGraphSchema_K2::PC_Class, UEdGraphSchema_K2::PC_Class};
    const TArray<FString> Contracts = {TEXT("Conv_ObjectToSoftObjectReference"), TEXT("Conv_ClassToSoftClassReference"),
        TEXT("Conv_SoftObjectReferenceToObject"), TEXT("Conv_SoftClassReferenceToClass"), TEXT("Conv_SoftClassReferenceToClass")};
    UNativizationV2Subsystem* Subsystem = GEngine->GetEngineSubsystem<UNativizationV2Subsystem>();
    Subsystem->LoadTranslatorObjects();
    FNativeApiCatalog::BeginGeneration();
    FRetainedBlueprintGeneration::Begin({Blueprint.Get()});
    FRetainedBlueprintGeneration::BeginObject(Blueprint.Get());
    for (int32 ConversionIndex = 0; ConversionIndex < SourceCategories.Num(); ++ConversionIndex)
    {
        UK2Node_ConvertAsset* Node = NewObject<UK2Node_ConvertAsset>(Graph);
        Node->CreateNewGuid();
        Node->AllocateDefaultPins();
        Graph->AddNode(Node);
        UEdGraphPin* Input = Node->FindPinChecked(TEXT("Input"), EGPD_Input);
        UEdGraphPin* Output = Node->FindPinChecked(TEXT("Output"), EGPD_Output);
        Input->PinType.PinCategory = SourceCategories[ConversionIndex];
        Output->PinType.PinCategory = DestinationCategories[ConversionIndex];
        Input->PinType.PinSubCategoryObject = AActor::StaticClass();
        Output->PinType.PinSubCategoryObject = AActor::StaticClass();
        if (ConversionIndex == 4)
        {
            Output->PinType.PinSubCategoryObject = Blueprint->GeneratedClass.Get();
        }
        UK2Node_CallFunction* Consumer = NewObject<UK2Node_CallFunction>(Graph);
        Consumer->CreateNewGuid();
        Graph->AddNode(Consumer);
        UEdGraphPin* ConsumerInput = Consumer->CreatePin(EGPD_Input, Output->PinType, TEXT("Converted"));
        Output->MakeLinkTo(ConsumerInput);
        UTranslatorBPToCppObject* RegisteredTranslator = Subsystem->FindTranslatorForNode(Node);
        TestTrue(TEXT("The standard translator registry resolves ConvertAsset"),
            RegisteredTranslator && RegisteredTranslator->IsA<UConvertAssetTranslatorObject>());
        const FEdGraphPinType OriginalInputType = Input->PinType;
        const FEdGraphPinType OriginalOutputType = Output->PinType;
        const int32 OriginalNodeCount = Graph->Nodes.Num();
        FNativeGenerationContext::BeginObject();
        const FGenerateResultStruct Result = Subsystem->GenerateInputParameterCodeForNode(Consumer, ConsumerInput, 0, {});
        FString Implementation = TEXT("#include \"CoreMinimal.h\"\n");
        FNativeGenerationContext::CompleteImplementation(Implementation);
        TestTrue(TEXT("The soft pointer dependency is a complete include directive"), Implementation.Contains(TEXT("#include \"UObject/SoftObjectPtr.h\"")));
        TestFalse(TEXT("No bare header paths are emitted as C++ code"), Implementation.Contains(TEXT("\nUObject/SoftObjectPtr.h\n")));
        TestTrue(Contracts[ConversionIndex] + TEXT(" uses its public native contract"), Result.Code.Contains(Contracts[ConversionIndex]));
        TestFalse(TEXT("Asset conversion does not introduce reflection dispatch"), Result.Code.Contains(TEXT("ProcessEvent")));
        TestFalse(TEXT("Soft resolution does not synchronously load assets"), Result.Code.Contains(TEXT("LoadSynchronous")));
        if (ConversionIndex == 4)
        {
            TestTrue(TEXT("A converted class constraint uses its native type identity"),
                Result.Code.Contains(FRetainedBlueprintGeneration::GetClassName(Blueprint->GeneratedClass) + TEXT("::StaticClass()")));
        }
        TestTrue(TEXT("The original input type is unchanged"), Input->PinType == OriginalInputType);
        TestTrue(TEXT("The original output type is unchanged"), Output->PinType == OriginalOutputType);
        TestEqual(TEXT("No intermediate graph nodes are created"), Graph->Nodes.Num(), OriginalNodeCount);
        TestTrue(TEXT("Original pin links are preserved"), Input->LinkedTo.IsEmpty() &&
            Output->LinkedTo.Num() == 1 && Output->LinkedTo[0] == ConsumerInput);
    }
    FRetainedBlueprintGeneration::EndObject();
    FRetainedBlueprintGeneration::Begin({});
    TestEqual(TEXT("The conversion contracts produce no source errors"), FNativeApiCatalog::GetErrors().Num(), 0);
    return true;
}

#endif
