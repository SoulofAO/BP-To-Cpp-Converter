// Fill out your copyright notice in the Description page of Project Settings.


#include "TranslatorObjects/Latens/AIMoveToTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"

void UAIMoveToTranslatorObject::GenerateNewFunction(UK2Node* InputNode, TArray<UK2Node*> Path, TArray<FGenerateFunctionStruct>& EntryNodes, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	UEdGraphPin* OnSuccessExecPin = InputNode->FindPin(TEXT("OnSuccess"));
	UEdGraphPin* OnFailExecPin = InputNode->FindPin(TEXT("OnFail"));
	UEdGraphPin* PathFollowResultPin = InputNode->FindPin(TEXT("MovementResult"));

	TMap<UEdGraphPin*, FName> ExecPins;
	ExecPins.Add(OnSuccessExecPin, "OnSuccess");
	ExecPins.Add(OnFailExecPin, "OnFail");
	
	for (TPair<UEdGraphPin*, FName> Pair : ExecPins)
	{
		TArray<UK2Node*> MacroStack;
		UEdGraphPin* TargetPin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Pair.Key, 0, true, MacroStack, NativizationV2Subsystem);
		if (TargetPin && TargetPin->GetOwningNode())
		{
			UK2Node* TargetNode = Cast<UK2Node>(TargetPin->GetOwningNode());

			FGenerateFunctionStruct ExistingStruct;
			if (!UBlueprintNativizationDataLibrary::FindGenerateFunctionStructByNode(EntryNodes, TargetNode, ExistingStruct))
			{
				FGenerateFunctionStruct NewStruct;
				NewStruct.Node = TargetNode;
				EntryNodes.Add(NewStruct);

				FGenerateFunctionStruct* FoundStruct = EntryNodes.FindByKey(NewStruct);
				if (FoundStruct)
				{
					FString PathFollowPinName = UBlueprintNativizationLibrary::GetUniquePinName(PathFollowResultPin, EntryNodes);
					FoundStruct->CustomParametersString = {"FAIRequestID RequestID", FString::Format(TEXT("const FPathFollowingResult & {0}"), { PathFollowPinName })};

					FoundStruct->Name = *UBlueprintNativizationLibrary::GetUniqueEntryNodeName(TargetNode, EntryNodes, Pair.Value.ToString());
				}
			}
		}
	}

}

TSet<FString> UAIMoveToTranslatorObject::GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	return TSet<FString>({
		TEXT("#include \"AIBlueprintHelperLibrary.h\""),
		TEXT("#include \"AIAsyncTaskBlueprintProxy.h\"")
		});
}

