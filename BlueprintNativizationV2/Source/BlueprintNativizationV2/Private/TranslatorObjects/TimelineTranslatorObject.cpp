/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/TimelineTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"
#include "Engine/TimelineTemplate.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveVector.h"
#include "Curves/CurveLinearColor.h"
#include "RetainedBlueprintGeneration.h"
#include "NativeApiCatalog.h"
#include "Components/TimelineComponent.h"

void UTimelineTranslatorObject::GenerateNewFunction(UK2Node* InputNode, TArray<UK2Node*> Path,
    TArray<FGenerateFunctionStruct>& EntryNodes, UNativizationV2Subsystem* Subsystem)
{
    UK2Node_Timeline* Timeline = CastChecked<UK2Node_Timeline>(InputNode);
    for (UEdGraphPin* Pin : Timeline->Pins)
    {
        if (Pin->Direction != EGPD_Output || Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
        {
            continue;
        }
        TArray<UK2Node*> MacroStack;
        UEdGraphPin* Target = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Pin, 0, false, MacroStack, Subsystem);
        if (!Target)
        {
            continue;
        }
        if (EntryNodes.ContainsByPredicate([&](const FGenerateFunctionStruct& Entry)
        {
            return Entry.Node == Target->GetOwningNode() && Entry.bIsLatentContinuation && Entry.EntryPinName == Target->GetName();
        }))
        {
            continue;
        }
        FGenerateFunctionStruct Entry;
        Entry.Node = CastChecked<UK2Node>(Target->GetOwningNode());
        Entry.Name = *Subsystem->GetNodeStateName(Entry.Node, Timeline->TimelineName.ToString() + Pin->GetName());
        Entry.bIsLatentContinuation = true;
        Entry.EntryPinName = Target->GetName();
        EntryNodes.Add(Entry);
    }
}

FString UTimelineTranslatorObject::GenerateGlobalVariables(UK2Node* InputNode, FString Prefix, UNativizationV2Subsystem* Subsystem)
{
    return FString();
}

FString UTimelineTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName,
    TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations,
    UNativizationV2Subsystem* Subsystem)
{
    UK2Node_Timeline* Timeline = CastChecked<UK2Node_Timeline>(Node);
    const FString Variable = UBlueprintNativizationLibrary::GetUniquePropertyName(
        FindFProperty<FProperty>(Node->GetBlueprint()->GeneratedClass, Timeline->TimelineName));
    const TMap<FString, FString> Methods = {
        { TEXT("Play"), TEXT("Play") }, { TEXT("PlayFromStart"), TEXT("PlayFromStart") },
        { TEXT("Stop"), TEXT("Stop") }, { TEXT("Reverse"), TEXT("Reverse") },
        { TEXT("ReverseFromEnd"), TEXT("ReverseFromEnd") }, { TEXT("SetNewTime"), TEXT("SetNewTime") }
    };
    EntryPinName.ReplaceInline(TEXT(" "), TEXT(""));
    const FString* Method = Methods.Find(EntryPinName);
    if (!Method)
    {
        return FString();
    }
    FString Arguments;
    FString Content;
    if (*Method == TEXT("SetNewTime"))
    {
        const FGenerateResultStruct Time = Subsystem->GenerateInputParameterCodeForNode(Node, Timeline->GetNewTimePin(), 0, MacroStack);
        Content = GenerateNewPreparations(Preparations, Time.Preparations);
        Preparations.Append(Time.Preparations);
        Arguments = Time.Code;
    }
    return Content + Variable + TEXT("->") + *Method + TEXT("(") + Arguments + TEXT(");\n");
}

UCurveBase* UTimelineTranslatorObject::GetCurveByName(UK2Node_Timeline* Timeline, FString Name)
{
    TSet<UCurveBase*> Curves;
    Timeline->GetBlueprint()->FindTimelineTemplateByVariableName(Timeline->TimelineName)->GetAllCurves(Curves);
    for (UCurveBase* Curve : Curves)
    {
        if (Curve->GetName() == Name)
        {
            return Curve;
        }
    }
    return nullptr;
}

