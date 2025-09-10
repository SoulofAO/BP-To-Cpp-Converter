/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/TimelineTranslatorObject.h"
#include "K2Node_Timeline.h"
#include "BlueprintNativizationSubsystem.h"
#include "Components/TimelineComponent.h"
#include "Engine/TimelineTemplate.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "BlueprintNativizationLibrary.h"
#include "Curves/CurveVector.h"
#include "Curves/CurveLinearColor.h"


FString UTimelineTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, TSet<FString>& Preparations, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	FString Content;
 
    if (UK2Node_Timeline* Timeline = Cast<UK2Node_Timeline>(Node))
    {
        FString TimelineComponentName = UBlueprintNativizationLibrary::GetLambdaUniqueVariableNameByNode("Timeline", Node, NativizationV2Subsystem->EntryNodes);

        Node->Pins;

        if (EntryPinName == "Play")
        {
            Content += FString::Format(TEXT("{0}->Play();"), { TimelineComponentName });
        }
        else if (EntryPinName == "Play from Start")
        {
            Content += FString::Format(TEXT("{0}->PlayFromStart();"), { TimelineComponentName });
        }
        else if (EntryPinName == "Stop")
        {
            Content += FString::Format(TEXT("{0}->Stop();"), { TimelineComponentName });
        }
        else if (EntryPinName == "Reverse")
        {
            Content += FString::Format(TEXT("{0}->Reverse();"), { TimelineComponentName });
        }
        else if (EntryPinName == "Reverse from End")
        {
            Content += FString::Format(TEXT("{0}->ReverseFromEnd();"), { TimelineComponentName });
        }
        else if (EntryPinName == "Set New Time")
        {
            UEdGraphPin* NewTimePin = Node->FindPin(TEXT("SetNewTime"));
            FGenerateResultStruct FloatInputResult = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, NewTimePin, 0, MacroStack);
            Content += GenerateNewPreparations(Preparations, FloatInputResult.Preparations);
            Content += FString::Format(TEXT("{0}->SetNewTime({1});"), { TimelineComponentName,  FloatInputResult.Code});
        }
    }

	return Content;
}

FString UTimelineTranslatorObject::GenerateGlobalVariables(UK2Node* InputNode, FString Prefix, UNativizationV2Subsystem* NativizationV2Subsystem)
{
    FString Content;

    if (UK2Node_Timeline* Timeline = Cast<UK2Node_Timeline>(InputNode))
    {
        UTimelineTemplate* TimelineTemplate = InputNode->GetBlueprint()->FindTimelineTemplateByVariableName(Timeline->TimelineName);
        FString TimelineVar = UBlueprintNativizationLibrary::GetLambdaUniqueVariableNameByClass("Timeline", InputNode->GetBlueprint()->GeneratedClass);

        TSet<UCurveBase*> InOutCurves;
        TimelineTemplate->GetAllCurves(InOutCurves);

        for (UCurveBase* Curve : InOutCurves)
        {
            FString VarType = "";

            if (UCurveFloat* CurveFloat = Cast<UCurveFloat>(Curve))
            {
                VarType = "float";
            }
            else if (UCurveVector* CurveVector = Cast<UCurveVector>(Curve))
            {
                VarType = "FVector";
            }
            else if (UCurveLinearColor* CurveLinearColor = Cast<UCurveLinearColor>(Curve))
            {
                VarType = "FLinearColor";
            }
            Content += VarType + " " + UBlueprintNativizationLibrary::GetLambdaUniqueVariableNameByClass(Curve->GetName(), InputNode->GetBlueprint()->GeneratedClass) + ";\n";
        }
    }

    Content += FString::Format(TEXT("UPROPERTY(EditAnywhere, BlueprintReadOnly) \n UTimelineComponent* {1};\n"),
        { UBlueprintNativizationLibrary::GetLambdaUniqueVariableNameByClass("Timeline", InputNode->GetBlueprint()->GeneratedClass)});
    Content += FString::Format(TEXT("UPROPERTY() \n ETimelineDirection {1};\n"),
        { UBlueprintNativizationLibrary::GetLambdaUniqueVariableNameByClass("TimelineDirection",InputNode->GetBlueprint()->GeneratedClass)});

    return Content;
}

UCurveBase* UTimelineTranslatorObject::GetCurveByName(UK2Node_Timeline* Timeline, FString Name)
{
    TSet<UCurveBase*> InOutCurves;
    UTimelineTemplate* TimelineTemplate = Timeline->GetBlueprint()->FindTimelineTemplateByVariableName(Timeline->TimelineName);
    TimelineTemplate->GetAllCurves(InOutCurves);
    for (UCurveBase* Curve : InOutCurves)
    {
        if (Curve->GetName() == Name)
        {
            return Curve;
        }
    }
    return nullptr;
}

