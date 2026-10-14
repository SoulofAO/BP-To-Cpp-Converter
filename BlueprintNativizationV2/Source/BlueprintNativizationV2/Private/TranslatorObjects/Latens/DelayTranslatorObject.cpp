/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/Latens/DelayTranslatorObject.h"
#include "NativeApiCatalog.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"

bool UDelayTranslatorObject::SupportsNativeOperation(const UFunction* Function)
{
    const FName FunctionName = Function->GetFName();
    if (Function->GetOwnerClass() == APlayerController::StaticClass())
    {
        return FunctionName == TEXT("PlayDynamicForceFeedback");
    }
    if (Function->GetOwnerClass() == UKismetSystemLibrary::StaticClass())
    {
        return FunctionName == TEXT("Delay") || FunctionName == TEXT("RetriggerableDelay") ||
            FunctionName == TEXT("DelayUntilNextTick") || FunctionName == TEXT("DelayUntilNextFrame") ||
            FunctionName == TEXT("MoveComponentTo") || FunctionName == TEXT("LoadAsset") ||
            FunctionName == TEXT("LoadAssetClass") || FunctionName == TEXT("LoadAssets");
    }
    if (Function->GetOwnerClass() == UGameplayStatics::StaticClass())
    {
        return FunctionName == TEXT("LoadStreamLevel") || FunctionName == TEXT("UnloadStreamLevel") ||
            FunctionName == TEXT("LoadStreamLevelBySoftObjectPtr") || FunctionName == TEXT("UnloadStreamLevelBySoftObjectPtr");
    }
    return false;
}

bool UDelayTranslatorObject::CanApply(UK2Node* Node)
{
    const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
    const UFunction* Function = nullptr;
    if (Call)
    {
        Function = Call->GetTargetFunction();
    }
    return Function && Function->HasMetaData(TEXT("Latent"));
}

void UDelayTranslatorObject::GenerateNewFunction(UK2Node* InputNode, TArray<UK2Node*> Path,
    TArray<FGenerateFunctionStruct>& EntryNodes, UNativizationV2Subsystem* Subsystem)
{
    TArray<UK2Node*> MacroStack;
    UEdGraphPin* Target = UBlueprintNativizationLibrary::GetFirstNonKnotPin(
        InputNode->FindPin(UEdGraphSchema_K2::PN_Then), 0, false, MacroStack, Subsystem);
    if (!Target)
    {
        return;
    }
    UK2Node* TargetNode = CastChecked<UK2Node>(Target->GetOwningNode());
    for (FGenerateFunctionStruct& Entry : EntryNodes)
    {
        if (Entry.Node == TargetNode && Entry.bIsLatentContinuation && Entry.EntryPinName == Target->GetName())
        {
            Entry.bIsLatentContinuation = true;
            Entry.EntryPinName = Target->GetName();
            return;
        }
    }
    FGenerateFunctionStruct Continuation;
    Continuation.Node = TargetNode;
    FString CompletionName = CastChecked<UK2Node_CallFunction>(InputNode)->GetTargetFunction()->GetName() + TEXT("Completed");
    if (Target->PinName != UEdGraphSchema_K2::PN_Execute)
    {
        FString EntryName = Target->GetName();
        EntryName[0] = FChar::ToUpper(EntryName[0]);
        CompletionName += EntryName;
    }
    Continuation.Name = *Subsystem->GetNodeStateName(TargetNode, CompletionName);
    Continuation.bIsLatentContinuation = true;
    Continuation.EntryPinName = Target->GetName();
    EntryNodes.Add(Continuation);
}

TSet<FString> UDelayTranslatorObject::GenerateLocalVariables(UK2Node* InputNode,
    TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem)
{
    return Super::GenerateLocalVariables(InputNode, MacroStack, Subsystem);
}


TSet<FString> UDelayTranslatorObject::GenerateHeaderIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem)
{
    return { TEXT("#include \"TimerManager.h\"") };
}