FString UAIMoveToTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, TSet<FString>& Preparations, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	FString ProxyVarName = *UBlueprintNativizationLibrary::GetLambdaUniqueVariableNameByNode("MoveToProxy", Node, NativizationV2Subsystem->EntryNodes);

	// Генерация входных параметров (Pawn, Destination, TargetActor, AcceptanceRadius, StopOnOverlap)
	FGenerateResultStruct PawnStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, Node->FindPin(TEXT("Pawn")), 0, MacroStack);
	FGenerateResultStruct DestinationStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, Node->FindPin(TEXT("Destination")), 0, MacroStack);
	FGenerateResultStruct TargetActorStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, Node->FindPin(TEXT("TargetActor")), 0, MacroStack);
	FGenerateResultStruct AcceptanceRadiusStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, Node->FindPin(TEXT("AcceptanceRadius")), 0, MacroStack);
	FGenerateResultStruct StopOnOverlapStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, Node->FindPin(TEXT("bStopOnOverlap")), 0, MacroStack);

	FString PawnCode = PawnStruct.Code.IsEmpty() ? FString("nullptr") : PawnStruct.Code;
	FString DestinationCode = DestinationStruct.Code.IsEmpty() ? FString("FVector::ZeroVector") : DestinationStruct.Code;
	FString TargetActorCode = TargetActorStruct.Code.IsEmpty() ? FString("nullptr") : TargetActorStruct.Code;
	FString AcceptanceRadiusCode = AcceptanceRadiusStruct.Code.IsEmpty() ? FString("0.0f") : AcceptanceRadiusStruct.Code;
	FString StopOnOverlapCode = StopOnOverlapStruct.Code.IsEmpty() ? FString("false") : StopOnOverlapStruct.Code;

	FString Content;
	TSet<FString> LocalPreparations = Preparations;

	Content += GenerateNewPreparations(Preparations, PawnStruct.Preparations);
	Content += GenerateNewPreparations(Preparations, DestinationStruct.Preparations);
	Content += GenerateNewPreparations(Preparations, TargetActorStruct.Preparations);
	Content += GenerateNewPreparations(Preparations, AcceptanceRadiusStruct.Preparations);
	Content += GenerateNewPreparations(Preparations, StopOnOverlapStruct.Preparations);

	LocalPreparations.Append(PawnStruct.Preparations);
	LocalPreparations.Append(DestinationStruct.Preparations);
	LocalPreparations.Append(TargetActorStruct.Preparations);
	LocalPreparations.Append(AcceptanceRadiusStruct.Preparations);
	LocalPreparations.Append(StopOnOverlapStruct.Preparations);

	Content += FString::Format(TEXT("UAIAsyncTaskBlueprintProxy* {0} = UAIBlueprintHelperLibrary::CreateMoveToProxyObject(GetWorld(), {1}, {2}, {3}, {4}, {5});\n"), {
		*ProxyVarName,
		*PawnCode,
		*DestinationCode,
		*TargetActorCode,
		*AcceptanceRadiusCode,
		*StopOnOverlapCode
		});

	UEdGraphPin* OnSuccessExecPin = Node->FindPin(TEXT("OnSuccess"));
	UEdGraphPin* OnFailExecPin = Node->FindPin(TEXT("OnFail"));

	TMap<UEdGraphPin*, FName> ExecPins;
	ExecPins.Add(OnSuccessExecPin, TEXT("OnSuccess"));
	ExecPins.Add(OnFailExecPin, "OnFail");

	bool HasAnyLink = false;

	for (TPair<UEdGraphPin*, FName> Pair : ExecPins)
	{
		UEdGraphPin* TargetPin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Pair.Key, 0, true, MacroStack, NativizationV2Subsystem);
		if (TargetPin)
		{
			HasAnyLink = true;
			break;
		}
	}
	if (HasAnyLink)
	{
		Content += "if(Proxy)\n{\n";
		for (TPair<UEdGraphPin*, FName> Pair : ExecPins)
		{
			UEdGraphPin* TargetPin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Pair.Key, 0, true, MacroStack, NativizationV2Subsystem);
			if (TargetPin && TargetPin->GetOwningNode())
			{
				UK2Node* TargetNode = Cast<UK2Node>(TargetPin->GetOwningNode());

				Content += FString::Format(TEXT("{0}->{1}.AddDynamic(this, &{2}::{3});\n"),
					{
						ProxyVarName,
						Pair.Value.ToString(),
						*UBlueprintNativizationLibrary::GetUniqueFieldName(Node->GetBlueprint()->GeneratedClass),
						*UBlueprintNativizationLibrary::GetUniqueEntryNodeName(TargetNode, NativizationV2Subsystem->EntryNodes, Pair.Value.ToString())
					});
			}
		}
		Content += "}\n";
	}
	
	UEdGraphPin* BaseExec = Node->FindPin(TEXT("then"));
	UEdGraphPin* BaseExecTargetPin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(BaseExec, 0, true, MacroStack, NativizationV2Subsystem);
	Content += NativizationV2Subsystem->GenerateCodeFromNode(Cast<UK2Node>(BaseExecTargetPin->GetOwningNode()), BaseExecTargetPin->GetName(), VisitedNodes, LocalPreparations, MacroStack);
	return Content;
}

TSet<FString> UAIMoveToTranslatorObject::GenerateCSPrivateDependencyModuleNames(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	return TSet<FString>({ "AIModule" });
}

bool UAIMoveToTranslatorObject::CanContinueCodeGeneration(UK2Node* InputNode, FString EntryPinName)
{
	return false;
}