FString UTimelineTranslatorObject::GenerateConstruction(UK2Node* InputNode, FString Prefix, UNativizationV2Subsystem* Subsystem)
{
    UK2Node_Timeline* Timeline = CastChecked<UK2Node_Timeline>(InputNode);
    UTimelineTemplate* Definition = Timeline->GetBlueprint()->FindTimelineTemplateByVariableName(Timeline->TimelineName);
    auto PropertyName = [&](FName Name)
    {
        return UBlueprintNativizationLibrary::GetUniquePropertyName(FindFProperty<FProperty>(InputNode->GetBlueprint()->GeneratedClass, Name));
    };
    const FString Variable = PropertyName(Timeline->TimelineName);
    FString Content = FString::Format(TEXT("{0} = CreateDefaultSubobject<UTimelineComponent>(TEXT(\"{1}\"));\n"),
        { Variable, Timeline->TimelineName.ToString().ReplaceCharWithEscapedChar() });
    Content += Variable + TEXT("->SetPropertySetObject(this);\n");
    FRetainedBlueprintGeneration::AddInstanceInitialization(Variable + TEXT("->SetPropertySetObject(this);\n"));
    Content += Variable + TEXT("->SetDirectionPropertyName(TEXT(\"") + PropertyName(Definition->GetDirectionPropertyName()) + TEXT("\"));\n");
    Content += Variable + TEXT("->SetTimelineLength(") + FString::SanitizeFloat(Definition->TimelineLength) + TEXT("f);\n");
    Content += Variable + TEXT("->SetTimelineLengthMode(") + UBlueprintNativizationLibrary::GenerateEnumLiteral(StaticEnum<ETimelineLengthMode>(), Definition->LengthMode) + TEXT(");\n");
    Content += Variable + TEXT("->SetLooping(") + LexToString(bool(Definition->bLoop)) + TEXT(");\n");
    Content += Variable + TEXT("->SetIgnoreTimeDilation(") + LexToString(bool(Definition->bIgnoreTimeDilation)) + TEXT(");\n");
    Content += Variable + TEXT("->SetIsReplicated(") + LexToString(bool(Definition->bReplicated)) + TEXT(");\n");
    Content += Variable + TEXT("->PrimaryComponentTick.TickGroup = ") + UBlueprintNativizationLibrary::GenerateEnumLiteral(StaticEnum<ETickingGroup>(), Definition->TimelineTickGroup) + TEXT(";\n");
    auto SerializeCurve = [&](const FRichCurve& Curve, const FString& Destination)
    {
        FString Text;
        FRichCurve::StaticStruct()->ExportText(Text, &Curve, nullptr, nullptr, PPF_None, nullptr);
        return TEXT("FRichCurve::StaticStruct()->ImportText(TEXT(\"") + Text.ReplaceCharWithEscapedChar() + TEXT("\"), &") +
            Destination + TEXT(", nullptr, PPF_None, nullptr, TEXT(\"TimelineCurve\"));\n");
    };
    auto AddCurve = [&](UCurveBase* Curve, const FTTPropertyTrack& Track, const FString& Type, const FString& Kind)
    {
        if (!Curve)
        {
            return;
        }
        const FString CurveName = Subsystem->GetNodeStateName(Timeline, Track.GetTrackName().ToString() + TEXT("Curve"));
        if (Track.bIsExternalCurve)
        {
            Content += TEXT("auto* ") + CurveName + TEXT(" = CreateDefaultSubobject<") + Type + TEXT(">(TEXT(\"") + CurveName + TEXT("\"));\n");
            const FString Asset = FRetainedBlueprintGeneration::GetAssetValue(Curve, Curve->GetClass());
            FRetainedBlueprintGeneration::AddInstanceInitialization(FString::Format(
                TEXT("{0}->Set{1}Curve({2}, TEXT(\"{3}\"));\n"),
                {Variable, Kind, Asset, Track.GetTrackName().ToString().ReplaceCharWithEscapedChar()}));
        }
        else
        {
            Content += TEXT("auto* ") + CurveName + TEXT(" = CreateDefaultSubobject<") + Type + TEXT(">(TEXT(\"") + CurveName + TEXT("\"));\n");
            if (UCurveFloat* FloatCurve = Cast<UCurveFloat>(Curve))
            {
                Content += SerializeCurve(FloatCurve->FloatCurve, CurveName + TEXT("->FloatCurve"));
            }
            else if (UCurveVector* VectorCurve = Cast<UCurveVector>(Curve))
            {
                for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
                {
                    Content += SerializeCurve(VectorCurve->FloatCurves[AxisIndex], CurveName + TEXT("->FloatCurves[") + FString::FromInt(AxisIndex) + TEXT("]"));
                }
            }
            else if (UCurveLinearColor* ColorCurve = Cast<UCurveLinearColor>(Curve))
            {
                for (int32 ChannelIndex = 0; ChannelIndex < 4; ++ChannelIndex)
                {
                    Content += SerializeCurve(ColorCurve->FloatCurves[ChannelIndex], CurveName + TEXT("->FloatCurves[") + FString::FromInt(ChannelIndex) + TEXT("]"));
                }
            }
        }
        Content += Variable + TEXT("->AddInterp") + Kind + TEXT("(") + CurveName + TEXT(", FOnTimeline") + Kind + TEXT("(), TEXT(\"") +
            PropertyName(Track.GetPropertyName()) + TEXT("\"), TEXT(\"") + Track.GetTrackName().ToString().ReplaceCharWithEscapedChar() + TEXT("\"));\n");
    };
    for (const FTTFloatTrack& Track : Definition->FloatTracks)
    {
        AddCurve(Track.CurveFloat, Track, TEXT("UCurveFloat"), TEXT("Float"));
    }
    for (const FTTVectorTrack& Track : Definition->VectorTracks)
    {
        AddCurve(Track.CurveVector, Track, TEXT("UCurveVector"), TEXT("Vector"));
    }
    for (const FTTLinearColorTrack& Track : Definition->LinearColorTracks)
    {
        AddCurve(Track.CurveLinearColor, Track, TEXT("UCurveLinearColor"), TEXT("LinearColor"));
    }
    const bool bHasExternalEvents = Definition->EventTracks.ContainsByPredicate([](const FTTEventTrack& Track)
    {
        return Track.bIsExternalCurve;
    });
    FString EventInitialization;
    if (bHasExternalEvents)
    {
        FStructProperty* TimelineProperty = FindFProperty<FStructProperty>(UTimelineComponent::StaticClass(), TEXT("TheTimeline"));
        FArrayProperty* EventsProperty = FindFProperty<FArrayProperty>(FTimeline::StaticStruct(), TEXT("Events"));
        FStructProperty* EventProperty = nullptr;
        if (EventsProperty)
        {
            EventProperty = CastField<FStructProperty>(EventsProperty->Inner);
        }
        if (!TimelineProperty || TimelineProperty->Struct != FTimeline::StaticStruct() ||
            !EventProperty || EventProperty->Struct != FTimelineEventEntry::StaticStruct())
        {
            FNativeApiCatalog::AddError(FString::Format(
                TEXT("Cannot rebuild external timeline events because the reflected timeline event storage is unavailable: {0}, timeline {1}."),
                {Timeline->GetBlueprint()->GetPathName(), Timeline->TimelineName.ToString()}));
            return Content;
        }
        const FString TimelinePropertyName = Subsystem->GetNodeStateName(Timeline, TEXT("EventTimelineProperty"));
        const FString TimelineStateName = Subsystem->GetNodeStateName(Timeline, TEXT("EventTimelineState"));
        const FString EventsPropertyName = Subsystem->GetNodeStateName(Timeline, TEXT("TimelineEventsProperty"));
        const FString EventsName = Subsystem->GetNodeStateName(Timeline, TEXT("TimelineEvents"));
        EventInitialization = FString::Format(
            TEXT("FStructProperty* {0} = FindFProperty<FStructProperty>(UTimelineComponent::StaticClass(), TEXT(\"TheTimeline\"));\n")
            TEXT("FTimeline* {1} = {0}->ContainerPtrToValuePtr<FTimeline>({2});\n")
            TEXT("FArrayProperty* {3} = FindFProperty<FArrayProperty>(FTimeline::StaticStruct(), TEXT(\"Events\"));\n")
            TEXT("FScriptArrayHelper {4}({3}, {3}->ContainerPtrToValuePtr<void>({1}));\n")
            TEXT("{4}.EmptyValues();\n"),
            {TimelinePropertyName, TimelineStateName, Variable, EventsPropertyName, EventsName});
    }
    for (UEdGraphPin* Pin : Timeline->Pins)
    {
        if (Pin->Direction != EGPD_Output || Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
        {
            continue;
        }
        TArray<UK2Node*> MacroStack;
        UEdGraphPin* Target = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Pin, 0, false, MacroStack, Subsystem);
        if (!Target)
        {
            continue;
        }
        const FGenerateFunctionStruct* Entry = Subsystem->EntryNodes.FindByPredicate([&](const FGenerateFunctionStruct& Candidate)
        {
            return Candidate.Node == Target->GetOwningNode() && Candidate.bIsLatentContinuation && Candidate.EntryPinName == Target->GetName();
        });
        const FString Callback = Subsystem->GetNodeStateName(Timeline, Pin->GetName() + TEXT("Callback"));
        const FString CallbackInitialization = TEXT("FOnTimelineEvent ") + Callback + TEXT(";\n") + Callback + TEXT(".BindUFunction(this, TEXT(\"") + Entry->Name.ToString() + TEXT("\"));\n");
        if (Pin->PinName == TEXT("Update"))
        {
            Content += CallbackInitialization;
            Content += Variable + TEXT("->SetTimelinePostUpdateFunc(") + Callback + TEXT(");\n");
        }
        else if (Pin->PinName == TEXT("Finished"))
        {
            Content += CallbackInitialization;
            Content += Variable + TEXT("->SetTimelineFinishedFunc(") + Callback + TEXT(");\n");
        }
        else
        {
            FString TrackInitialization = CallbackInitialization;
            for (const FTTEventTrack& Track : Definition->EventTracks)
            {
                if (Track.GetTrackName() == Pin->PinName && Track.CurveKeys)
                {
                    if (Track.bIsExternalCurve)
                    {
                        const FString Asset = FRetainedBlueprintGeneration::GetAssetValue(Track.CurveKeys, UCurveFloat::StaticClass());
                        const FString CurveName = Subsystem->GetNodeStateName(Timeline, Track.GetTrackName().ToString() + TEXT("EventCurve"));
                        const FString KeyName = Subsystem->GetNodeStateName(Timeline, Track.GetTrackName().ToString() + TEXT("EventCurveKey"));
                        TrackInitialization += FString::Format(
                            TEXT("if (UCurveFloat* {0} = {1})\n{\n")
                            TEXT("for (const FRichCurveKey& {2} : {0}->FloatCurve.GetConstRefOfKeys())\n{\n")
                            TEXT("{3}->AddEvent({2}.Time, {4});\n}\n}\n"),
                            {CurveName, Asset, KeyName, Variable, Callback});
                    }
                    else
                    {
                        for (const FRichCurveKey& Key : Track.CurveKeys->FloatCurve.GetConstRefOfKeys())
                        {
                            TrackInitialization += Variable + TEXT("->AddEvent(") + FString::SanitizeFloat(Key.Time) + TEXT("f, ") + Callback + TEXT(");\n");
                        }
                    }
                }
            }
            if (bHasExternalEvents)
            {
                EventInitialization += TrackInitialization;
            }
            else
            {
                Content += TrackInitialization;
            }
        }
    }
    if (bHasExternalEvents)
    {
        FRetainedBlueprintGeneration::AddInstanceInitialization(EventInitialization);
    }
    if (Definition->bAutoPlay)
    {
        Content += Variable + TEXT("->Play();\n");
    }
    return Content;
}