TSet<FString> UDelayTranslatorObject::GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem)
{
    return { TEXT("#include \"TimerManager.h\""), TEXT("#include \"Engine/World.h\""),
        TEXT("#include \"Engine/Engine.h\""), TEXT("#include \"Components/SceneComponent.h\""),
        TEXT("#include \"Engine/LevelStreaming.h\""), TEXT("#include \"Kismet/GameplayStatics.h\""),
        TEXT("#include \"Engine/AssetManager.h\""), TEXT("#include \"Engine/StreamableManager.h\""),
        TEXT("#include \"HAL/IConsoleManager.h\""), TEXT("#include \"Engine/CoreSettings.h\""), TEXT("#include \"Kismet/KismetSystemLibrary.h\""), TEXT("#include \"GameFramework/PlayerController.h\"") };
}

FString UDelayTranslatorObject::GenerateGlobalVariables(UK2Node* InputNode, FString Prefix, UNativizationV2Subsystem* Subsystem)
{
    const FString State = Subsystem->GetNodeStateName(InputNode, CastChecked<UK2Node_CallFunction>(InputNode)->GetTargetFunction()->GetName() + TEXT("Operation"));
    FString Content = FString::Format(TEXT("FTimerHandle {0}Schedule;\nFTimerDelegate {0}Update;\nbool {0}Pending = false;\nfloat {0}Remaining = 0.0f;\n"), { State });
    const UFunction* Function = CastChecked<UK2Node_CallFunction>(InputNode)->GetTargetFunction();
    if (Function->GetFName() == TEXT("LoadAsset") || Function->GetFName() == TEXT("LoadAssetClass") || Function->GetFName() == TEXT("LoadAssets"))
    {
        Content += TEXT("TArray<TFunction<bool()>> ") + State + TEXT("Requests;\n");
    }
    if (Function->GetFName() == TEXT("PlayDynamicForceFeedback"))
    {
        Content += FString::Format(TEXT("FDynamicForceFeedbackHandle {0}Feedback = 0;\nbool {0}Infinite = false;\n"), { State });
    }
    if (Function->GetFName() == TEXT("MoveComponentTo"))
    {
        Content += FString::Format(TEXT(
            "UPROPERTY(Transient)\nTObjectPtr<USceneComponent> {0}Component;\n"
            "FVector {0}InitialLocation;\nFVector {0}TargetLocation;\n"
            "FRotator {0}InitialRotation;\nFRotator {0}TargetRotation;\n"
            "float {0}Elapsed = 0.0f;\nfloat {0}Duration = 0.0f;\n"
            "bool {0}Interpolating = false;\nbool {0}EaseIn = false;\nbool {0}EaseOut = false;\nbool {0}ShortestRotation = false;\n"), { State });
    }
    return Content;
}