FString UTimelineTranslatorObject::GenerateConstruction(UK2Node* InputNode, FString Prefix, UNativizationV2Subsystem* NativizationV2Subsystem)
{
    FString Content;

    if (UK2Node_Timeline* Timeline = Cast<UK2Node_Timeline>(InputNode))
    {
        UTimelineTemplate* TimelineTemplate = InputNode->GetBlueprint()->FindTimelineTemplateByVariableName(Timeline->TimelineName);

        FString TimelineVar = UBlueprintNativizationLibrary::GetLambdaUniqueVariableNameByClass("Timeline", InputNode->GetBlueprint()->GeneratedClass);

        Content += FString::Format(TEXT("UTimelineComponent* {0} = CreateDefaultSubobject<UTimelineComponent>(TEXT(\"T{1}\"));\n"),
            { *TimelineVar, *Timeline->TimelineName.ToString() });

        TSet<UCurveBase*> InOutCurves;
        TimelineTemplate->GetAllCurves(InOutCurves);

        int32 CurveIndex = 0;
        if (NativizationV2Subsystem->LeftAllAssetRefInBlueprint)
        {
            for (UCurveBase* InOutCurve : InOutCurves)
            {
                FString BaseName = Timeline->TimelineName.ToString();
                FString AssetBase = FString::Format(TEXT("{0}_Curve{1}"), { *BaseName, FString::FromInt(CurveIndex) });
                FString PackageName = FString::Format(TEXT("/Game/SavedCurves/{0}"), { *AssetBase });
                UPackage* Package = CreatePackage(*PackageName);

                FName DuplicateName = FName(*AssetBase);
                UCurveBase* Duplicate = DuplicateObject(InOutCurve, Package, DuplicateName);

                FAssetRegistryModule::AssetCreated(Duplicate);
                Duplicate->MarkPackageDirty();

                FString PackageFileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
                FSavePackageArgs Args;
                UPackage::SavePackage(Package, Duplicate, *PackageFileName, Args);

                FString AssetPath = FString::Format(TEXT("/Game/SavedCurves/{0}.{0}"), { *AssetBase });
                FString CurveVarName = FString::Format(TEXT("{0}_GeneratedCurve{1}"), { *TimelineVar, FString::FromInt(CurveIndex) });

                Content += FString::Format(TEXT("UCurveBase* {0} = LoadObject<UCurveBase>(nullptr, TEXT(\"{1}\"));\n"),
                    { *CurveVarName, *AssetPath });

                Content += FString::Format(TEXT("if({0} == nullptr) { UE_LOG(LogTemp, Warning, TEXT(\"Failed to load curve {1}\")); }\n"),
                    { *CurveVarName, *AssetPath });

                CurveIndex++;
            }
        }
        else
        {
            for (UCurveBase* InOutCurve : InOutCurves)
            {
                if (UCurveFloat* FloatCurve = Cast<UCurveFloat>(InOutCurve))
                {
                    FString NewCurveVar = FloatCurve->GetName();

                    Content += FString::Format(TEXT("UCurveFloat* {0} = NewObject<UCurveFloat>(this, UCurveFloat::StaticClass());\n"),
                        { *NewCurveVar });

                    const FRichCurve& Src = FloatCurve->FloatCurve;
                    for (const FRichCurveKey& Key : Src.Keys)
                    {
                        FString TimeStr = FString::SanitizeFloat(Key.Time);
                        FString ValueStr = FString::SanitizeFloat(Key.Value);

                        Content += FString::Format(TEXT("{0}->FloatCurve.AddKey({1}f, {2}f);\n"),
                            { *NewCurveVar, *TimeStr, *ValueStr });
                    }
                }
                else if (UCurveVector* VecCurve = Cast<UCurveVector>(InOutCurve))
                {
                    FString NewCurveVar = VecCurve->GetName();
                    Content += FString::Format(TEXT("UCurveVector* {0} = NewObject<UCurveVector>(this, UCurveVector::StaticClass());\n"),
                        { *NewCurveVar });

                    const FRichCurve& SrcX = VecCurve->FloatCurves[0];
                    const FRichCurve& SrcY = VecCurve->FloatCurves[1];
                    const FRichCurve& SrcZ = VecCurve->FloatCurves[2];

                    for (const FRichCurveKey& Key : SrcX.Keys)
                    {
                        FString TimeStr = FString::SanitizeFloat(Key.Time);
                        float ValX = Key.Value;
                        float ValY = SrcY.Eval(Key.Time);
                        float ValZ = SrcZ.Eval(Key.Time);

                        FString ValXStr = FString::SanitizeFloat(ValX);
                        FString ValYStr = FString::SanitizeFloat(ValY);
                        FString ValZStr = FString::SanitizeFloat(ValZ);

                        Content += FString::Format(TEXT("{0}->FloatCurves[0].AddKey({1}f, {2}f);\n"), { *NewCurveVar, *TimeStr, *ValXStr });
                        Content += FString::Format(TEXT("{0}->FloatCurves[1].AddKey({1}f, {2}f);\n"), { *NewCurveVar, *TimeStr, *ValYStr });
                        Content += FString::Format(TEXT("{0}->FloatCurves[2].AddKey({1}f, {2}f);\n"), { *NewCurveVar, *TimeStr, *ValZStr });
                    }


                }
                else if (UCurveLinearColor* CurveLinearColor = Cast<UCurveLinearColor>(InOutCurve))
                {
                    FString NewCurveVar = CurveLinearColor->GetName();
                    Content += FString::Format(TEXT("UCurveLinearColor* {0} = NewObject<UCurveLinearColor>(this, UCurveLinearColor::StaticClass());\n"),
                        { *NewCurveVar });

                    const FRichCurve& SrcR = CurveLinearColor->FloatCurves[0];
                    const FRichCurve& SrcG = CurveLinearColor->FloatCurves[1];
                    const FRichCurve& SrcB = CurveLinearColor->FloatCurves[2];
                    const FRichCurve& SrcA = CurveLinearColor->FloatCurves[3];

                    for (const FRichCurveKey& Key : SrcR.Keys)
                    {
                        FString TimeStr = FString::SanitizeFloat(Key.Time);
                        float ValR = Key.Value;
                        float ValG = SrcG.Eval(Key.Time);
                        float ValB = SrcB.Eval(Key.Time);
                        float ValA = SrcA.Eval(Key.Time);
                        FString ValRStr = FString::SanitizeFloat(ValR);
                        FString ValGStr = FString::SanitizeFloat(ValG);
                        FString ValBStr = FString::SanitizeFloat(ValB);
                        FString ValAStr = FString::SanitizeFloat(ValA);
                        Content += FString::Format(TEXT("{0}->FloatCurves[0].AddKey({1}f, {2}f);\n"), { *NewCurveVar, *TimeStr, *ValRStr });
                        Content += FString::Format(TEXT("{0}->FloatCurves[1].AddKey({1}f, {2}f);\n"), { *NewCurveVar, *TimeStr, *ValGStr });
                        Content += FString::Format(TEXT("{0}->FloatCurves[2].AddKey({1}f, {2}f);\n"), { *NewCurveVar, *TimeStr, *ValBStr });
                        Content += FString::Format(TEXT("{0}->FloatCurves[3].AddKey({1}f, {2}f);\n"), { *NewCurveVar, *TimeStr, *ValAStr });
                    }
                }
                else
                {
                    Content += FString::Format(TEXT("UE_LOG(LogTemp, Warning, TEXT(\"Unsupported curve type for timeline {0}\"));\n"), { *Timeline->TimelineName.ToString() });
                }

                CurveIndex++;
            }
        }

        UEdGraphPin* UpdateExecPin = Timeline->FindPin(TEXT("Update"));
        UEdGraphPin* FinishExecPin = Timeline->FindPin(TEXT("Update"));

        TArray<UK2Node*> EmptyMacroStack;
        UEdGraphPin* NextUpdateExecPin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(UpdateExecPin, 0, true, EmptyMacroStack, NativizationV2Subsystem);
        UEdGraphPin* NextFinishExecPin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(UpdateExecPin, 0, true, EmptyMacroStack, NativizationV2Subsystem);
        
        TArray<UEdGraphPin*> OutPins = UBlueprintNativizationLibrary::GetFilteredPins(Timeline, EPinOutputOrInputFilter::Ouput, EPinExcludeFilter::ExecPin, EPinIncludeOnlyFilter::None);

        bool AnyPinLinked = false;
        for (UEdGraphPin* OutPin : OutPins)
        {
            if (UBlueprintNativizationLibrary::CheckAnySubPinLinked(OutPin))
            {
                AnyPinLinked = true;
            }
        }
        
        if (NextUpdateExecPin || AnyPinLinked)
        {
            FString UpdateEntryName = UBlueprintNativizationLibrary::GetUniqueEntryNodeName(Cast<UK2Node>(NextUpdateExecPin->GetOwningNode()), NativizationV2Subsystem->EntryNodes, TEXT("UpdateTimeline"));
            FString WorldTickHandleVarName = UBlueprintNativizationLibrary::GenerateLambdaConstructorUniqueVariableName(Timeline->GetClass(), "WorldTickHandle");


            FString UpdateCurvesCode;
            UpdateCurvesCode += "TSet<UCurveBase*> InOutCurves;";

            for (UCurveBase* Curve : InOutCurves)
            {
                FString Name = UBlueprintNativizationLibrary::GetLambdaUniqueVariableNameByClass(Curve->GetName(), InputNode->GetBlueprint()->GeneratedClass);

                if (Cast<UCurveFloat>(Curve))
                {
                    UpdateCurvesCode += FString::Format(TEXT(
                    "InOutCurves.Empty();\n"
                        "{0}->GetAllCurves(InOutCurves);\n"
                        "for(UCurveBase* Curve: InOutCurves)\n"
                        "{\n"
                        "if(Curve->GetName() == {1})\n"
                        "{\n"
                        "if (UCurveFloat* FloatCurve = Cast<UCurveFloat>(InOutCurve))\n"
                        "{\n"
                        "{1} = Curve->GetFloatValue({0}->GetPlaybackPosition());\n"
                        "}\n"
                        "}\n"
                        "}\n"
                    ), { TimelineVar, Curve->GetName() });
                }
                else if (Cast<UCurveVector>(Curve))
                {
                    UpdateCurvesCode += FString::Format(TEXT(
                        "InOutCurves.Empty();\n"
                        "{0}->GetAllCurves(InOutCurves);\n"
                        "for(UCurveBase* Curve: InOutCurves)\n"
                        "{\n"
                        "if(Curve->GetName() == {1})\n"
                        "{\n"
                        "if (UCurveVector* VectorCurve = Cast<UCurveVector>(InOutCurve))\n"
                        "{\n"
                        "{1} = Curve->GetVectorValue({0}->GetPlaybackPosition());\n"
                        "}\n"
                        "}\n"
                        "}\n"
                    ), { TimelineVar, Curve->GetName() });
                }
                else if (Cast<UCurveLinearColor>(Curve))
                {
                    UpdateCurvesCode += FString::Format(TEXT(
                        "InOutCurves.Empty();\n"
                        "{0}->GetAllCurves(InOutCurves);\n"
                        "for(UCurveBase* Curve: InOutCurves)\n"
                        "{\n"
                        "if(Curve->GetName() == {1})\n"
                        "{\n"
                        "if (UCurveLinearColor* LinearColorCurve = Cast<UCurveLinearColor>(InOutCurve))\n"
                        "{\n"
                        "{1} = Curve->GetLinearColorValue({0}->GetPlaybackPosition());\n"
                        "}\n"
                        "}\n"
                        "}\n"
                    ), { TimelineVar, Curve->GetName() });
                }
            }


            Content += FString::Format(TEXT(
                    "{0} = FWorldDelegates::OnWorldTickStart.AddLambda([{2}](UWorld* World, ELevelTick TickType, float DeltaSeconds)\n"
                    "{\n"
                    "    if (!World || TickType != LEVELTICK_All) return;\n"
                    "    if ({2})\n"
                    "    {\n"
                    "        if({2}->IsPlaying()) \n"
                    "        {\n"
                    "           {3}\n"
                    "           {1}();\n"
                    "        }\n"
                    "    }\n"
                    "    else\n"
                    "    {\n"
                    "        FWorldDelegates::OnWorldTickStart.Remove({1});\n"
                    "        {1}.Reset();\n"
                    "    }\n"
                    "});\n"
                ), { *WorldTickHandleVarName ,*UpdateEntryName, TimelineVar, UpdateCurvesCode });

        }
        if (NextFinishExecPin)
        {
            FString FinishEntryName = UBlueprintNativizationLibrary::GetUniqueEntryNodeName(Cast<UK2Node>(NextFinishExecPin->GetOwningNode()), NativizationV2Subsystem->EntryNodes, TEXT("FinishTimeline"));
            FString FinishedFunctionVarName = UBlueprintNativizationLibrary::GenerateLambdaConstructorUniqueVariableName(Timeline->GetClass(), "FinishedFunction");
            
            Content += FString::Format(TEXT(
                "FOnTimelineEvent {0};"
                "{0}.BindUFunction(this, FName(\"{1}\"));"
                "{2}->SetTimelineFinishedFunc({0});"
            ), { FinishedFunctionVarName, FinishEntryName, TimelineVar});
        }
    }
    return Content;
}



bool UTimelineTranslatorObject::CanContinueCodeGeneration(UK2Node* InputNode, FString EntryPinName)
{
	return false;
}

TSet<FString> UTimelineTranslatorObject::GenerateHeaderIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	return {"#include \"Components/TimelineComponent.h\""};
}

FGenerateResultStruct UTimelineTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
    if (UK2Node_Timeline* Timeline = Cast<UK2Node_Timeline>(Node))
    {
        if (Pin->GetName() == "Direction")
        {
            return UBlueprintNativizationLibrary::GetLambdaUniqueVariableNameByClass("TimelineDirection", Timeline->GetBlueprint()->GeneratedClass);
        }
        else
        {
            return UBlueprintNativizationLibrary::GetLambdaUniqueVariableNameByClass(Pin->GetName(), Timeline->GetBlueprint()->GeneratedClass);
        }
    }

	return FGenerateResultStruct();
}