bool UTimelineTranslatorObject::CanContinueCodeGeneration(UK2Node* InputNode, FString EntryPinName)
{
    return false;
}

TSet<FString> UTimelineTranslatorObject::GenerateHeaderIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem)
{
    return { TEXT("#include \"Components/TimelineComponent.h\"") };
}

TSet<FString> UTimelineTranslatorObject::GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem)
{
    return { TEXT("#include \"Curves/CurveFloat.h\""), TEXT("#include \"Curves/CurveVector.h\""), TEXT("#include \"Curves/CurveLinearColor.h\""), TEXT("#include \"UObject/UnrealType.h\"") };
}

FGenerateResultStruct UTimelineTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin,
    int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem)
{
    UK2Node_Timeline* Timeline = CastChecked<UK2Node_Timeline>(Node);
    UTimelineTemplate* Definition = Timeline->GetBlueprint()->FindTimelineTemplateByVariableName(Timeline->TimelineName);
    FName Name = Definition->GetDirectionPropertyName();
    for (const FTTFloatTrack& Track : Definition->FloatTracks)
    {
        if (Track.GetTrackName() == Pin->PinName)
        {
            Name = Track.GetPropertyName();
        }
    }
    for (const FTTVectorTrack& Track : Definition->VectorTracks)
    {
        if (Track.GetTrackName() == Pin->PinName)
        {
            Name = Track.GetPropertyName();
        }
    }
    for (const FTTLinearColorTrack& Track : Definition->LinearColorTracks)
    {
        if (Track.GetTrackName() == Pin->PinName)
        {
            Name = Track.GetPropertyName();
        }
    }
    return UBlueprintNativizationLibrary::GetUniquePropertyName(FindFProperty<FProperty>(Node->GetBlueprint()->GeneratedClass, Name));
}