FString UDelayTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName,
    TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations,
    UNativizationV2Subsystem* Subsystem)
{
    UK2Node_CallFunction* Call = CastChecked<UK2Node_CallFunction>(Node);
    UFunction* Function = Call->GetTargetFunction();
    const FString FunctionName = Function->GetName();
    const FString State = Subsystem->GetNodeStateName(Node, FunctionName + TEXT("Operation"));
    FString Completed;
    UEdGraphPin* Target = UBlueprintNativizationLibrary::GetFirstNonKnotPin(
        Node->FindPin(UEdGraphSchema_K2::PN_Then), 0, false, MacroStack, Subsystem);
    if (Target)
    {
        const FGenerateFunctionStruct* Continuation = Subsystem->EntryNodes.FindByPredicate([&](const FGenerateFunctionStruct& Entry)
        {
            return Entry.Node == Target->GetOwningNode() && Entry.bIsLatentContinuation && Entry.EntryPinName == Target->GetName();
        });
        if (!Continuation)
        {
            FNativeApiCatalog::AddError(TEXT("A native asynchronous continuation was not registered for ") + Node->GetPathName());
            return FString();
        }
        Completed = Continuation->Name.ToString() + TEXT("();\n");
    }
    FGenerationPreparations RequiredPreparations;
    FString Inputs;
    auto ReadInput = [&](const TCHAR* PinName)
    {
        UEdGraphPin* InputPin = Node->FindPin(PinName);
        if (InputPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Delegate)
        {
            const FString Variable = State + TEXT("Input") + PinName;
            const FDelegateProperty* Property = FindFProperty<FDelegateProperty>(Function, PinName);
            FString Type = Property->GetCPPType(nullptr, EPropertyExportCPPFlags::CPPF_BlueprintCppBackend);
            if (!Type.Contains(TEXT("::")))
            {
                Type = UBlueprintNativizationLibrary::GetUniqueFieldName(Function->GetOwnerClass()) + TEXT("::") + Type;
            }
            Inputs += Type + TEXT(" ") + Variable + TEXT(";\n");
            if (!InputPin->LinkedTo.IsEmpty())
            {
                const FGenerateResultStruct Delegate = Subsystem->GenerateInputParameterCodeForNode(Node, InputPin, 0, MacroStack);
                RequiredPreparations.Append(Delegate.Preparations);
                Inputs += TEXT("FScriptDelegate ") + Variable + TEXT("Source = ") + Delegate.Code + TEXT(";\n") +
                    Variable + TEXT(".BindUFunction(") + Variable + TEXT("Source.GetUObject(), ") + Variable + TEXT("Source.GetFunctionName());\n");
            }
            return Variable;
        }
        const FGenerateResultStruct Input = Subsystem->GenerateInputParameterCodeForNode(Node, InputPin, 0, MacroStack);
        RequiredPreparations.Append(Input.Preparations);
        const FString Variable = State + TEXT("Input") + PinName;
        Inputs += TEXT("const auto ") + Variable + TEXT(" = ") + Input.Code + TEXT(";\n");
        return Variable;
    };
    FString Context = TEXT("this");
    if (UEdGraphPin* WorldContext = Node->FindPin(TEXT("WorldContextObject")); WorldContext && !WorldContext->LinkedTo.IsEmpty())
    {
        Context = ReadInput(TEXT("WorldContextObject"));
    }
    if (FunctionName == TEXT("MoveComponentTo"))
    {
        Context = ReadInput(TEXT("Component"));
    }
    if (FunctionName == TEXT("PlayDynamicForceFeedback"))
    {
        UEdGraphPin* Receiver = Node->FindPin(UEdGraphSchema_K2::PN_Self);
        if (Receiver && !Receiver->LinkedTo.IsEmpty())
        {
            Context = ReadInput(TEXT("self"));
        }
    }
    const FString World = State + TEXT("World");
    const FString Finish = State + TEXT("Pending = false;\n") + State + TEXT("Update.Unbind();\n") + Completed;
    const FString Schedule = State + TEXT("Schedule = ") + World + TEXT("->GetTimerManager().SetTimerForNextTick(") + State + TEXT("Update);\n");
    FString Setup;
    FString Update;
    FString Captures = TEXT("this, ") + World;
    FString Guard = TEXT("!") + State + TEXT("Pending");

    if (FunctionName == TEXT("PlayDynamicForceFeedback"))
    {
        const FString Intensity = ReadInput(TEXT("Intensity"));
        const FString Duration = ReadInput(TEXT("Duration"));
        const FString LeftLarge = ReadInput(TEXT("bAffectsLeftLarge"));
        const FString LeftSmall = ReadInput(TEXT("bAffectsLeftSmall"));
        const FString RightLarge = ReadInput(TEXT("bAffectsRightLarge"));
        const FString RightSmall = ReadInput(TEXT("bAffectsRightSmall"));
        FString Action = TEXT("Start");
        if (EntryPinName == TEXT("Stop") || EntryPinName == TEXT("Update"))
        {
            Action = EntryPinName;
        }
        const FString Invocation = State + TEXT("Feedback = ") + Context + TEXT("->PlayDynamicForceFeedback(") +
            FString::Join(TArray<FString>{ Intensity, Duration, LeftLarge, LeftSmall, RightLarge, RightSmall,
                TEXT("EDynamicForceFeedbackAction::") + Action, State + TEXT("Feedback") }, TEXT(", ")) + TEXT(");\n");
        if (Action != TEXT("Start"))
        {
            FString Content = GenerateNewPreparations(Preparations, RequiredPreparations);
            Preparations.Append(RequiredPreparations);
            Content += TEXT("{\n") + Inputs + TEXT("if (") + State + TEXT("Pending && IsValid(") + Context + TEXT("))\n{\n") + Invocation;
            if (Action == TEXT("Stop"))
            {
                Content += State + TEXT("Remaining = 0.0f;\n") + State + TEXT("Infinite = false;\n");
            }
            return Content + TEXT("}\n}\n");
        }
        Guard = TEXT("true");
        Setup = Invocation + State + TEXT("Remaining = ") + Duration + TEXT(";\n") + State + TEXT("Infinite = ") + Duration + TEXT(" < 0.0f;\n");
        Captures += TEXT(", FeedbackController = TWeakObjectPtr<APlayerController>(") + Context + TEXT("), FeedbackIdentifier = ") + State + TEXT("Feedback");
        Update = State + TEXT("Remaining -= ") + World + TEXT("->GetDeltaSeconds();\nif (!") + State + TEXT("Infinite && ") + State + TEXT("Remaining <= 0.0f)\n{\n") + Finish + TEXT("}\nelse\n{\n") + Schedule + TEXT("}\n");
    }
    else if (FunctionName == TEXT("Delay") || FunctionName == TEXT("RetriggerableDelay"))
    {
        const FString Duration = ReadInput(TEXT("Duration"));
        Setup = State + TEXT("Remaining = ") + Duration + TEXT(";\n");
        Update = State + TEXT("Remaining -= ") + World + TEXT("->GetDeltaSeconds();\nif (") + State + TEXT("Remaining <= 0.0f)\n{\n") + Finish + TEXT("}\nelse\n{\n") + Schedule + TEXT("}\n");
        if (FunctionName == TEXT("RetriggerableDelay"))
        {
            Guard = TEXT("true");
        }
    }
    else if (FunctionName == TEXT("DelayUntilNextTick") || FunctionName == TEXT("DelayUntilNextFrame"))
    {
        Captures += TEXT(", InitialFrame = GFrameCounter");
        FString WaitCondition = TEXT("GFrameCounter == InitialFrame");
        if (FunctionName == TEXT("DelayUntilNextTick"))
        {
            Update += TEXT("const IConsoleVariable* GuaranteeNextFrame = IConsoleManager::Get().FindConsoleVariable(TEXT(\"LatentActions.GuaranteeNextTickDelay\"));\n");
            WaitCondition = TEXT("GuaranteeNextFrame && GuaranteeNextFrame->GetBool() && GFrameCounter == InitialFrame");
        }
        Update += TEXT("if (") + WaitCondition + TEXT(")\n{\n") + Schedule + TEXT("}\nelse\n{\n") + Finish + TEXT("}\n");
    }
    else if (FunctionName == TEXT("MoveComponentTo"))
    {
        const FString Location = ReadInput(TEXT("TargetRelativeLocation"));
        const FString Rotation = ReadInput(TEXT("TargetRelativeRotation"));
        const FString Duration = ReadInput(TEXT("OverTime"));
        const FString EaseIn = ReadInput(TEXT("bEaseIn"));
        const FString EaseOut = ReadInput(TEXT("bEaseOut"));
        const FString Shortest = ReadInput(TEXT("bForceShortestRotationPath"));
        Guard = TEXT("true");
        if (EntryPinName == TEXT("Stop"))
        {
            Setup = State + TEXT("Interpolating = false;\n");
        }
        else if (EntryPinName == TEXT("Return"))
        {
            Setup = FString::Format(TEXT("{0}Duration = {0}Elapsed;\n{0}Elapsed = 0.0f;\n{0}TargetLocation = {0}InitialLocation;\n{0}TargetRotation = {0}InitialRotation;\n{0}InitialLocation = {1}->GetRelativeLocation();\n{0}InitialRotation = {1}->GetRelativeRotation();\n"), { State, Context });
        }
        else
        {
            Setup = FString::Format(TEXT("{0}Duration = {1};\n{0}Elapsed = 0.0f;\n{0}InitialLocation = {2}->GetRelativeLocation();\n{0}InitialRotation = {2}->GetRelativeRotation();\n{0}TargetLocation = {3};\n{0}TargetRotation = {4};\n"), { State, Duration, Context, Location, Rotation });
        }
        if (EntryPinName == TEXT("Stop") || EntryPinName == TEXT("Return"))
        {
            FString Content = GenerateNewPreparations(Preparations, RequiredPreparations);
            Preparations.Append(RequiredPreparations);
            return Content + TEXT("{\n") + Inputs + TEXT("if (") + State + TEXT("Pending && IsValid(") + Context + TEXT("))\n{\n") + Setup + TEXT("}\n}\n");
        }
        Setup += FString::Format(TEXT("if (!{0}Pending)\n{\n{0}Component = {1};\n{0}Interpolating = true;\n{0}EaseIn = {2};\n{0}EaseOut = {3};\n{0}ShortestRotation = {4};\n}\n"), { State, Context, EaseIn, EaseOut, Shortest });
        Update = FString::Format(TEXT(
            "{0}Elapsed += {1}->GetDeltaSeconds();\n"
            "const bool bComplete = {0}Elapsed >= {0}Duration;\n"
            "if (IsValid({0}Component) && {0}Interpolating)\n{\n"
            "float Blend = 1.0f;\nif (!bComplete)\n{\nBlend = {0}Elapsed / {0}Duration;\n"
            "if ({0}EaseIn && {0}EaseOut)\n{\nBlend = FMath::InterpEaseInOut(0.0f, 1.0f, Blend, 2.0f);\n}\n"
            "else if ({0}EaseIn)\n{\nBlend = FMath::Square(Blend);\n}\n"
            "else if ({0}EaseOut)\n{\nBlend = FMath::Sqrt(Blend);\n}\n}\n"
            "FVector Location = {0}TargetLocation;\nFRotator Rotation = {0}TargetRotation;\n"
            "if (!bComplete)\n{\nLocation = FMath::Lerp({0}InitialLocation, {0}TargetLocation, Blend);\n"
            "if ({0}ShortestRotation)\n{\nRotation = FQuat::Slerp({0}InitialRotation.Quaternion(), {0}TargetRotation.Quaternion(), Blend).GetNormalized().Rotator();\n}\n"
            "else\n{\nRotation = FMath::Lerp({0}InitialRotation, {0}TargetRotation, Blend);\n}\n}\n"
            "{0}Component->SetRelativeLocation(Location, false);\nif (IsValid({0}Component))\n{\n{0}Component->SetRelativeRotation(Rotation, false);\n}\n}\n"
            "if (bComplete || !{0}Interpolating)\n{\n{0}Component = nullptr;\n{2}}\nelse\n{\n{3}}\n"), { State, World, Finish, Schedule });
    }
    else if (FunctionName.Contains(TEXT("StreamLevel")))
    {
        FString LevelName;
        if (Node->FindPin(TEXT("LevelName")))
        {
            LevelName = ReadInput(TEXT("LevelName"));
        }
        else
        {
            LevelName = TEXT("FName(*") + ReadInput(TEXT("Level")) + TEXT(".GetLongPackageName())");
        }
        const bool bLoading = FunctionName.StartsWith(TEXT("Load"));
        FString Visible = TEXT("false");
        FString Block;
        if (bLoading)
        {
            Visible = ReadInput(TEXT("bMakeVisibleAfterLoad"));
            Block = ReadInput(TEXT("bShouldBlockOnLoad"));
        }
        else
        {
            Block = ReadInput(TEXT("bShouldBlockOnUnload"));
        }
        Setup = TEXT("ULevelStreaming* StreamingLevel = UGameplayStatics::GetStreamingLevel(") + World + TEXT(", ") + LevelName + TEXT(");\n");
        Setup += TEXT("if (StreamingLevel)\n{\n");
        if (bLoading)
        {
            Setup += TEXT("StreamingLevel->SetShouldBeLoaded(true);\nStreamingLevel->SetShouldBeVisible(") + Visible + TEXT(" || StreamingLevel->GetShouldBeVisibleFlag());\nStreamingLevel->bShouldBlockOnLoad = ") + Block + TEXT(";\n");
            Update = TEXT("const bool bComplete = !StreamingLevel || (StreamingLevel->IsLevelLoaded() && (StreamingLevel->IsLevelVisible() || !bVisible));\n");
        }
        else
        {
            Setup += TEXT("StreamingLevel->SetShouldBeLoaded(false);\nStreamingLevel->SetShouldBeVisible(false);\nStreamingLevel->bShouldBlockOnUnload = ") + Block + TEXT(";\n");
            Update = TEXT("const bool bComplete = !StreamingLevel || !StreamingLevel->IsLevelLoaded() || !GUseBackgroundLevelStreaming;\n");
        }
        Setup += TEXT("for (FConstPlayerControllerIterator ControllerIterator = ") + World + TEXT("->GetPlayerControllerIterator(); ControllerIterator; ++ControllerIterator)\n{\nif (APlayerController* Controller = ControllerIterator->Get())\n{\nController->LevelStreamingStatusChanged(StreamingLevel, StreamingLevel->ShouldBeLoaded(), StreamingLevel->ShouldBeVisible(), ") + Block + TEXT(", ") + Block + TEXT(", INDEX_NONE);\n}\n}\n}\n");
        Captures += TEXT(", StreamingLevel, bVisible = ") + Visible;
        Update += TEXT("if (bComplete)\n{\n") + Finish + TEXT("}\nelse\n{\n") + Schedule + TEXT("}\n");
    }
    else if (FunctionName == TEXT("LoadAsset") || FunctionName == TEXT("LoadAssetClass") || FunctionName == TEXT("LoadAssets"))
    {
        const TCHAR* AssetPinName = TEXT("Asset");
        if (FunctionName == TEXT("LoadAssetClass"))
        {
            AssetPinName = TEXT("AssetClass");
        }
        else if (FunctionName == TEXT("LoadAssets"))
        {
            AssetPinName = TEXT("Assets");
        }
        const FString Asset = ReadInput(AssetPinName);
        const FString Callback = ReadInput(TEXT("OnLoaded"));
        Setup = TEXT("TArray<FSoftObjectPath> AssetPaths;\n");
        if (FunctionName == TEXT("LoadAssets"))
        {
            Setup += TEXT("for (const auto& Asset : ") + Asset + TEXT(")\n{\nAssetPaths.Add(Asset.ToSoftObjectPath());\n}\n");
        }
        else
        {
            Setup += TEXT("AssetPaths.Add(") + Asset + TEXT(".ToSoftObjectPath());\n");
        }
        Setup += TEXT("TArray<FSoftObjectPath> RequestedPaths = AssetPaths;\nRequestedPaths.RemoveAll([](const FSoftObjectPath& Path)\n{\nreturn Path.IsNull();\n});\n")
            TEXT("TSharedPtr<FStreamableHandle> Loading;\nif (!RequestedPaths.IsEmpty())\n{\nLoading = UAssetManager::GetStreamableManager().RequestAsyncLoad(RequestedPaths);\n}\n");
        Captures += TEXT(", Loading, AssetPaths, LoadedCallback = ") + Callback;
        Update = TEXT("if (!Loading || Loading->HasLoadCompleted() || Loading->WasCanceled())\n{\n");
        if (FunctionName == TEXT("LoadAssets"))
        {
            Update += TEXT("TArray<UObject*> LoadedObjects;\nfor (const FSoftObjectPath& Path : AssetPaths)\n{\nLoadedObjects.Add(Path.ResolveObject());\n}\nLoadedCallback.ExecuteIfBound(LoadedObjects);\n");
        }
        else if (FunctionName == TEXT("LoadAssetClass"))
        {
            Update += TEXT("LoadedCallback.ExecuteIfBound(Cast<UClass>(AssetPaths[0].ResolveObject()));\n");
        }
        else
        {
            Update += TEXT("LoadedCallback.ExecuteIfBound(AssetPaths[0].ResolveObject());\n");
        }
        Update += Completed + TEXT("return true;\n}\nreturn false;\n");
        FString Content = GenerateNewPreparations(Preparations, RequiredPreparations);
        Preparations.Append(RequiredPreparations);
        Content += TEXT("{\n") + Inputs + TEXT("if (UWorld* ") + World + TEXT(" = GEngine->GetWorldFromContextObject(") + Context + TEXT(", EGetWorldErrorMode::ReturnNull))\n{\n") + Setup;
        Content += State + TEXT("Requests.Add([")+ Captures + TEXT("]() mutable -> bool\n{\n") + Update + TEXT("});\n");
        Content += TEXT("if (!") + State + TEXT("Pending)\n{\n") + State + TEXT("Pending = true;\n") +
            State + TEXT("Update = FTimerDelegate::CreateWeakLambda(this, [this, ") + World + TEXT("]()\n{\n") +
            TEXT("TArray<TFunction<bool()>> CurrentRequests = MoveTemp(") + State + TEXT("Requests);\n") +
            TEXT("for (TFunction<bool()>& Request : CurrentRequests)\n{\nif (!Request())\n{\n") + State + TEXT("Requests.Add(MoveTemp(Request));\n}\n}\n") +
            TEXT("if (") + State + TEXT("Requests.IsEmpty())\n{\n") + State + TEXT("Pending = false;\n") + State + TEXT("Update.Unbind();\n}\nelse\n{\n") +
            Schedule + TEXT("}\n});\n") + Schedule + TEXT("}\n}\n}\n");
        return Content;
    }
    FString Content = GenerateNewPreparations(Preparations, RequiredPreparations);
    Preparations.Append(RequiredPreparations);
    Content += TEXT("{\n") + Inputs + TEXT("if (UWorld* ") + World + TEXT(" = GEngine->GetWorldFromContextObject(") + Context + TEXT(", EGetWorldErrorMode::ReturnNull))\n{\nif (") + Guard + TEXT(")\n{\n") + Setup;
    if (FunctionName == TEXT("Delay") || FunctionName == TEXT("RetriggerableDelay") ||
        FunctionName == TEXT("DelayUntilNextTick") || FunctionName == TEXT("DelayUntilNextFrame"))
    {
        Captures = TEXT("this, WorldContext = TWeakObjectPtr<UObject>(") + Context + TEXT(")") + Captures.Mid((TEXT("this, ") + World).Len());
        Update = TEXT("UWorld* ") + World + TEXT(" = GEngine->GetWorldFromContextObject(WorldContext.Get(), EGetWorldErrorMode::ReturnNull);\nif (!") + World + TEXT(")\n{\n") +
            State + TEXT("Pending = false;\n") + State + TEXT("Update.Unbind();\nreturn;\n}\n") + Update;
    }
    FString DelegateFactory = TEXT("FTimerDelegate::CreateWeakLambda(this, [");
    if (FunctionName == TEXT("PlayDynamicForceFeedback"))
    {
        DelegateFactory = TEXT("FTimerDelegate::CreateLambda([");
        Captures += TEXT(", CallbackOwner = TWeakObjectPtr<UObject>(this)");
        Update = TEXT("if (!CallbackOwner.IsValid())\n{\nif (FeedbackController.IsValid())\n{\nFeedbackController->PlayDynamicForceFeedback(0.0f, 0.0f, false, false, false, false, EDynamicForceFeedbackAction::Stop, FeedbackIdentifier);\n}\nreturn;\n}\n") + Update;
    }
    Content += World + TEXT("->GetTimerManager().ClearTimer(") + State + TEXT("Schedule);\n") + State + TEXT("Pending = true;\n") + State + TEXT("Update = ") + DelegateFactory + Captures + TEXT("]() mutable\n{\n") + Update + TEXT("});\n") + Schedule + TEXT("}\n}\n}\n");
    return Content;
}
