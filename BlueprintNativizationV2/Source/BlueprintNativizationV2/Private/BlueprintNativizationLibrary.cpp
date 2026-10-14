/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "BlueprintNativizationLibrary.h"
#include "EngineAssetPolicy.h"
#include "GameFramework/Actor.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Misc/SecureHash.h"
#include "Logging/TokenizedMessage.h"
#include "GenerationGraphContext.h"
#include "StructInitialization.h"
#include "EditorUtilityWidget.h"
#include "EditorUtilityWidgetBlueprint.h"
#include "RetainedBlueprintGeneration.h"
#include "NativeApiCatalog.h"
#include "NativeGenerationContext.h"
#include "UObject/StructOnScope.h"
#include "TranslatorObjects/StructTranslatorObject.h"
#include "InputCoreTypes.h"
#include "BlueprintNativizationV2.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/UnrealType.h"
#include "UObject/ObjectMacros.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_Event.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Tunnel.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableSet.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_VariableGet.h"
#include "K2Node_Tunnel.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node_Composite.h"
#include "K2Node_AssignmentStatement.h"
#include "K2Node_TemporaryVariable.h"
#include "SourceCodeNavigation.h"
#include "Kismet/KismetSystemLibrary.h"
#include "K2Node_MakeArray.h"
#include "K2Node_Knot.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Engine/UserDefinedEnum.h"
#include "NodeDependingOnEnumInterface.h"
#include "StructUtils/UserDefinedStruct.h"
#include "StructUtils/InstancedStruct.h"
#include "BlueprintNativizationUniqueNameSubsystem.h"
#include "BlueprintNativizationSettings.h"
#include "IDesktopPlatform.h"
#include "DesktopPlatformModule.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "BlueprintEditorLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node_EditablePinBase.h"
#include "BlueprintNativizationSubsystem.h"
#include "K2Node_EnhancedInputAction.h"
#include "InputAction.h"
#include "WorkflowOrientedApp/WorkflowTabManager.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "EditorUtilitySubsystem.h"
#include "K2Node_ComponentBoundEvent.h"

FString UBlueprintNativizationLibrary::GetFunctionNameByEntryNode(UEdGraphNode* EntryNode)
{
	if (UK2Node_FunctionEntry* FunctionEntry = Cast<UK2Node_FunctionEntry>(EntryNode))
	{
		if (!FunctionEntry->FunctionReference.GetMemberName().IsNone())
		{
			return FunctionEntry->FunctionReference.GetMemberName().ToString();
		}
		return FunctionEntry->GetGraph()->GetName();
	}
	if (UK2Node_Event* CustomEvent = Cast<UK2Node_Event>(EntryNode))
	{
		return  CustomEvent->GetFunctionName().ToString();
	}
	if (UK2Node_Tunnel* K2Node_Tunnel = Cast<UK2Node_Tunnel>(EntryNode))
	{
		return K2Node_Tunnel->GetGraph()->GetName();
	}
	return EntryNode->GetName();
}

FString UBlueprintNativizationLibrary::GetFunctionNameByInstanceNode(UK2Node* Node)
{
	if (UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(Node))
	{
        if (UFunction* Function = CallFunctionNode->GetTargetFunction())
        {
            return Function->GetName();
        }
        return CallFunctionNode->FunctionReference.GetMemberName().ToString();
	}
	if (UK2Node_MacroInstance* MacroInstanceNode = Cast<UK2Node_MacroInstance>(Node))
	{
		if (UEdGraph* MacroGraph = MacroInstanceNode->GetMacroGraph())
		{
			return MacroGraph->GetName();
		}
		return FString();
	}
	return Node->GetName();
}

UK2Node* UBlueprintNativizationLibrary::FindEntryNodeByFunctionName(UBlueprint* Blueprint, const FName& TargetNodeName)
{
	if (!Blueprint) return nullptr;

	TArray<UEdGraph*> Graphs;
	FGenerationGraphContext::GetGraphs(Blueprint, Graphs);

	for (UEdGraph* Graph : Graphs)
	{
		if (!Graph) continue;

		TArray<UK2Node*> EntryNodes = GetEntryNodesPerGraph(Graph, true);

		for (UK2Node* EntryNode : EntryNodes)
		{
			if (!EntryNode) continue;
			if (UK2Node_Event* Event = Cast<UK2Node_Event>(EntryNode))
			{
				if (Event->GetFunctionName() == TargetNodeName)
				{
					return EntryNode;
				}
			}

			FName EntryName = GetEntryFunctionNameByNode(EntryNode);
			if (EntryName == TargetNodeName)
			{
				return EntryNode;
			}
		}
	}

	return nullptr;
}


TArray<FName> UBlueprintNativizationLibrary::GetAllBlueprintCallableNames(UBlueprint* Blueprint, bool bRemoveAllMacro, bool bUseDisplayName)
{
	TArray<FName> Result;

	if (!Blueprint) return Result;

	TArray<UEdGraph*> Graphs;

	Graphs.Append(Blueprint->UbergraphPages);
	Graphs.Append(Blueprint->FunctionGraphs);

	if (!bRemoveAllMacro)
	{
		Graphs.Append(Blueprint->MacroGraphs);
	}

	for (UEdGraph* Graph : Graphs)
	{
		if (!Graph) continue;

		TArray<UK2Node*> EntryNodes = GetEntryNodesPerGraph(Graph, !bRemoveAllMacro);

		if (EntryNodes.Num() > 0 && EntryNodes[0])
		{
			for (UK2Node* EntryNode : EntryNodes)
			{
				Result.Add(GetEntryFunctionNameByNode(EntryNode));
			}
		}
		else
		{
			Result.Add(Graph->GetFName());
		}
	}

	return Result;
}

UFunction* UBlueprintNativizationLibrary::FindFunctionByFunctionName(UBlueprint* Blueprint, const FName& TargetNodeName)
{
    if (!Blueprint)
    {
        return nullptr;
    }
    if (Blueprint->SkeletonGeneratedClass)
    {
        if (UFunction* Function = Blueprint->SkeletonGeneratedClass->FindFunctionByName(TargetNodeName))
        {
            return Function;
        }
    }
    if (Blueprint->GeneratedClass)
    {
        return Blueprint->GeneratedClass->FindFunctionByName(TargetNodeName);
    }
    return nullptr;
}

UFunction* UBlueprintNativizationLibrary::GetFunctionDeclaration(UFunction* Function)
{
    if (Function && !Function->GetOwnerClass()->HasAnyClassFlags(CLASS_Native))
    {
        if (UFunction* Declaration = FindFunctionByFunctionName(GetBlueprintFromFunction(Function), Function->GetFName()))
        {
            return Declaration;
        }
    }
    return Function;
}

FProperty* UBlueprintNativizationLibrary::GetPropertyFromEntryNode(UK2Node* EntryNode, const FName& PropertyName)
{
	if (!EntryNode)
	{
		return nullptr;
	}
	
	if (const UFunction* Function = FindFunctionByFunctionName(EntryNode->GetBlueprint(), *GetFunctionNameByEntryNode(EntryNode)))
	{
		return Function->FindPropertyByName(PropertyName);
	}

	return nullptr;
}


TArray<UK2Node*> UBlueprintNativizationLibrary::GetAllExecuteNodesInFunction(UK2Node* EntryNode, TArray<FGenerateFunctionStruct> EntryNodes, FString EntryPinName)
{
	TArray<UK2Node*> Result;
	if (!EntryNode)
	{
		return Result;
	}

	TArray<UK2Node*> PendingNodes;

	TSet<UK2Node*> EntryNodeSet;
	FGenerateFunctionStruct FoundStruct;

	UBlueprintNativizationDataLibrary::FindGenerateFunctionStructByNode(EntryNodes, EntryNode, FoundStruct);
	if (EntryNodes.Contains(FoundStruct))
	{
		EntryNodes.Remove(FoundStruct);
	}

	for (const FGenerateFunctionStruct& Entry : EntryNodes)
	{
		if (Entry.Node)
		{
			EntryNodeSet.Add(Entry.Node);
		}
	}
	EntryNodeSet.Remove(EntryNode);
	PendingNodes.AddUnique(EntryNode);

	while (!PendingNodes.IsEmpty())
	{
		UK2Node* Current = PendingNodes.Pop();
		if (Result.Contains(Current))
		{
			continue;
		}

		if (EntryNodeSet.Contains(Current))
		{
			continue;
		}

		Result.Add(Current);


		TArray<UEdGraphPin*> Pins;
		Pins = GetFilteredPins(Current, EPinOutputOrInputFilter::Ouput, EPinExcludeFilter::None, EPinIncludeOnlyFilter::ExecPin);

		if (EntryPinName.IsEmpty() || EntryPinName == "None")
		{
			for (UEdGraphPin* OutPin : Pins)
			{
				TArray<UK2Node*> LocalMacroStack = {};
				if (UEdGraphPin* LinkedExecPin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(OutPin, 0, true, LocalMacroStack, nullptr))
				{
					if (LinkedExecPin->GetOwningNode())
					{
						PendingNodes.AddUnique(Cast<UK2Node>(LinkedExecPin->GetOwningNode()));
					}
					
				}
			}
		}
		else
		{
			UEdGraphPin* SpecificPin = GetPinByName(Pins, EntryPinName);
			if (SpecificPin)
			{
				TArray<UK2Node*> LocalMacroStack = {};
				if (UEdGraphPin* LinkedExecPin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(SpecificPin, 0, true, LocalMacroStack, nullptr))
				{
					if (LinkedExecPin->GetOwningNode())
					{
						PendingNodes.AddUnique(Cast<UK2Node>(LinkedExecPin->GetOwningNode()));
					}
				}
			}
		}
	}

	return Result;
}

TArray<UK2Node*> UBlueprintNativizationLibrary::GetAllExecuteNodesInFunction(UK2Node* EntryNode, FString EntryPinName)
{
	TArray<UK2Node*> Result;
	if (!EntryNode)
	{
		return Result;
	}

	TArray<UK2Node*> PendingNodes;

	PendingNodes.AddUnique(EntryNode);

	while (!PendingNodes.IsEmpty())
	{
		UK2Node* Current = PendingNodes.Pop();
		if (Result.Contains(Current))
		{
			continue;
		}

		Result.Add(Current);

		TArray<UEdGraphPin*> Pins;
		Pins = GetFilteredPins(Current, EPinOutputOrInputFilter::Ouput, EPinExcludeFilter::None, EPinIncludeOnlyFilter::ExecPin);

		if (EntryPinName.IsEmpty() || EntryPinName == "None")
		{
			for (UEdGraphPin* OutPin : Pins)
			{
				TArray<UK2Node*> LocalMacroStack = {};
				if (UEdGraphPin* LinkedExecPin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(OutPin, 0, true, LocalMacroStack, nullptr))
				{
					if (LinkedExecPin->GetOwningNode())
					{
						PendingNodes.AddUnique(Cast<UK2Node>(LinkedExecPin->GetOwningNode()));
					}

				}
			}
		}
		else
		{
			UEdGraphPin* SpecificPin = GetPinByName(Pins, EntryPinName);
			if (SpecificPin)
			{
				TArray<UK2Node*> LocalMacroStack = {};
				if (UEdGraphPin* LinkedExecPin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(SpecificPin, 0, true, LocalMacroStack, nullptr))
				{
					if (LinkedExecPin->GetOwningNode())
					{
						PendingNodes.AddUnique(Cast<UK2Node>(LinkedExecPin->GetOwningNode()));
					}
				}
			}
		}
	}

	return Result;
}

TArray<UK2Node*> UBlueprintNativizationLibrary::GetAllExecuteNodesInFunctionWithMacroSupported(UK2Node* EntryNode, UNativizationV2Subsystem* NativizationV2Subsystem, FString EntryPinName, TArray<UK2Node*> MacroStack)
{
	TArray<UK2Node*> Result;
	TArray<UK2Node*> PendingNodes;

	TSet<UK2Node*> EntryNodeSet;
	FGenerateFunctionStruct FoundStruct;

	TArray<FGenerateFunctionStruct> LocalEntryNodes = NativizationV2Subsystem->EntryNodes;
	UBlueprintNativizationDataLibrary::FindGenerateFunctionStructByNode(LocalEntryNodes, EntryNode, FoundStruct);
	if (LocalEntryNodes.Contains(FoundStruct))
	{
		LocalEntryNodes.Remove(FoundStruct);
	}

	for (const FGenerateFunctionStruct& Entry : LocalEntryNodes)
	{
		if (Entry.Node)
		{
			EntryNodeSet.Add(Entry.Node);
		}
	}
	if (!EntryNode)
	{
		return Result;
	}
	EntryNodeSet.Remove(EntryNode);
	PendingNodes.AddUnique(EntryNode);

	while (!PendingNodes.IsEmpty())
	{
		UK2Node* Current = PendingNodes.Pop();
		if (Result.Contains(Current))
		{
			continue;
		}

		if (EntryNodeSet.Contains(Current))
		{
			continue;
		}

		Result.Add(Current);


		TArray<UEdGraphPin*> Pins;
		Pins = GetFilteredPins(Current, EPinOutputOrInputFilter::Ouput, EPinExcludeFilter::None, EPinIncludeOnlyFilter::ExecPin);

		if (EntryPinName.IsEmpty() || EntryPinName == "None")
		{
			for (UEdGraphPin* OutPin : Pins)
			{
				TArray<UK2Node*> LocalMacroStack = FGenerationGraphContext::GetMacroStack(Current);
				if (UEdGraphPin* LinkedExecPin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(OutPin, 0, false, LocalMacroStack, NativizationV2Subsystem))
				{
					if (LinkedExecPin->GetOwningNode())
					{
						PendingNodes.AddUnique(Cast<UK2Node>(LinkedExecPin->GetOwningNode()));
					}

				}
			}
		}
		else
		{
			UEdGraphPin* SpecificPin = GetPinByName(Pins, EntryPinName);
			if (SpecificPin)
			{
				TArray<UK2Node*> LocalMacroStack = FGenerationGraphContext::GetMacroStack(Current);
				if (UEdGraphPin* LinkedExecPin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(SpecificPin, 0, false, LocalMacroStack, NativizationV2Subsystem))
				{
					if (LinkedExecPin->GetOwningNode())
					{
						PendingNodes.AddUnique(Cast<UK2Node>(LinkedExecPin->GetOwningNode()));
					}
				}
			}
		}
	}

	return Result;
}

bool UBlueprintNativizationLibrary::CheckLoopInFunction(UK2Node* EntryNode, TArray<FGenerateFunctionStruct> EntryNodes, FString EntryPinName)
{
	if (!EntryNode)
	{
		return false;
	}

	TSet<UK2Node*> EntryNodeSet;
	for (const FGenerateFunctionStruct& Entry : EntryNodes)
	{
		if (Entry.Node)
		{
			EntryNodeSet.Add(Entry.Node);
		}
	}

	TSet<UK2Node*> Visited;
	TSet<UK2Node*> RecursionStack;

	TFunction<bool(UK2Node*)> DFS = [&](UK2Node* Current) -> bool
		{
			if (EntryNodeSet.Contains(Current))
			{
				return false; // ��� ��������� ����, ����� ��� ������ ������ ������
			}

			if (RecursionStack.Contains(Current))
			{
				return true; // ������� �����
			}

			if (Visited.Contains(Current))
			{
				return false; // ��� ����������, � ������ �� ����
			}

			Visited.Add(Current);
			RecursionStack.Add(Current);

			TArray<UEdGraphPin*> Pins = GetFilteredPins(Current, EPinOutputOrInputFilter::Ouput, EPinExcludeFilter::None, EPinIncludeOnlyFilter::ExecPin);
			if (EntryPinName != "None")
			{
				UEdGraphPin* SpecificPin = GetPinByName(Pins, EntryPinName);
				if (SpecificPin)
				{
					Pins = { SpecificPin };
				}
				else
				{
					Pins.Empty(); // ��������� ��� �� ������ � ���������
				}
			}

			for (UEdGraphPin* Pin : Pins)
			{
				for (UEdGraphPin* Linked : Pin->LinkedTo)
				{
					if (Linked && Linked->GetOwningNode())
					{
						UK2Node* NextNode = Cast<UK2Node>(Linked->GetOwningNode());
						if (NextNode && DFS(NextNode))
						{
							return true; // ����� ������� � ����� �� ��������
						}
					}
				}
			}

			RecursionStack.Remove(Current);
			return false;
		};

	return DFS(EntryNode);
}


TArray<UK2Node*> UBlueprintNativizationLibrary::GetAllInputNodes(TArray<UK2Node*> AllNodes, bool OnlyPureNode)
{
    TArray<UK2Node*> Result;
    TSet<UK2Node*> Visited;
    auto* Subsystem = GEngine->GetEngineSubsystem<UNativizationV2Subsystem>();
    for (int32 NodeIndex = 0; NodeIndex < AllNodes.Num(); ++NodeIndex)
    {
        UK2Node* Node = AllNodes[NodeIndex];
        if (!Node || Visited.Contains(Node))
        {
            continue;
        }
        Visited.Add(Node);
        for (UEdGraphPin* Pin : GetFilteredPins(Node, EPinOutputOrInputFilter::Input,
            EPinExcludeFilter::ExecPin, EPinIncludeOnlyFilter::None))
        {
            TArray<UK2Node*> MacroStack = FGenerationGraphContext::GetMacroStack(Node);
            UEdGraphPin* Source = GetFirstNonKnotPin(Pin, 0, false, MacroStack, Subsystem);
            UK2Node* InputNode = nullptr;
            if (Source && Source->Direction == EGPD_Output)
            {
                InputNode = Cast<UK2Node>(Source->GetOwningNode());
            }
            if (InputNode && (!OnlyPureNode || InputNode->IsNodePure()))
            {
                Result.AddUnique(InputNode);
                AllNodes.AddUnique(InputNode);
            }
        }
    }
    return Result;
}

UEdGraphPin* UBlueprintNativizationLibrary::FindClosestPinByName(const TArray<UEdGraphPin*>& Pins,const FString& OriginalName, int32 MinScore /* = -1 */)
{
	UEdGraphPin* BestPin = nullptr;
	int32 BestScore = TNumericLimits<int32>::Max();

	if (OriginalName.IsEmpty())
	{
		return nullptr;
	}

	for (UEdGraphPin* LocalPin : Pins)
	{
		if (!LocalPin)
		{
			continue;
		}

		const FString PinName = LocalPin->PinName.ToString();
		int32 Distance = LevenshteinDistance_FString(PinName, OriginalName);
		const int32 ContainBonus = PinName.Contains(OriginalName) ? (OriginalName.Len() * 10) : 0;
		int32 Score = Distance - ContainBonus;

		if (Score < BestScore)
		{
			BestScore = Score;
			BestPin = LocalPin;
		}
	}

	if (MinScore!= -1 && BestPin && BestScore > MinScore)
	{
		return nullptr;
	}

	return BestPin;
}


void UBlueprintNativizationLibrary::CloseTabsWithSpecificAssets(TArray<UObject*>& Assets)
{
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;

	if (!AssetEditorSubsystem)
	{
		return;
	}

	for (UObject* Asset : Assets)
	{
		if (!Asset)
		{
			continue;
		}

		IAssetEditorInstance* AssetEditor = AssetEditorSubsystem->FindEditorForAsset(Asset, false);

		if (!AssetEditor)
		{
			continue;
		}

		AssetEditorSubsystem->CloseAllAssetEditors();
	}
}


UBlueprint* UBlueprintNativizationLibrary::GetBlueprintFromFunction(UFunction* Function)
{
	if (!Function)
	{
		return nullptr;
	}

	UClass* OwnerClass = Function->GetOwnerClass();
	if (!OwnerClass)
	{
		return nullptr;
	}

#if WITH_EDITOR
	if (UBlueprint* BP = UBlueprint::GetBlueprintFromClass(OwnerClass))
	{
		return BP;
	}
#endif

	if (UBlueprintGeneratedClass* BPGenClass = Cast<UBlueprintGeneratedClass>(OwnerClass))
	{
		if (UBlueprint* BP = Cast<UBlueprint>(BPGenClass->ClassGeneratedBy))
		{
			return BP;
		}
	}

	if (UBlueprint* BP = Cast<UBlueprint>(OwnerClass->GetOuter()))
	{
		return BP;
	}

	return nullptr;
}


UEdGraph* UBlueprintNativizationLibrary::GetEdGraphByFunction(UFunction* Function)
{
#if WITH_EDITOR
	if (!Function) return nullptr;

	if (UClass* Class = Function->GetOuterUClass())
	{
		if (UBlueprint* BP = Cast<UBlueprint>(Class->ClassGeneratedBy))
		{
			for (UEdGraph* Graph : BP->FunctionGraphs)
			{
				if (Graph && Graph->GetFName() == Function->GetFName())
				{
					return Graph;
				}
			}

			for (UEdGraph* Graph : BP->UbergraphPages)
			{
				TArray<UK2Node_Event*> CustomEvents;

				Graph->GetNodesOfClass<UK2Node_Event>(CustomEvents);

				for (UK2Node_Event* CustomEvent : CustomEvents)
				{
					if (CustomEvent->GetFunctionName() == Function->GetName())
					{
						return Graph;
					}
				}
			}
		}
	}


#endif
	return nullptr;
}


bool UBlueprintNativizationLibrary::ShouldGenerateEvent(const UK2Node_Event* Event)
{
    if (Event->GetClass() != UK2Node_Event::StaticClass())
    {
        return true;
    }
    for (const UEdGraphPin* Pin : Event->Pins)
    {
        if (Pin->Direction == EGPD_Output && !Pin->LinkedTo.IsEmpty() &&
            (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Delegate))
        {
            return true;
        }
    }
    return false;
}

TArray<UK2Node*> UBlueprintNativizationLibrary::GetEntryNodesPerGraph(UEdGraph* EdGraph, bool IncludeMacro)
{
	TArray<UK2Node*> EntryNodes;

	if (!EdGraph)
		return EntryNodes;

	for (UEdGraphNode* Node : EdGraph->Nodes)
	{
		if (UK2Node_FunctionEntry* FuncEntry = Cast<UK2Node_FunctionEntry>(Node))
		{
			EntryNodes.Add(FuncEntry);

		}
        else if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
        {
            if (ShouldGenerateEvent(EventNode))
            {
                EntryNodes.Add(EventNode);
            }
        }
		else if (UK2Node_Tunnel* TunnelNode = Cast<UK2Node_Tunnel>(Node))
		{
			if (IncludeMacro)
			{
				if (!TunnelNode->bCanHaveInputs)
				{
					EntryNodes.Add(TunnelNode);
				}
			}
		}
		else if (UK2Node_EnhancedInputAction* EnhancedInputAction = Cast<UK2Node_EnhancedInputAction>(Node))
		{
			EntryNodes.Add(EnhancedInputAction);
		}
	}

	return EntryNodes;
}

UK2Node* UBlueprintNativizationLibrary::GetEntryNodesByFunction(UFunction* Function)
{
	if (!Function)
	{
		return nullptr;
	}

	UClass* OwnerClass = Function->GetOwnerClass();
	if (!OwnerClass)
	{
		return nullptr;
	}

	UBlueprintGeneratedClass* BPGC = Cast<UBlueprintGeneratedClass>(OwnerClass);
	if (!BPGC || !BPGC->ClassGeneratedBy)
	{
		return nullptr;
	}

	UBlueprint* Blueprint = Cast<UBlueprint>(BPGC->ClassGeneratedBy);
	if (!Blueprint)
	{
		return nullptr;
	}

	const FName FunctionName = Function->GetFName();

	TArray<UEdGraph*> AllGraphs;
	FGenerationGraphContext::GetGraphs(Blueprint, AllGraphs);

	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph)
			continue;

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node)
				continue;

			UK2Node* K2Node = Cast<UK2Node>(Node);
			if (!K2Node)
				continue;

			FName NodeFunctionName = *GetFunctionNameByEntryNode(K2Node);

			if (NodeFunctionName == FunctionName)
			{
				return K2Node;
			}
		}
	}

	return nullptr;
}


FString UBlueprintNativizationLibrary::GetUniqueEntryNodeName(UK2Node* EntryNode, TArray<FGenerateFunctionStruct> EntryNodes, FString PredictName)
{
	if (!EntryNode)
	{
		return TEXT("InvalidNode");
	}
	UBlueprintNativizationUniqueNameSubsystem* Subsystem = GEngine->GetEngineSubsystem<UBlueprintNativizationUniqueNameSubsystem>();
	if (Subsystem)
	{
		return Subsystem->GetUniqueFunctionName(EntryNode, EntryNodes, PredictName).ToString();
	}
	return FString();
}

FString UBlueprintNativizationLibrary::GetUniqueFunctionName(UFunction* Function, FString PredictName)
{
	if (!Function)
	{
		return TEXT("InvalidFunction");
	}
	UBlueprintNativizationUniqueNameSubsystem* Subsystem = GEngine->GetEngineSubsystem<UBlueprintNativizationUniqueNameSubsystem>();
	if (Subsystem)
	{
		return Subsystem->GetUniqueFunctionName(Function, PredictName).ToString();
	}
	return FString();
}

FString UBlueprintNativizationLibrary::GetUniquePropertyName(FProperty* Property)
{
	UBlueprintNativizationUniqueNameSubsystem* Subsystem = GEngine->GetEngineSubsystem<UBlueprintNativizationUniqueNameSubsystem>();
	if (Subsystem)
	{
		return Subsystem->GetUniqueVariableName(Property).ToString();
	}
	return FString();
}

FString UBlueprintNativizationLibrary::GetUniquePinName(UEdGraphPin* Pin, TArray<FGenerateFunctionStruct> EntryNodes)
{
	if (!Pin || !Pin->GetOwningNode())
	{
		return TEXT("InvalidPin");
	}

	UBlueprintNativizationUniqueNameSubsystem* Subsystem = GEngine->GetEngineSubsystem<UBlueprintNativizationUniqueNameSubsystem>();
	if (Subsystem)
	{
		return Subsystem->GetUniquePinVariableName(Pin, EntryNodes).ToString();
	}
	return FString();
}

void UBlueprintNativizationLibrary::ResetNameSubsistem()
{
	UBlueprintNativizationUniqueNameSubsystem* Subsystem = GEngine->GetEngineSubsystem<UBlueprintNativizationUniqueNameSubsystem>();
	if (Subsystem)
	{
		Subsystem->Reset();
	}
}

bool UBlueprintNativizationLibrary::IsModuleLoaded(const FName& ModuleName)
{
	return FModuleManager::Get().IsModuleLoaded(ModuleName);
}

UFunction* UBlueprintNativizationLibrary::GetRootFunctionDeclaration(const UFunction* Function)
{
	if (!Function)
		return nullptr;

	const FName FunctionName = Function->GetFName();
	const UClass* SuperClass = Function->GetOwnerClass();

	UFunction* RootFunction = const_cast<UFunction*>(Function); // на случай, если это уже корень

	while (SuperClass)
	{
		UFunction* CandidateFunction = SuperClass->FindFunctionByName(FunctionName, EIncludeSuperFlag::ExcludeSuper);
		if (CandidateFunction && Function->IsSignatureCompatibleWith(CandidateFunction))
		{
			RootFunction = CandidateFunction;
		}

		SuperClass = SuperClass->GetSuperClass();
	}

	return RootFunction;
}


TArray<FName> UBlueprintNativizationLibrary::GetAllPropertyNamesByStruct(UScriptStruct* Struct)
{
	TArray<FName> PropertyNames;

	if (!Struct)
	{
		return PropertyNames;
	}

    FConstructorDescriptor Constructor;
    if (UBlueprintNativizationSettingsLibrary::FindConstructorDescriptorByStruct(Struct, Constructor))
    {
        for (const auto& Member : Constructor.ConstructorProperties)
        {
            PropertyNames.Add(FName(*Member.OriginalPropertyName));
        }
        return PropertyNames;
    }
	for (TFieldIterator<FProperty> It(Struct); It; ++It)
	{
		FProperty* Property = *It;
		if (Property)
		{
			PropertyNames.Add(Property->GetFName());
		}
	}

	return PropertyNames;
}

FName UBlueprintNativizationLibrary::GetEntryFunctionNameByNode(UK2Node* Node)
{
	if (!Node)
	{
		return NAME_None;
	}

	UBlueprint* Blueprint = Node->GetBlueprint();
	if (!Blueprint)
	{
		return NAME_None;
	}

	UClass* TargetClass = Blueprint->GeneratedClass
		? Blueprint->GeneratedClass
		: Blueprint->SkeletonGeneratedClass;

	if (UK2Node_ComponentBoundEvent* ComponentBoundEvent = Cast<UK2Node_ComponentBoundEvent>(Node))
	{
		FName PropertyName = ComponentBoundEvent->GetComponentPropertyName();
		FName DelegateName = ComponentBoundEvent->DelegatePropertyName;
		return *FString::Format(TEXT("On{0}{1}"), {
			*PropertyName.ToString(),
			*DelegateName.ToString()
			});
	}

	if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
	{
		FName FunctionName = EventNode->GetFunctionName();

		if (TargetClass)
		{
			if (UFunction* Func = TargetClass->FindFunctionByName(FunctionName))
			{
				return FName(*Func->GetDisplayNameText().ToString());
			}
		}
		return FunctionName;
	}

	if (UK2Node_EnhancedInputAction* EnhancedInputAction = Cast<UK2Node_EnhancedInputAction>(Node))
	{
		if (EnhancedInputAction->InputAction)
		{
			FString Formatted = FString::Format(
				TEXT("InpActEvt_{0}_{1}"),
				{ *EnhancedInputAction->InputAction->GetName(), *EnhancedInputAction->GetName() }
			);
			return FName(*Formatted);
		}
		return EnhancedInputAction->GetFName();
	}

	if (UK2Node_FunctionEntry* FunctionEntry = Cast<UK2Node_FunctionEntry>(Node))
	{
		return FName(FunctionEntry->FunctionReference.GetMemberName());
	}

	return NAME_None;
}
UEditorUtilityWidget* UBlueprintNativizationLibrary::OpenEditorUtilityWidget(UEditorUtilityWidgetBlueprint* WidgetBlueprint)
{
#if WITH_EDITOR
	if (!WidgetBlueprint)
	{
		return nullptr;
	}

	if (GEditor == nullptr)
	{
		return nullptr;
	}

	if (UEditorUtilitySubsystem* Subsystem = GEditor->GetEditorSubsystem<UEditorUtilitySubsystem>())
	{
		// Если такой EUW уже открыт, Subsystem сфокусирует существующую вкладку.
		return Subsystem->SpawnAndRegisterTab(WidgetBlueprint);
	}
#endif
	return nullptr;
}
void UBlueprintNativizationLibrary::OpenWidgetToFullscreen(UEditorUtilityWidget* Widget)
{
	if (!Widget || !FSlateApplication::IsInitialized())
		return;

	TSharedRef<SWidget> SlateWidget = Widget->TakeWidget();
	TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(SlateWidget);

	if (ParentWindow.IsValid())
	{
		ParentWindow->Maximize();
		FSlateApplication::Get().SetKeyboardFocus(SlateWidget, EFocusCause::SetDirectly);
		return;
	}
};

FString UBlueprintNativizationLibrary::TrimAfterLastComma(const FString& Input)
{
	int32 LastCommaIndex;
	if (Input.FindLastChar(',', LastCommaIndex))
	{
		return Input.Left(LastCommaIndex);
	}
	return Input;
}

bool UBlueprintNativizationLibrary::IsCppDefinedField(UField* Field)
{
	if (UClass* Class = Cast<UClass>(Field))
	{
		return Class->HasAnyClassFlags(CLASS_Native);
	}
	else if (UStruct* Struct = Cast<UStruct>(Field))
	{
		return Struct->IsNative();
	}
	else if (UEnum* Enum = Cast<UEnum>(Field))
	{
		return Enum->IsNative();
	}

	return false;
}

bool UBlueprintNativizationLibrary::IsCppDefinedProperty(FProperty* Property)
{
	if (!Property)
	{
		return false;
	}

	const UStruct* OwnerStruct = Property->GetOwnerStruct();
	if (!OwnerStruct)
	{
		return false;
	}

	return OwnerStruct->IsNative();
}

TArray<UStruct*> UBlueprintNativizationLibrary::FindStructsContainingProperty(FProperty* TargetProperty)
{
	TArray<UStruct*> Results;

	if (!TargetProperty)
	{
		return Results;
	}

	// ����������� �� ���� ��������� ���������� (������� ������, �������, ���������)
	for (TObjectIterator<UStruct> StructIt; StructIt; ++StructIt)
	{
		UStruct* Struct = *StructIt;

		// ���������� ��� FProperty ������ ���������
		for (FProperty* Prop = Struct->PropertyLink; Prop != nullptr; Prop = Prop->PropertyLinkNext)
		{
			if (Prop == TargetProperty)
			{
				Results.Add(Struct);
				break; // ����� ����� � �� ��� ����� ������ property � ���� �������
			}
		}
	}

	return Results;
}

bool UBlueprintNativizationLibrary::DoesFieldContainProperty(const FProperty* TargetProperty, const UField* ContainerField)
{
	if (!TargetProperty || !ContainerField)
	{
		return false;
	}

	const UStruct* Struct = Cast<UStruct>(ContainerField);
	if (!Struct)
	{
		return false;
	}

	for (TFieldIterator<FProperty> It(Struct, EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		FProperty* Property = *It;

		if (Property->GetFName() == TargetProperty->GetFName() &&
			Property->GetClass() == TargetProperty->GetClass())
		{
			return true;
		}
	}

	return false;
}

bool UBlueprintNativizationLibrary::IsAvailibleOneLineDefaultConstructorForProperty(FProperty* Property)
{
	if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
        if (UBlueprintNativizationSettingsLibrary::FindStructTranslator(StructProperty->Struct))
        {
            return true;
        }
		FConstructorDescriptor ConstructorDescriptor;
		if (UBlueprintNativizationSettingsLibrary::FindConstructorDescriptorByStruct(StructProperty->Struct, ConstructorDescriptor) || !StructProperty->Struct->IsNative())
		{
			return true;
		}
		else
		{
			return false;
		}
	}
	else if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		return IsAvailibleOneLineDefaultConstructorForProperty(ArrayProperty->Inner);
	}
	else if (FSetProperty* SetProperty = CastField<FSetProperty>(Property))
	{
		return IsAvailibleOneLineDefaultConstructorForProperty(SetProperty->ElementProp);
	}
	else if (FMapProperty* MapProperty = CastField<FMapProperty>(Property))
	{
		bool bKeyOk = IsAvailibleOneLineDefaultConstructorForProperty(MapProperty->KeyProp);
		if (!bKeyOk)
		{
			return false;
		}
		bool bValueOk = IsAvailibleOneLineDefaultConstructorForProperty(MapProperty->ValueProp);
		return bValueOk;
	}

	return true;
}


FString UBlueprintNativizationLibrary::GenerateTextLiteral(const FText& Value, const FString& TextNamespace, const FString& TextName)
{
    FString SerializedText;
    FTextStringHelper::WriteToBuffer(SerializedText, Value, true);
    const ETextGenerationMode TextGenerationMode = GetDefault<UBlueprintNativizationV2EditorSettings>()->TextGenerationMode;
    if (TextGenerationMode != ETextGenerationMode::PreserveTextData)
    {
        const FString SourceText = Value.BuildSourceString();
        if (TextGenerationMode == ETextGenerationMode::PlainText)
        {
            return FString::Format(TEXT("FText::FromString(TEXT(\"{0}\"))"), { SourceText.ReplaceCharWithEscapedChar() });
        }
        if (Value.IsCultureInvariant() || SourceText.IsEmpty())
        {
            return FString::Format(TEXT("FText::AsCultureInvariant(TEXT(\"{0}\"))"), { SourceText.ReplaceCharWithEscapedChar() });
        }
        FString LocalizationNamespace = TextNamespace;
        if (LocalizationNamespace.IsEmpty())
        {
            LocalizationNamespace = UBlueprintNativizationSettingsLibrary::GetModuleName();
        }
        FString LocalizationName = TextName;
        if (LocalizationName.IsEmpty())
        {
            LocalizationName = TEXT("Text");
        }
        FString TextIdentity = SerializedText;
        const TOptional<FString> OriginalNamespace = FTextInspector::GetNamespace(Value);
        const TOptional<FString> OriginalKey = FTextInspector::GetKey(Value);
        if (OriginalNamespace.IsSet() && OriginalKey.IsSet() && !OriginalKey.GetValue().IsEmpty())
        {
            TextIdentity = OriginalNamespace.GetValue() + TEXT("|") + OriginalKey.GetValue();
        }
        const FTCHARToUTF8 IdentityBytes(*TextIdentity);
        const FString LocalizationKey = LocalizationName + TEXT("_") +
            FSHA1::HashBuffer(IdentityBytes.Get(), IdentityBytes.Length()).ToString();
        return FString::Format(TEXT("NSLOCTEXT(\"{0}\", \"{1}\", \"{2}\")"),
            { LocalizationNamespace.ReplaceCharWithEscapedChar(), LocalizationKey.ReplaceCharWithEscapedChar(), SourceText.ReplaceCharWithEscapedChar() });
    }
    return TEXT("FTextStringHelper::CreateFromBuffer(TEXT(\"") + SerializedText.ReplaceCharWithEscapedChar() +
        TEXT("\"), nullptr, nullptr, true)");
}

static void RegisterAssetClassInclude(UClass* AssetClass)
{
    FString AssetClassInclude;
    if (GEngine->GetEngineSubsystem<UNativizationV2Subsystem>()->GetIncludeFromField(AssetClass, AssetClassInclude))
    {
        if (FNativeGenerationContext::bGeneratingInlineDefaults)
        {
            AssetClassInclude.RemoveFromStart(TEXT("#include \""));
            AssetClassInclude.RemoveFromEnd(TEXT("\""));
            FStructTranslationResult Requirements;
            Requirements.HeaderIncludes.Add(AssetClassInclude);
            FNativeGenerationContext::AddRequirements(Requirements);
        }
        else
        {
            FNativeGenerationContext::AddImplementationInclude(AssetClassInclude);
        }
    }
}

static bool RequiresDeferredObjectInitialization(FProperty* Property, const void* PropertyAddress,
    bool bIncludeNativeDependencies, TSet<const UObject*>& VisitedObjects)
{
    if (Property->IsA<FSoftObjectProperty>())
    {
        return false;
    }
    if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
    {
        UObject* Object = ObjectProperty->GetObjectPropertyValue(PropertyAddress);
        if (!Object)
        {
            return false;
        }
        if (UClass* Class = Cast<UClass>(Object))
        {
            if (!Class->HasAnyClassFlags(CLASS_Native))
            {
                return bIncludeNativeDependencies || FRetainedBlueprintGeneration::IsRetained(Class);
            }
            return false;
        }
        if (Object->IsA<UBlueprint>() || !Object->GetClass()->HasAnyClassFlags(CLASS_Native))
        {
            return true;
        }
        if (Object->IsAsset() && bIncludeNativeDependencies)
        {
            return true;
        }
        if (Property->HasAnyPropertyFlags(CPF_InstancedReference | CPF_PersistentInstance) && !VisitedObjects.Contains(Object))
        {
            VisitedObjects.Add(Object);
            for (TFieldIterator<FProperty> PropertyIterator(Object->GetClass()); PropertyIterator; ++PropertyIterator)
            {
                FProperty* NestedProperty = *PropertyIterator;
                for (int32 ArrayIndex = 0; ArrayIndex < NestedProperty->ArrayDim; ++ArrayIndex)
                {
                    if (RequiresDeferredObjectInitialization(NestedProperty,
                        NestedProperty->ContainerPtrToValuePtr<void>(Object, ArrayIndex), bIncludeNativeDependencies, VisitedObjects))
                    {
                        return true;
                    }
                }
            }
        }
        return false;
    }
    if (FStructProperty* StructureProperty = CastField<FStructProperty>(Property))
    {
        UScriptStruct* Structure = StructureProperty->Struct;
        const void* StructureMemory = PropertyAddress;
        if (Structure == FInstancedStruct::StaticStruct())
        {
            const FInstancedStruct& Instance = *static_cast<const FInstancedStruct*>(PropertyAddress);
            if (!Instance.IsValid())
            {
                return false;
            }
            Structure = const_cast<UScriptStruct*>(Instance.GetScriptStruct());
            StructureMemory = Instance.GetMemory();
        }
        for (TFieldIterator<FProperty> PropertyIterator(Structure); PropertyIterator; ++PropertyIterator)
        {
            FProperty* NestedProperty = *PropertyIterator;
            for (int32 ArrayIndex = 0; ArrayIndex < NestedProperty->ArrayDim; ++ArrayIndex)
            {
                if (RequiresDeferredObjectInitialization(NestedProperty,
                    NestedProperty->ContainerPtrToValuePtr<void>(StructureMemory, ArrayIndex), bIncludeNativeDependencies, VisitedObjects))
                {
                    return true;
                }
            }
        }
    }
    else if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
    {
        FScriptArrayHelper ArrayValues(ArrayProperty, PropertyAddress);
        for (int32 ElementIndex = 0; ElementIndex < ArrayValues.Num(); ++ElementIndex)
        {
            if (RequiresDeferredObjectInitialization(ArrayProperty->Inner, ArrayValues.GetRawPtr(ElementIndex),
                bIncludeNativeDependencies, VisitedObjects))
            {
                return true;
            }
        }
    }
    else if (FSetProperty* SetProperty = CastField<FSetProperty>(Property))
    {
        FScriptSetHelper SetValues(SetProperty, PropertyAddress);
        for (int32 ElementIndex = 0; ElementIndex < SetValues.GetMaxIndex(); ++ElementIndex)
        {
            if (SetValues.IsValidIndex(ElementIndex) && RequiresDeferredObjectInitialization(SetProperty->ElementProp,
                SetValues.GetElementPtr(ElementIndex), bIncludeNativeDependencies, VisitedObjects))
            {
                return true;
            }
        }
    }
    else if (FMapProperty* MapProperty = CastField<FMapProperty>(Property))
    {
        FScriptMapHelper MapValues(MapProperty, PropertyAddress);
        for (int32 ElementIndex = 0; ElementIndex < MapValues.GetMaxIndex(); ++ElementIndex)
        {
            if (!MapValues.IsValidIndex(ElementIndex))
            {
                continue;
            }
            if (RequiresDeferredObjectInitialization(MapProperty->KeyProp, MapValues.GetKeyPtr(ElementIndex),
                bIncludeNativeDependencies, VisitedObjects) ||
                RequiresDeferredObjectInitialization(MapProperty->ValueProp, MapValues.GetValuePtr(ElementIndex),
                    bIncludeNativeDependencies, VisitedObjects))
            {
                return true;
            }
        }
    }
    return false;
}

bool UBlueprintNativizationLibrary::RequiresPostCDOInitialization(FProperty* Property, const void* PropertyAddress,
    bool bIncludeNativeDependencies)
{
    TSet<const UObject*> VisitedObjects;
    return RequiresDeferredObjectInitialization(Property, PropertyAddress, bIncludeNativeDependencies, VisitedObjects);
}

FString UBlueprintNativizationLibrary::GenerateOneLineDefaultConstructorForProperty(FProperty* Property, bool LeftAllAssetRefInBlueprint, void* PropertyAddress, const FString& ObjectOuter)
{
	if (!Property || !PropertyAddress)
	{
		return TEXT("/* Invalid */");
	}

    if (Property->IsA<FInterfaceProperty>())
    {
        return TEXT("{}");
    }
    if (Property->IsA<FSoftObjectProperty>() || Property->IsA<FInt64Property>())
    {
        FEdGraphPinType Type;
        ConvertPropertyToPinType(Property, Type);
        if (LeftAllAssetRefInBlueprint && Property->IsA<FSoftObjectProperty>())
        {
            return GetPinType(Type, false) + TEXT("{}");
        }
        FString Value;
        Property->ExportTextItem_Direct(Value, PropertyAddress, nullptr, nullptr, PPF_None);
        return GenerateOneLineDefaultConstructor(Property->GetName(), Type, Value, nullptr, LeftAllAssetRefInBlueprint);
    }
	if (FIntProperty* IntProp = CastField<FIntProperty>(Property))
	{
		return FString::FromInt(IntProp->GetPropertyValue(PropertyAddress));
	}
	else if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Property))
	{
		return FStructInitialization::Number(FloatProp->GetPropertyValue(PropertyAddress));
	}
    else if (FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
    {
        UClass* DefaultClass = Cast<UClass>(ClassProperty->GetObjectPropertyValue(PropertyAddress));
        if (LeftAllAssetRefInBlueprint && DefaultClass && !DefaultClass->HasAnyClassFlags(CLASS_Native))
        {
            return TEXT("nullptr");
        }
        FEdGraphPinType Type;
        ConvertPropertyToPinType(Property, Type);
        return GenerateOneLineDefaultConstructor(Property->GetName(), Type, FString(),
            ClassProperty->GetObjectPropertyValue(PropertyAddress), LeftAllAssetRefInBlueprint);
    }
	else if (FObjectPropertyBase* ObjectPropertyBase = CastField<FObjectPropertyBase>(Property))
	{
		UObject* Object = ObjectPropertyBase->GetObjectPropertyValue(PropertyAddress);
        if (Object && Property->HasAnyPropertyFlags(CPF_InstancedReference | CPF_PersistentInstance))
        {
            return FRetainedBlueprintGeneration::ConstructInstancedObject(ObjectPropertyBase, Object, ObjectOuter, LeftAllAssetRefInBlueprint);
        }
		if (Object && Object->IsAsset())
		{
			if (LeftAllAssetRefInBlueprint)
			{
				return FString(TEXT("nullptr"));
			}
			else
			{
				FString AssetPath = FSoftObjectPath(Object).ToString();
				RegisterAssetClassInclude(Object->GetClass());
				return FString::Format(TEXT("LoadObject<{0}>(nullptr, TEXT(\"{1}\"))"),
					TArray<FStringFormatArg>{ FStringFormatArg(FRetainedBlueprintGeneration::GetClassName(Object->GetClass())), FStringFormatArg(AssetPath) });
			}
		}
		else
		{
			return FString(TEXT("nullptr"));
		}
	}

	else if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
	{
		return FString::Format(TEXT("TEXT(\"{0}\")"), { StrProp->GetPropertyValue(PropertyAddress).ReplaceCharWithEscapedChar() });
	}
	else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
	{
		return BoolProp->GetPropertyValue(PropertyAddress) ? TEXT("true") : TEXT("false");
	}
	else if (FNameProperty* NameProp = CastField<FNameProperty>(Property))
	{
		FName Name = NameProp->GetPropertyValue(PropertyAddress);
		return FString::Format(TEXT("FName(TEXT(\"{0}\"))"), { NameProp->GetPropertyValue(PropertyAddress).ToString().ReplaceCharWithEscapedChar() });
	}
	else if (FTextProperty* TextProp = CastField<FTextProperty>(Property))
	{
		return GenerateTextLiteral(TextProp->GetPropertyValue(PropertyAddress), Property->GetPathName(), ObjectOuter + TEXT(".") + Property->GetName());
	}
	else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Property))
	{
		return  FStructInitialization::Number(DoubleProp->GetPropertyValue(PropertyAddress));
	}
	else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
	{
		UEnum* Enum = EnumProp->GetEnum();
		int64 EnumValue = EnumProp->GetUnderlyingProperty()->GetSignedIntPropertyValue(PropertyAddress);
        return GenerateEnumLiteral(Enum, EnumValue);
	}
	else if (FByteProperty* ByteProp = CastField<FByteProperty>(Property))
	{
		if (UEnum* Enum = ByteProp->Enum)
		{
			uint8 EnumValue = ByteProp->GetPropertyValue(PropertyAddress);
            return GenerateEnumLiteral(Enum, EnumValue);
		}
		else
		{
			uint8 EnumValue = ByteProp->GetPropertyValue(PropertyAddress);
			return FString::FromInt(EnumValue);
		}
	}
    if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
    {
        if (NumericProperty->IsInteger())
        {
            if (Property->IsA<FUInt64Property>())
            {
                return LexToString(NumericProperty->GetUnsignedIntPropertyValue(PropertyAddress)) + TEXT("ull");
            }
            return LexToString(NumericProperty->GetSignedIntPropertyValue(PropertyAddress));
        }
        return FStructInitialization::Number(NumericProperty->GetFloatingPointPropertyValue(PropertyAddress));
    }
	if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
        if (UBlueprintNativizationSettingsLibrary::FindStructTranslator(StructProperty->Struct))
        {
            FStructTranslationRequest Request;
            Request.Structure = StructProperty->Struct;
            Request.Property = Property;
            Request.PropertyValue = PropertyAddress;
            Request.bKeepAssetReferencesInBlueprint = LeftAllAssetRefInBlueprint;
            Request.OwnerExpression = ObjectOuter;
            Request.Operation = EStructTranslationOperation::PropertyValue;
            return UBlueprintNativizationSettingsLibrary::TranslateStructure(Request);
        }
        FConstructorDescriptor Constructor;
        if (StructProperty->Struct->IsNative() &&
            !UBlueprintNativizationSettingsLibrary::FindConstructorDescriptorByStruct(StructProperty->Struct, Constructor))
        {
            FStructTranslationRequest Request;
            Request.Structure = StructProperty->Struct;
            Request.Property = Property;
            Request.PropertyValue = PropertyAddress;
            Request.bKeepAssetReferencesInBlueprint = LeftAllAssetRefInBlueprint;
            Request.OwnerExpression = ObjectOuter;
            Request.Operation = EStructTranslationOperation::PropertyValue;
            return UBlueprintNativizationSettingsLibrary::TranslateStructure(Request);
        }
		const void* StructValuePtr = PropertyAddress;
		FString Result = "";
		Result += UBlueprintNativizationLibrary::GetUniqueFieldName(StructProperty->Struct);
		Result += TEXT("(");

		bool bFirst = true;
		TArray<FName> PropertyNames = GetAllPropertyNamesByStruct(StructProperty->Struct);
		for (FName PropertyName : PropertyNames)
		{
			FProperty* InnerProp = StructProperty->Struct->FindPropertyByName(PropertyName);
			if (!InnerProp)
				continue;

			if (!bFirst)
			{
				Result += TEXT(", ");
			}

			void* InnerPropertyAddress = InnerProp->ContainerPtrToValuePtr<void>(PropertyAddress);

            FString Argument = GenerateOneLineDefaultConstructorForProperty(InnerProp, LeftAllAssetRefInBlueprint, InnerPropertyAddress, ObjectOuter);
            if (InnerProp->IsA<FArrayProperty>() || InnerProp->IsA<FSetProperty>() || InnerProp->IsA<FMapProperty>())
            {
                Argument = GetPropertyType(InnerProp) + Argument;
            }
            Result += Argument;
			bFirst = false;
		}

		Result += TEXT(")");
		return Result;
	}
	else if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		void* RawArrayData = ArrayProperty->GetPropertyValuePtr(PropertyAddress);
		FScriptArrayHelper Helper(ArrayProperty, RawArrayData);

		FString Result = TEXT("{");
		for (int32 i = 0; i < Helper.Num(); ++i)
		{
			if (i > 0) Result += TEXT(", ");
			Result += GenerateOneLineDefaultConstructorForProperty(ArrayProperty->Inner, LeftAllAssetRefInBlueprint, Helper.GetRawPtr(i), ObjectOuter);
		}
		Result += TEXT("}");
		return Result;
	}
	else if (FSetProperty* SetProperty = CastField<FSetProperty>(Property))
	{
		void* RawSetData = SetProperty->GetPropertyValuePtr(PropertyAddress);
		FScriptSetHelper Helper(SetProperty, RawSetData);

		FString Result = TEXT("{");

		int32 i = 0;
		for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
		{
			if (Helper.IsValidIndex(Index))
			{
				if (i++ > 0) Result += TEXT(", ");
				Result += GenerateOneLineDefaultConstructorForProperty(SetProperty->ElementProp, LeftAllAssetRefInBlueprint, Helper.GetElementPtr(Index), ObjectOuter);
			}
		}
		Result += TEXT("}");
		return Result;
	}
	else if (FMapProperty* MapProperty = CastField<FMapProperty>(Property))
	{
		FScriptMapHelper Helper(MapProperty, PropertyAddress);

		FString Result = TEXT("{");

		int32 i = 0;
		for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
		{
			if (Helper.IsValidIndex(Index))
			{
				if (i++ > 0) Result += TEXT(", ");

				FString KeyStr = GenerateOneLineDefaultConstructorForProperty(MapProperty->KeyProp, LeftAllAssetRefInBlueprint, Helper.GetKeyPtr(Index), ObjectOuter);
				FString ValueStr = GenerateOneLineDefaultConstructorForProperty(MapProperty->ValueProp, LeftAllAssetRefInBlueprint, Helper.GetValuePtr(Index), ObjectOuter);

				Result += FString::Printf(TEXT("{%s, %s}"), *KeyStr, *ValueStr);
			}
		}
		Result += TEXT("}");
		return Result;
	}
	return "";
}

UEdGraph* UBlueprintNativizationLibrary::GetConstructionGraph(UBlueprint* Blueprint)
{
	if (!Blueprint)
		return nullptr;

	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetName() == TEXT("ConstructionScript"))
		{
			return Graph;
		}
	}
	return nullptr;
};

static void ClearDefaultAssetValues(FProperty* Property, void* Value)
{
    if (FSoftObjectProperty* SoftObject = CastField<FSoftObjectProperty>(Property))
    {
        SoftObject->ClearValue(Value);
    }
    else if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
    {
        UObject* Object = ObjectProperty->GetObjectPropertyValue(Value);
        if (Object && Object->IsAsset())
        {
            ObjectProperty->SetObjectPropertyValue(Value, nullptr);
        }
    }
    else if (FStructProperty* Structure = CastField<FStructProperty>(Property))
    {
        for (TFieldIterator<FProperty> Member(Structure->Struct); Member; ++Member)
        {
            ClearDefaultAssetValues(*Member, Member->ContainerPtrToValuePtr<void>(Value));
        }
    }
    else if (FArrayProperty* Array = CastField<FArrayProperty>(Property))
    {
        FScriptArrayHelper Elements(Array, Value);
        for (int32 Index = 0; Index < Elements.Num(); ++Index)
        {
            ClearDefaultAssetValues(Array->Inner, Elements.GetRawPtr(Index));
        }
    }
    else if (FSetProperty* Set = CastField<FSetProperty>(Property))
    {
        FScriptSetHelper Elements(Set, Value);
        for (int32 Index = 0; Index < Elements.GetMaxIndex(); ++Index)
        {
            if (Elements.IsValidIndex(Index))
            {
                ClearDefaultAssetValues(Set->ElementProp, Elements.GetElementPtr(Index));
            }
        }
        Elements.Rehash();
    }
    else if (FMapProperty* Map = CastField<FMapProperty>(Property))
    {
        FScriptMapHelper Elements(Map, Value);
        for (int32 Index = 0; Index < Elements.GetMaxIndex(); ++Index)
        {
            if (Elements.IsValidIndex(Index))
            {
                ClearDefaultAssetValues(Map->KeyProp, Elements.GetKeyPtr(Index));
                ClearDefaultAssetValues(Map->ValueProp, Elements.GetValuePtr(Index));
            }
        }
        Elements.Rehash();
    }
}

FString UBlueprintNativizationLibrary::GenerateManyLinesDefaultConstructorForProperty(FProperty* Property,
	void* PropertyAddress,
	void* SuperPropertyAddress,
	TArray<FGenerateFunctionStruct> EntryNodes, UClass* Class, bool FullInitialize, bool LeftAllAssetRefInBlueprint, FString PrefixComponentName, FString& NewPropertyName, const FString& ObjectOuter)
{
	FString Content;

	if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		FString StructName;
        bool bRequiresSerializedInitialization = false;
        if (StructProperty->Struct->IsNative() && !UBlueprintNativizationSettingsLibrary::FindStructTranslator(StructProperty->Struct))
        {
            for (TFieldIterator<FProperty> Member(StructProperty->Struct); Member; ++Member)
            {
                bRequiresSerializedInitialization |= Member->HasAnyPropertyFlags(CPF_NativeAccessSpecifierPrivate | CPF_NativeAccessSpecifierProtected);
            }
        }
		if (FullInitialize)
		{
			StructName = UBlueprintNativizationLibrary::GenerateLambdaConstructorUniqueVariableName(Class, StructProperty->GetName() + TEXT("DefaultValue"));
            const FString StructureType = GetUniqueFieldName(StructProperty->Struct);
            if (bRequiresSerializedInitialization)
            {
                FStructTranslationResult Requirements;
                Requirements.HeaderIncludes.Add(TEXT("UObject/StructOnScope.h"));
                FNativeGenerationContext::AddRequirements(Requirements);
                const FString StorageName = GenerateLambdaConstructorUniqueVariableName(Class, StructProperty->GetName() + TEXT("Storage"));
                Content += TEXT("FStructOnScope ") + StorageName + TEXT("(") + StructureType + TEXT("::StaticStruct());\n") +
                    StructureType + TEXT("& ") + StructName + TEXT(" = *reinterpret_cast<") + StructureType + TEXT("*>(") +
                    StorageName + TEXT(".GetStructMemory());\n");
            }
            else
            {
                Content += StructureType + TEXT(" ") + StructName + TEXT("{};\n");
            }
		}
		else
		{
            StructName = PrefixComponentName + UBlueprintNativizationLibrary::GetUniquePropertyName(Property);
            if (Property->GetOwnerClass() && Property->GetOwnerClass()->HasAnyClassFlags(CLASS_Native) &&
                Property->HasAnyPropertyFlags(CPF_NativeAccessSpecifierPrivate | CPF_NativeAccessSpecifierProtected))
            {
                FStructTranslationResult Requirements;
                Requirements.HeaderIncludes.Add(TEXT("UObject/UnrealType.h"));
                FNativeGenerationContext::AddImplementationInclude(TEXT("#include \"Misc/FeedbackContext.h\""));
                FNativeGenerationContext::AddRequirements(Requirements);
                StructName = TEXT("(*static_cast<") + GetUniqueFieldName(StructProperty->Struct) +
                    TEXT("*>(FindFProperty<FStructProperty>((") + ObjectOuter + TEXT(")->GetClass(), ") +
                    FRetainedBlueprintGeneration::Quote(Property->GetName()) + TEXT(")->ContainerPtrToValuePtr<void>(") + ObjectOuter + TEXT(")))");
            }
        }
        if (UBlueprintNativizationSettingsLibrary::FindStructTranslator(StructProperty->Struct))
        {
            FStructTranslationRequest Request;
            Request.Structure = StructProperty->Struct;
            Request.Property = Property;
            Request.PropertyValue = PropertyAddress;
            Request.OwnerClass = Class;
            Request.OwnerExpression = ObjectOuter;
            FStructOnScope NativeDefaults(StructProperty->Struct);
            if (FullInitialize)
            {
                Request.BaseValue = NativeDefaults.GetStructMemory();
            }
            Request.DestinationExpression = StructName;
            Request.InitializationStage = EStructInitializationStage::NativeConstructor;
            Request.bKeepAssetReferencesInBlueprint = LeftAllAssetRefInBlueprint;
            Request.Operation = EStructTranslationOperation::PropertyValue;
            NewPropertyName = StructName;
            return Content + UBlueprintNativizationSettingsLibrary::TranslateStructure(Request);
        }
        if (bRequiresSerializedInitialization)
        {
            FStructOnScope SerializedDefaults(StructProperty->Struct);
            StructProperty->Struct->CopyScriptStruct(SerializedDefaults.GetStructMemory(), PropertyAddress);
            if (LeftAllAssetRefInBlueprint)
            {
                for (TFieldIterator<FProperty> Member(StructProperty->Struct); Member; ++Member)
                {
                    ClearDefaultAssetValues(*Member, Member->ContainerPtrToValuePtr<void>(SerializedDefaults.GetStructMemory()));
                }
            }
            FStructTranslationRequest Request;
            Request.Structure = StructProperty->Struct;
            Request.PropertyValue = SerializedDefaults.GetStructMemory();
            Request.OwnerClass = Class;
            Request.OwnerExpression = ObjectOuter;
            Request.DestinationExpression = StructName;
            Request.InitializationStage = EStructInitializationStage::NativeConstructor;
            Request.bKeepAssetReferencesInBlueprint = LeftAllAssetRefInBlueprint;
            Request.Operation = EStructTranslationOperation::PropertyValue;
            NewPropertyName = StructName;
            return Content + UBlueprintNativizationSettingsLibrary::TranslateStructure(Request);
        }

        for (TFieldIterator<FProperty> PropertyIterator(StructProperty->Struct); PropertyIterator; ++PropertyIterator)
        {
            FProperty* LocalProperty = *PropertyIterator;
            for (int32 ElementIndex = 0; ElementIndex < LocalProperty->ArrayDim; ++ElementIndex)
            {
                void* LocalValue = nullptr;
                void* SuperValue = nullptr;
                if (PropertyAddress)
                {
                    LocalValue = LocalProperty->ContainerPtrToValuePtr<void>(PropertyAddress, ElementIndex);
                }
                if (SuperPropertyAddress)
                {
                    SuperValue = LocalProperty->ContainerPtrToValuePtr<void>(SuperPropertyAddress, ElementIndex);
                }
                if (!LocalValue && !SuperValue)
                {
                    return TEXT("");
                }
                FString LocalValueString;
                FString SuperValueString;
                LocalProperty->ExportText_Direct(LocalValueString, LocalValue, nullptr, nullptr, 0);
                if (SuperValue)
                {
                    LocalProperty->ExportText_Direct(SuperValueString, SuperValue, nullptr, nullptr, 0);
                }
                if (!SuperValue || LocalValueString != SuperValueString || FullInitialize)
                {
                    FString Destination = StructName + TEXT(".") + GetUniquePropertyName(LocalProperty);
                    if (LocalProperty->ArrayDim > 1)
                    {
                        Destination += FString::Format(TEXT("[{0}]"), {ElementIndex});
                    }
                    FString Initializer;
                    if (!IsAvailibleOneLineDefaultConstructorForProperty(LocalProperty))
                    {
                        Content += GenerateManyLinesDefaultConstructorForProperty(LocalProperty, LocalValue, SuperValue,
                            EntryNodes, Class, true, LeftAllAssetRefInBlueprint, TEXT(""), Initializer, ObjectOuter);
                    }
                    else
                    {
                        Initializer = GenerateOneLineDefaultConstructorForProperty(LocalProperty, LeftAllAssetRefInBlueprint, LocalValue, ObjectOuter);
                    }
                    Content += FString::Format(TEXT("{0} = {1};\n"), {Destination, Initializer});
                }
            }
        }
		NewPropertyName = StructName;
		return Content;
	}

	else if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		FString ArrayName = UBlueprintNativizationLibrary::GenerateLambdaConstructorUniqueVariableName(Class, ArrayProperty->GetName() + TEXT("DefaultValue"));
		Content += UBlueprintNativizationLibrary::GetPropertyType(ArrayProperty) + TEXT(" ") + ArrayName + TEXT("{};\n");
		
		void* RawArrayData = ArrayProperty->GetPropertyValuePtr(PropertyAddress);
		FScriptArrayHelper Helper(ArrayProperty, RawArrayData);

		for (int32 i = 0; i < Helper.Num(); ++i)
		{
			FString InsidePropertyName;
			Content += GenerateManyLinesDefaultConstructorForProperty(ArrayProperty->Inner, Helper.GetRawPtr(i), nullptr, EntryNodes, Class, true, LeftAllAssetRefInBlueprint, "", InsidePropertyName, ObjectOuter);
			Content += FString::Format(TEXT("{0}.Add({1});\n"), { ArrayName, InsidePropertyName });
		}
		NewPropertyName = ArrayName;
        if (!FullInitialize)
        {
            Content += PrefixComponentName + GetUniquePropertyName(Property) + TEXT(" = ") + ArrayName + TEXT(";\n");
        }
		return Content;
	}
	else if (FSetProperty* SetProperty = CastField<FSetProperty>(Property))
	{
		FString SetName = UBlueprintNativizationLibrary::GenerateLambdaConstructorUniqueVariableName(Class, SetProperty->GetName() + TEXT("DefaultValue"));
		Content += UBlueprintNativizationLibrary::GetPropertyType(SetProperty) + TEXT(" ") + SetName + TEXT("{};\n");

		void* RawSetData = SetProperty->GetPropertyValuePtr(PropertyAddress);
		FScriptSetHelper Helper(SetProperty, RawSetData);

		for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
		{
			if (Helper.IsValidIndex(Index))
			{
				FString InsidePropertyName;
				Content += GenerateManyLinesDefaultConstructorForProperty(SetProperty->ElementProp, Helper.GetElementPtr(Index), nullptr, EntryNodes, Class, true, LeftAllAssetRefInBlueprint, "", InsidePropertyName, ObjectOuter);
				Content += FString::Format(TEXT("{0}.Add({1});\n"), { SetName, InsidePropertyName });
			}
		}
		NewPropertyName = SetName;
        if (!FullInitialize)
        {
            Content += PrefixComponentName + GetUniquePropertyName(Property) + TEXT(" = ") + SetName + TEXT(";\n");
        }
		return Content;
	}
	else if (FMapProperty* MapProperty = CastField<FMapProperty>(Property))
	{
		FString MapName = UBlueprintNativizationLibrary::GenerateLambdaConstructorUniqueVariableName(Class, MapProperty->GetName() + TEXT("DefaultValue"));
		Content += UBlueprintNativizationLibrary::GetPropertyType(MapProperty) + TEXT(" ") + MapName + TEXT("{};\n");

		FScriptMapHelper Helper(MapProperty, PropertyAddress);

		for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
		{
			if (Helper.IsValidIndex(Index))
			{
				FString InsideKeyPropertyName;
				Content += GenerateManyLinesDefaultConstructorForProperty(MapProperty->KeyProp, Helper.GetKeyPtr(Index), nullptr, EntryNodes, Class, true, LeftAllAssetRefInBlueprint, "", InsideKeyPropertyName, ObjectOuter);

				FString InsideValuePropertyName;
				Content += GenerateManyLinesDefaultConstructorForProperty(MapProperty->ValueProp, Helper.GetValuePtr(Index), nullptr, EntryNodes, Class, true, LeftAllAssetRefInBlueprint, "", InsideValuePropertyName, ObjectOuter);

				Content += FString::Format(TEXT("{0}.Add({1}, {2});\n"), { MapName, InsideKeyPropertyName, InsideValuePropertyName });

			}
		}
		NewPropertyName = MapName;
        if (!FullInitialize)
        {
            Content += PrefixComponentName + GetUniquePropertyName(Property) + TEXT(" = ") + MapName + TEXT(";\n");
        }
		return Content;
	}
    NewPropertyName = GenerateOneLineDefaultConstructorForProperty(Property, LeftAllAssetRefInBlueprint, PropertyAddress, ObjectOuter);
    return FString();
}

FString UBlueprintNativizationLibrary::ProcessFloatStrings(const FString& Input)
{
	FString Result = Input;
	const FRegexPattern FloatPattern(TEXT(R"(([-+]?\d+\.\d+))"));
	FRegexMatcher Matcher(FloatPattern, Result);

	// Массив для хранения найденных совпадений: позиция и исходное значение
	struct FMatchInfo
	{
		int32 Start;
		int32 End;
		FString Text;
	};
	TArray<FMatchInfo> Matches;

	while (Matcher.FindNext())
	{
		FMatchInfo Info;
		Info.Start = Matcher.GetMatchBeginning();
		Info.End = Matcher.GetMatchEnding();
		Info.Text = Result.Mid(Info.Start, Info.End - Info.Start);
		Matches.Add(Info);
	}

	for (int32 i = Matches.Num() - 1; i >= 0; --i)
	{
		const FMatchInfo& Info = Matches[i];
		FString Trimmed = UBlueprintNativizationLibrary::TrimFloatToMeaningfulString(Info.Text);
		Result = Result.Left(Info.Start) + Trimmed + Result.Mid(Info.End);
	}

	return Result;
}


FString UBlueprintNativizationLibrary::TrimFloatToMeaningfulString(FString Value)
{
	FString IntPart, FracPart;
	if (!Value.Split(TEXT("."), &IntPart, &FracPart))
	{
		return Value;
	}

	int32 TrimIndex = FracPart.Len();
	while (TrimIndex > 1 && FracPart[TrimIndex - 1] == '0')
	{
		--TrimIndex;
	}
	FracPart = FracPart.Left(TrimIndex);

	TArray<FStringFormatArg> Args;
	Args.Add(IntPart);
	Args.Add(FracPart);

	return FString::Format(TEXT("{0}.{1}"), Args);
}

bool UBlueprintNativizationLibrary::IsNotOnlyHyphenAndBrackets(FString Input)
{
	for (TCHAR Char : Input)
	{
		if (Char != '(' && Char != ')')
		{
			return true;
		}
	}
	return false;
}

FString UBlueprintNativizationLibrary::GenerateManyLinesDefaultConstructor(FString Name, FEdGraphPinType Type, FString DefaultValue, UClass* Class, bool IsConstructorFunction, UK2Node* EntryNode, TArray<FGenerateFunctionStruct> EntryNodes, bool FullInitialize, bool LeftAllAssetRefInBlueprint, FString& OutVariableName)
{
	FString Content;

	auto TrimFirstAndLast = [](const FString& Str) -> FString
		{
			const FString Value = Str.TrimStartAndEnd();
            if (Value.StartsWith(TEXT("(")) && Value.EndsWith(TEXT(")")))
            {
                return Value.Mid(1, Value.Len() - 2);
            }
            return Value;
		};

	if (Type.PinCategory == UEdGraphSchema_K2::PC_Struct)
	{
		UScriptStruct* ScriptStruct = Cast<UScriptStruct>(Type.PinSubCategoryObject.Get());
		if (!ScriptStruct) return "";
        if (UBlueprintNativizationSettingsLibrary::FindStructTranslator(ScriptStruct))
        {
            OutVariableName = Name;
            if (FullInitialize)
            {
                OutVariableName = GenerateLambdaConstructorUniqueVariableName(Class, Name);
                Content = GetUniqueFieldName(ScriptStruct) + TEXT(" ") + OutVariableName + TEXT("{};\n");
            }
            FStructTranslationRequest Request;
            Request.Structure = ScriptStruct;
            Request.SerializedValue = DefaultValue;
            Request.OwnerClass = Class;
            Request.DestinationExpression = OutVariableName;
            Request.bKeepAssetReferencesInBlueprint = LeftAllAssetRefInBlueprint;
            Request.Operation = EStructTranslationOperation::SerializedValue;
            if (IsConstructorFunction)
            {
                Request.InitializationStage = EStructInitializationStage::NativeConstructor;
            }
            return Content + UBlueprintNativizationSettingsLibrary::TranslateStructure(Request);
        }


		if (!IsNotOnlyHyphenAndBrackets(DefaultValue))
		{
			OutVariableName = Name;
            if (FullInitialize)
            {
                return FString::Format(TEXT("{0} {1}{};\n"), { GetUniqueFieldName(ScriptStruct), Name });
            }
            return Name + TEXT(" = {};\n");
		}
		FString StructName = "";
		if (IsConstructorFunction)
		{
			StructName = FullInitialize ? GenerateLambdaConstructorUniqueVariableName(Class, Name) : Name;
		}
		else
		{
			UFunction* Function = GetFunctionByNodeAndEntryNodes(EntryNode, EntryNodes);
			StructName = FullInitialize ? GetLambdaUniqueVariableNameByFunction(Name, Function, EntryNodes) : Name;
		}


		if (FullInitialize)
		{
			Content += FString::Format(TEXT("{0} {1};\n"), { GetUniqueFieldName(ScriptStruct), StructName });
		}

		int Count = 0;
		DefaultValue = TrimFirstAndLast(DefaultValue);

		for (TFieldIterator<FProperty> It(ScriptStruct); It; ++It)
		{
			FProperty* Property = *It;
			TArray<FString> Parts;
			SplitIgnoringParentheses(DefaultValue, Parts);

			FString ValueStr = "";
			for (FString Part : Parts)
			{
				if (Part.Contains(Property->GetName()))
				{
					ValueStr = ExtractNestedParam(Part, Property->GetName());
					break;
				}
			}

			if (ValueStr.IsEmpty())
			{
				ValueStr = GetStructPart(DefaultValue, Count);
			}

			FEdGraphPinType InnerPinType;
			ConvertPropertyToPinType(Property, InnerPinType);

			FString InnerVar;
			if (!IsAvailibleOneLineDefaultConstructorForProperty(Property))
			{
				Content += GenerateManyLinesDefaultConstructor(Property->GetName(), InnerPinType, ValueStr, Class, IsConstructorFunction, EntryNode, EntryNodes, true, LeftAllAssetRefInBlueprint, InnerVar);
			}
			else
			{
				InnerVar = GenerateOneLineDefaultConstructor(Property->GetName(), InnerPinType, ValueStr, nullptr, LeftAllAssetRefInBlueprint);
			}

			Content += FString::Format(TEXT("{0}.{1} = {2};\n"), { StructName, Property->GetNameCPP(), InnerVar });
			Count++;
		}
		OutVariableName = StructName;
		return Content;
	}

	else if (Type.IsArray() && Type.IsSet())
	{
		FString ContainerName = "";
		if (IsConstructorFunction)
		{
			ContainerName = GenerateLambdaConstructorUniqueVariableName(Class, Name);
		}
		else
		{
			UFunction* Function = GetFunctionByNodeAndEntryNodes(EntryNode, EntryNodes);
			ContainerName = FullInitialize ? GetLambdaUniqueVariableNameByFunction(Name, Function, EntryNodes) : Name;
		}

		Content += FString::Format(TEXT("{0} {1};\n"), { GetPinType(Type, false), ContainerName });

		TArray<FString> Tokens;
		DefaultValue = TrimFirstAndLast(DefaultValue);
		SplitIgnoringParentheses(DefaultValue, Tokens);

		for (const FString& Token : Tokens)
		{
			FString ElemVar;
			FEdGraphPinType CopyType = Type;
			CopyType.ContainerType = EPinContainerType::None; 

			Content += GenerateManyLinesDefaultConstructor(Name, CopyType, Token.TrimStartAndEnd(), Class, IsConstructorFunction, EntryNode, EntryNodes, true, LeftAllAssetRefInBlueprint, ElemVar);
			Content += FString::Format(TEXT("{0}.Add({1});\n"), {ContainerName, ElemVar });
		}
		OutVariableName = ContainerName;
		return Content;
	}


	else if (Type.IsMap())
	{
		FString ContainerName = "";
		if (IsConstructorFunction)
		{
			ContainerName = GenerateLambdaConstructorUniqueVariableName(Class, Name);
		}
		else
		{
			UFunction* Function = GetFunctionByNodeAndEntryNodes(EntryNode, EntryNodes);
			ContainerName = FullInitialize ? GetLambdaUniqueVariableNameByFunction(Name, Function, EntryNodes) : Name;
		}

		Content += FString::Format(TEXT("{0} {2};\n"),
			{ GetPinType(Type, false), ContainerName });

		TArray<FString> Tokens;
		DefaultValue = TrimFirstAndLast(DefaultValue);
		SplitIgnoringParentheses(DefaultValue, Tokens);

		FString WorkingText = DefaultValue;
		for (const FString& Token : Tokens)
		{
			FString ElemVarKey;
			FEdGraphPinType CopyTypeKey = Type;
			CopyTypeKey.ContainerType = EPinContainerType::None;

			FString ElemVarValue;
			FEdGraphPinType CopyTypeValue = Type;
			CopyTypeValue.ContainerType = EPinContainerType::None;
			CopyTypeValue.PinCategory = CopyTypeValue.PinValueType.TerminalCategory;

			TArray<FString> Values;
			SplitIgnoringParentheses(Token, Values);

			if (Values.Num() == 2)
			{
				Content += GenerateManyLinesDefaultConstructor(Name, CopyTypeKey, Values[0].TrimStartAndEnd(), Class, IsConstructorFunction, EntryNode, EntryNodes, true, LeftAllAssetRefInBlueprint, ElemVarKey);
				Content += GenerateManyLinesDefaultConstructor(Name, CopyTypeValue, Values[1].TrimStartAndEnd(), Class, IsConstructorFunction, EntryNode, EntryNodes, true, LeftAllAssetRefInBlueprint, ElemVarValue);
				
				Content += FString::Format(TEXT("{0}.Add({1}{2});\n"), { ContainerName, ElemVarKey, ElemVarValue });
			}
		}
		return Content;
	}

	return "";
}

FString UBlueprintNativizationLibrary::GenerateOneLineDefaultConstructor(FString Name, FEdGraphPinType Type, FString DefaultValue, UObject* DefaultObject, bool LeftAllAssetRefInBlueprint)
{
	FString Text = DefaultValue;
	auto TrimFirstAndLast = [](const FString& Str) -> FString
		{
			return (Str.Len() > 1) ? Str.Mid(1, Str.Len() - 2) : FString();
		};


	auto RemoveBrackets = [](const FString& Input) -> FString
		{
			if (Input.StartsWith(TEXT("(")) && Input.EndsWith(TEXT(")")) && Input.Len() >= 2)
			{
				return Input.Mid(1, Input.Len() - 2);
			}
			return Input;
		};


	if (Type.IsArray())
	{
		TArray<FString> Tokens;
		Text = TrimFirstAndLast(Text);
		SplitIgnoringParentheses(Text, Tokens);

		FEdGraphPinType LocalType = Type;
		LocalType.ContainerType = EPinContainerType::None;

		TArray<FString> Elements;
		for (const FString& Token : Tokens)
		{
			Elements.Add(GenerateOneLineDefaultConstructor(Name, LocalType, Token, nullptr, LeftAllAssetRefInBlueprint));
		}

		FString Result = TEXT("{") + FString::Join(Elements, TEXT(", ")) + TEXT("}");
		return Result;
	}
	else if (Type.IsSet())
	{
		TArray<FString> Tokens;
		Text = TrimFirstAndLast(Text);
		SplitIgnoringParentheses(Text, Tokens);

		FEdGraphPinType LocalType = Type;
		LocalType.ContainerType = EPinContainerType::None;

		TArray<FString> Elements;
		for (const FString& Token : Tokens)
		{
			Elements.Add(GenerateOneLineDefaultConstructor(Name, LocalType, Token, nullptr, LeftAllAssetRefInBlueprint));
		}

		FString Result = TEXT("{") + FString::Join(Elements, TEXT(", ")) + TEXT("}");
		return Result;
	}
	else if (Type.IsMap())
	{
		TArray<FString> Pairs;
		FString WorkingText = Text;
		Text = TrimFirstAndLast(Text);
		SplitIgnoringParentheses(Text, Pairs);


		while (!WorkingText.IsEmpty())
		{
			int32 OpenIdx, CloseIdx;
			if (WorkingText.FindChar('(', OpenIdx) && WorkingText.FindChar(')', CloseIdx) && CloseIdx > OpenIdx)
			{
				FString PairStr = WorkingText.Mid(OpenIdx + 1, CloseIdx - OpenIdx - 1);
				TArray<FString> KeyValue;
				PairStr.ParseIntoArray(KeyValue, TEXT(","), true);

				WorkingText = WorkingText.Mid(CloseIdx + 1);
				WorkingText.TrimStartInline();
				if (WorkingText.StartsWith(TEXT(",")))
				{
					WorkingText.RemoveAt(0);
					WorkingText.TrimStartInline();
				}
			}
			else
			{
				break;
			}
		}
		FString Result = TEXT("{") + FString::Join(Pairs, TEXT(", ")) + TEXT("}");
		return Result;
	}
    else if (Type.PinCategory == UEdGraphSchema_K2::PC_Int64)
    {
        if (Text.IsEmpty())
        {
            return TEXT("0LL");
        }
        if (Text == TEXT("-9223372036854775808"))
        {
            return TEXT("(-9223372036854775807LL - 1)");
        }
        return Text + TEXT("LL");
    }
	else if (Type.PinCategory == UEdGraphSchema_K2::PC_Int)
	{
		return Text.IsEmpty() ? "0" : Text;
	}
	else if (Type.PinCategory == UEdGraphSchema_K2::PC_Double || Type.PinCategory == UEdGraphSchema_K2::PC_Real || Type.PinCategory == UEdGraphSchema_K2::PC_Float)
	{
		FString Literal = TEXT("0.0");
        if (!Text.IsEmpty())
        {
            Literal = UBlueprintNativizationLibrary::TrimFloatToMeaningfulString(Text);
        }
        if (Type.PinCategory == UEdGraphSchema_K2::PC_Float || Type.PinSubCategory == UEdGraphSchema_K2::PC_Float)
        {
            if (!Literal.Contains(TEXT(".")) && !Literal.Contains(TEXT("e"), ESearchCase::IgnoreCase))
            {
                Literal += TEXT(".0");
            }
            Literal += TEXT("f");
        }
        return Literal;
	}
	else if (Type.PinCategory == UEdGraphSchema_K2::PC_Boolean)
	{
		return Text.IsEmpty() ? "false" : (Text == "True" || Text == "true" ? "true" : "false");
	}
	else if (Type.PinCategory == UEdGraphSchema_K2::PC_String)
	{
		return FString::Format(TEXT("TEXT(\"{0}\")"), { Text.ReplaceCharWithEscapedChar() });
	}
	else if (Type.PinCategory == UEdGraphSchema_K2::PC_Name)
	{
		return FString::Format(TEXT("FName(TEXT(\"{0}\"))"), { Text.ReplaceCharWithEscapedChar() });
	}
	else if (Type.PinCategory == UEdGraphSchema_K2::PC_Text)
	{
		FText TextValue;
		FTextStringHelper::ReadFromBuffer(*Text, TextValue);
		return GenerateTextLiteral(TextValue, FString(), Name);
	}
    else if (Type.PinCategory == UEdGraphSchema_K2::PC_Interface)
    {
        return GetPinType(Type, false) + TEXT("{}");
    }
    else if (Type.PinCategory == UEdGraphSchema_K2::PC_SoftObject || Type.PinCategory == UEdGraphSchema_K2::PC_SoftClass)
    {
        FString AssetPath = DefaultValue;
        if (DefaultObject)
        {
            AssetPath = DefaultObject->GetPathName();
        }
        AssetPath = FPackageName::ExportTextPathToObjectPath(AssetPath.TrimQuotes());
        if (Type.PinCategory == UEdGraphSchema_K2::PC_SoftClass)
        {
            UClass* OriginalClass = FindObject<UClass>(nullptr, *AssetPath);
            if (LeftAllAssetRefInBlueprint && !AssetPath.IsEmpty() && AssetPath != TEXT("None"))
            {
                if (!OriginalClass)
                {
                    OriginalClass = LoadObject<UClass>(nullptr, *AssetPath);
                }
                if (!OriginalClass)
                {
                    FNativeApiCatalog::AddError(TEXT("Cannot resolve the required Blueprint class literal: ") + AssetPath);
                    return GetPinType(Type, false) + TEXT("{}");
                }
                return GetPinType(Type, false) + TEXT("(") + FRetainedBlueprintGeneration::GetClassValue(OriginalClass) + TEXT(")");
            }
            UClass* ParentClass = FNativeGenerationContext::ResolveClass(OriginalClass);
            if (OriginalClass != ParentClass)
            {
                return GetPinType(Type, false) + TEXT("(") + FRetainedBlueprintGeneration::GetClassValue(ParentClass) + TEXT(")");
            }
        }
        if (AssetPath.IsEmpty() || AssetPath == TEXT("None") || LeftAllAssetRefInBlueprint)
        {
            return GetPinType(Type, false) + TEXT("{}");
        }
        return FString::Format(TEXT("{0}(FSoftObjectPath(TEXT(\"{1}\")))"),
            { GetPinType(Type, false), AssetPath.ReplaceCharWithEscapedChar() });
    }
	else if (Type.PinCategory == UEdGraphSchema_K2::PC_Object)
	{
		if (!DefaultObject && !DefaultValue.IsEmpty() && DefaultValue != TEXT("None"))
		{
			DefaultObject = LoadObject<UObject>(nullptr, *DefaultValue);
		}
        if (UEnum* Enumeration = Cast<UEnum>(DefaultObject))
        {
            return FString::Format(TEXT("StaticEnum<{0}>()"), { GetUniqueFieldName(Enumeration) });
        }
		if (DefaultObject && DefaultObject->IsAsset())
		{
			if (LeftAllAssetRefInBlueprint)
			{
				return FString(TEXT("nullptr"));
			}
			else
			{
				FString AssetPath = FSoftObjectPath(DefaultObject).ToString();
				RegisterAssetClassInclude(DefaultObject->GetClass());
				return FString::Format(TEXT("LoadObject<{0}>(nullptr, TEXT(\"{1}\"))"),
					TArray<FStringFormatArg>{ FStringFormatArg(FRetainedBlueprintGeneration::GetClassName(DefaultObject->GetClass())), FStringFormatArg(AssetPath) });
			}
		}

		if (Type.PinSubCategory == "self" || Name == "self")
		{
			return FString(TEXT(""));
		}
		else
		{
			return Text.IsEmpty() ? FString(TEXT("nullptr")) : Text;
		}
	}
	else if (Type.PinCategory == UEdGraphSchema_K2::PC_Class)
	{
		if (!DefaultObject && !DefaultValue.IsEmpty() && DefaultValue != TEXT("None"))
		{
            const FString ClassPath = FPackageName::ExportTextPathToObjectPath(DefaultValue.TrimQuotes());
            DefaultObject = LoadObject<UClass>(nullptr, *ClassPath);
            if (!DefaultObject)
            {
                FNativeApiCatalog::AddError(TEXT("Cannot resolve the required Blueprint class literal: ") + ClassPath);
            }
		}

        if (UClass* ClassValue = Cast<UClass>(DefaultObject))
        {
            return FRetainedBlueprintGeneration::GetClassValue(ClassValue);
        }

		return Text.IsEmpty() ? FString(TEXT("nullptr")) : Text;
	}


    else if (Type.PinCategory == UEdGraphSchema_K2::PC_Enum || Type.PinCategory == UEdGraphSchema_K2::PC_Byte)
    {
        if (UEnum* Enumeration = Cast<UEnum>(Type.PinSubCategoryObject.Get()))
        {
            int64 Value = 0;
            if (!Text.IsEmpty())
            {
                Value = Enumeration->GetValueByNameString(Text);
                if (Value == INDEX_NONE && Text.IsNumeric())
                {
                    Value = FCString::Atoi64(*Text);
                }
            }
            return GenerateEnumLiteral(Enumeration, Value);
        }
        if (Text.IsEmpty())
        {
            return TEXT("0");
        }
        return Text;
    }

	else if (Type.PinCategory == UEdGraphSchema_K2::PC_Struct)
	{
        UScriptStruct* SelectedStructure = Cast<UScriptStruct>(Type.PinSubCategoryObject.Get());
        if (UBlueprintNativizationSettingsLibrary::FindStructTranslator(SelectedStructure))
        {
            FStructTranslationRequest Request;
            Request.Structure = SelectedStructure;
            Request.SerializedValue = DefaultValue;
            Request.bKeepAssetReferencesInBlueprint = LeftAllAssetRefInBlueprint;
            Request.Operation = EStructTranslationOperation::SerializedValue;
            return UBlueprintNativizationSettingsLibrary::TranslateStructure(Request);
        }
        FConstructorDescriptor Constructor;
        if (SelectedStructure && SelectedStructure->IsNative() &&
            !UBlueprintNativizationSettingsLibrary::FindConstructorDescriptorByStruct(SelectedStructure, Constructor))
        {
            bool bHasClosedMembers = false;
            for (TFieldIterator<FProperty> Member(SelectedStructure); Member; ++Member)
            {
                if (Member->HasAnyPropertyFlags(CPF_NativeAccessSpecifierPrivate | CPF_NativeAccessSpecifierProtected))
                {
                    bHasClosedMembers = true;
                }
            }
            const FString TypeName = GetUniqueFieldName(SelectedStructure);
            if (bHasClosedMembers)
            {
                FStructTranslationRequest Request;
                Request.Structure = SelectedStructure;
                Request.SerializedValue = DefaultValue;
                Request.bKeepAssetReferencesInBlueprint = LeftAllAssetRefInBlueprint;
                Request.Operation = EStructTranslationOperation::SerializedValue;
                return UBlueprintNativizationSettingsLibrary::TranslateStructure(Request);
            }
            FString ValueName;
            FString Initializer = GenerateManyLinesDefaultConstructor(TEXT("StructureValue"), Type, DefaultValue,
                nullptr, false, nullptr, {}, false, LeftAllAssetRefInBlueprint, ValueName);
            return TEXT("([]()\n{\n") + TypeName + TEXT(" StructureValue{};\n") + Initializer + TEXT("return StructureValue;\n}())");
        }
		FString Result = "";
		UScriptStruct* Struct = Cast<UScriptStruct>(Type.PinSubCategoryObject.Get());
		Result += GetUniqueFieldName(Struct);
		Result += TEXT("(");

		TArray<FName> PropertyNames = GetAllPropertyNamesByStruct(Struct);
		TArray<FString> Args;
		int Count = 0;
		DefaultValue = RemoveBrackets(DefaultValue);

		for (FName PropertyName : PropertyNames)
		{
			FProperty* Property = Struct->FindPropertyByName(PropertyName);

			TArray<FString> Parts;
			SplitIgnoringParentheses(DefaultValue, Parts);
			FString PropertyValue = "";
			for (FString Part : Parts)
			{
				if (Part.Contains(Property->GetName()))
				{
					PropertyValue = ExtractNestedParam(Part, Property->GetName());
					break;
				}
			}

			if (PropertyValue.IsEmpty())
			{
				PropertyValue = GetStructPart(DefaultValue, Count);
			}

			FEdGraphPinType OutPinType;
			ConvertPropertyToPinType(Property, OutPinType);

			if (!PropertyValue.IsEmpty())
			{
                FString Argument = GenerateOneLineDefaultConstructor(PropertyName.ToString(), OutPinType, PropertyValue, nullptr, LeftAllAssetRefInBlueprint);
                if (Property->IsA<FArrayProperty>() || Property->IsA<FSetProperty>() || Property->IsA<FMapProperty>())
                {
                    Argument = GetPropertyType(Property) + Argument;
                }
                Args.Add(Argument);
			}
			Count++;
		}

		Result += FString::Join(Args, TEXT(","));
		Result += TEXT(")");
		return Result;
	}
	return "";
}

FString UBlueprintNativizationLibrary::GetStableFieldIdentifier(const UField* Field)
{
	if (!Field)
		return TEXT("");

	const UPackage* Package = Field->GetOutermost();
	FString PackageName = Package ? Package->GetName() : TEXT("UnknownPackage");

	FString FieldName = Field->GetName();

	return FString::Format(TEXT("{0}.{1}"), { *PackageName, *FieldName });
}

UField* UBlueprintNativizationLibrary::FindFieldByStableIdentifier(const FString& Identifier)
{
	if (Identifier.IsEmpty())
	{
		return nullptr;
	}

	// Ожидается формат: PackageName.FieldName
	FString PackageName, FieldName;
	if (!Identifier.Split(TEXT("."), &PackageName, &FieldName, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
	{
		return nullptr; // Неверный формат
	}

	// Попытка загрузить пакет
	UPackage* Package = FindPackage(nullptr, *PackageName);
	if (!Package)
	{
		Package = LoadPackage(nullptr, *PackageName, LOAD_None);
		if (!Package)
		{
			return nullptr;
		}
	}

	for (FThreadSafeObjectIterator It; It; ++It)
	{
		if (It->IsIn(Package) && It->GetName() == FieldName)
		{
			if (UField* AsField = Cast<UField>(*It))
			{
				return AsField;
			}
		}
	}

	return nullptr;
}

void ReplaceFieldIfMatch(UField*& Target, UField* OldType, UField* NewType)
{
	if (Target == OldType)
	{
		Target = NewType;
	}
}

void UBlueprintNativizationLibrary::ReplaceReferencesToTypeObjectTest(UObject* OldType, UObject* NewType)
{
	TArray<UBlueprint*> Blueprints;
	ReplaceReferencesToType(Cast<UField>(OldType), Cast<UField>(NewType), Blueprints);
}

void UBlueprintNativizationLibrary::ReplaceReferencesToType(UField* OldType, UField* NewType, TArray<UBlueprint*>& Blueprints, bool bDeferReconstruction, bool bReplaceParentClass)
{
	if (!OldType || !NewType || OldType == NewType)
	{
		UE_LOG(LogTemp, Warning, TEXT("Invalid input types."));
		return;
	}

	FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

	FARFilter Filter;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	Filter.bRecursivePaths = true;

	TArray<FString> AllPaths;
	AssetRegistry.Get().GetAllCachedPaths(AllPaths);

	const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	const FString ProjectPluginsDir = FPaths::Combine(ProjectDir, TEXT("Plugins"));

	TArray<FString> ProjectPluginNames;
	TArray<FString> PluginFolders;
	IFileManager& FileManager = IFileManager::Get();

	FileManager.FindFilesRecursive(PluginFolders, *ProjectPluginsDir, TEXT("*.uplugin"), true, false, true);

	for (const FString& PluginFilePath : PluginFolders)
	{
		FString PluginFolder = FPaths::GetPath(PluginFilePath);
		FString PluginName = FPaths::GetBaseFilename(PluginFilePath); 
		ProjectPluginNames.Add(PluginName);
	}

	for (const FString& Path : AllPaths)
	{
		if (Path.StartsWith("/Game"))
		{
			Filter.PackagePaths.Add(*Path);
		}
		else
		{
			for (const FString& PluginName : ProjectPluginNames)
			{
				const FString ExpectedMountPoint = "/" + PluginName;
				if (Path.StartsWith(ExpectedMountPoint))
				{
					Filter.PackagePaths.Add(*Path);
					break;
				}
			}
		}
	}

    Filter.PackageNames.Add(OldType->GetOutermost()->GetFName());
    for (int32 PackageIndex = 0; PackageIndex < Filter.PackageNames.Num(); ++PackageIndex)
    {
        TArray<FName> ReferencingPackages;
        AssetRegistry.Get().GetReferencers(Filter.PackageNames[PackageIndex], ReferencingPackages);
        for (FName ReferencingPackage : ReferencingPackages)
        {
            Filter.PackageNames.AddUnique(ReferencingPackage);
        }
    }
	TArray<FAssetData> AssetList;
	AssetRegistry.Get().GetAssets(Filter, AssetList);
    // Unsaved Blueprint references are not present in the on-disk dependency graph.
    for (TObjectIterator<UBlueprint> BlueprintIterator; BlueprintIterator; ++BlueprintIterator)
    {
        if (BlueprintIterator->IsAsset())
        {
            AssetList.AddUnique(FAssetData(*BlueprintIterator));
        }
    }


	for (const FAssetData& Asset : AssetList)
	{
        const FString PackageName = Asset.PackageName.ToString();
        bool bProjectPackage = PackageName.StartsWith(TEXT("/Game/"));
        for (const FString& PluginName : ProjectPluginNames)
        {
            bProjectPackage |= PackageName.StartsWith(TEXT("/") + PluginName + TEXT("/"));
        }
        if (!bProjectPackage)
        {
            continue;
        }
		UBlueprint* Blueprint = Cast<UBlueprint>(Asset.GetAsset());
		if (!Blueprint) continue;

		bool bModified = false;

		// === Parent Class ===
		if (bReplaceParentClass && Blueprint->ParentClass == OldType)
		{
			if (bDeferReconstruction)
            {
                Blueprint->ParentClass = Cast<UClass>(NewType);
            }
            else
            {
                UBlueprintEditorLibrary::ReparentBlueprint(Blueprint, Cast<UClass>(NewType));
            }
            bModified = true;
			UEditorAssetSubsystem* EditorAssetSubsystem = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
			const UBlueprintNativizationV2EditorSettings* Settings = GetDefault<UBlueprintNativizationV2EditorSettings>();

			if (!bDeferReconstruction && Settings->bAddBPPrefixToReparentBlueprint)
			{
				FString OldName = Asset.AssetName.ToString();
				if (!OldName.StartsWith(TEXT("BP_")))
				{
					FString NewName = TEXT("BP_") + OldName;
					FString DestPath = FPaths::GetPath(Asset.GetSoftObjectPath().ToString()) + TEXT("/") + NewName;

					EditorAssetSubsystem->RenameAsset(Asset.GetSoftObjectPath().ToString(), DestPath);
				}
			}
		}

		for (FBPVariableDescription& VarDesc : Blueprint->NewVariables)
		{
			if (VarDesc.VarType.PinValueType.TerminalSubCategoryObject == OldType)
                    {
                        VarDesc.VarType.PinValueType.TerminalSubCategoryObject = NewType;
                        bModified = true;
                    }
                    if (VarDesc.VarType.PinSubCategoryObject == OldType)
			{
				VarDesc.VarType.PinSubCategoryObject = NewType;
				bModified = true;
			}
		}

		// === Graph nodes ===
		TArray<UEdGraph*> Graphs;
		FGenerationGraphContext::GetGraphs(Blueprint, Graphs);

		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph) continue;

			if (UK2Node_FunctionEntry* FunctionEntry = Cast<UK2Node_FunctionEntry>(FBlueprintEditorUtils::GetEntryNode(Graph)))
			{
				TArray<FBPVariableDescription>& LocalVars = FunctionEntry->LocalVariables;
				for (FBPVariableDescription& BPVariableDescription : LocalVars)
				{
					if (BPVariableDescription.VarType.PinValueType.TerminalSubCategoryObject == OldType)
                    {
                        BPVariableDescription.VarType.PinValueType.TerminalSubCategoryObject = NewType;
                        bModified = true;
                    }
                    if (BPVariableDescription.VarType.PinSubCategoryObject == OldType)
					{
						BPVariableDescription.VarType.PinSubCategoryObject = NewType;
						bModified = true;
					}
				}
			}

			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node) continue;

				Node->Modify(false);
				bool bNodeModified = false;

				if (UK2Node_EditablePinBase* EditablePinBaseNode = Cast<UK2Node_EditablePinBase>(Node))
				{
					for (TSharedPtr<FUserPinInfo> UserPinInfo : EditablePinBaseNode->UserDefinedPins)
					{
						if (UserPinInfo->PinType.PinValueType.TerminalSubCategoryObject == OldType)
                    {
                        UserPinInfo->PinType.PinValueType.TerminalSubCategoryObject = NewType;
                        bModified = true;
                    }
                    if (UserPinInfo->PinType.PinSubCategoryObject == OldType)
						{
							UserPinInfo->PinType.PinSubCategoryObject = NewType;
						}
					}
				}

				for (UEdGraphPin* GraphPin : Node->Pins)
				{
					if (GraphPin->PinType.PinValueType.TerminalSubCategoryObject == OldType)
                    {
                        GraphPin->PinType.PinValueType.TerminalSubCategoryObject = NewType;
                        bModified = true;
                    }
                    if (GraphPin->PinType.PinSubCategoryObject == OldType)
					{
						GraphPin->Modify();
						GraphPin->PinType.PinSubCategoryObject = NewType;
						bModified = true;
						bNodeModified = true;
						if (!bDeferReconstruction)
                        {
                            Node->PinTypeChanged(GraphPin);
                        }
					}
				}

				if (bNodeModified && !bDeferReconstruction)
				{
					Node->ReconstructNode();
				}
			}

		}

		if (bModified)
		{
			Blueprints.AddUnique(Blueprint);
			UE_LOG(LogTemp, Log, TEXT("Updated blueprint: %s"), *Blueprint->GetName());
		}
	}
}


UBlueprint* UBlueprintNativizationLibrary::GetBlueprintFromAsset(UObject* Asset)
{
	if (!Asset)
	{
		return nullptr;
	}

	if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
	{
		return Blueprint;
	}

	if (UClass* AssetClass = Cast<UClass>(Asset))
	{
		return Cast<UBlueprint>(AssetClass->ClassGeneratedBy);
	}

	if (UClass* Class = Asset->GetClass())
	{
		return Cast<UBlueprint>(Class->ClassGeneratedBy);
	}

	return nullptr;
}

void UBlueprintNativizationLibrary::ClearBlueprint(UBlueprint* Blueprint)
{
	if (!Blueprint)
	{
		return;
	}

    const bool bPreviousSuppression = Blueprint->bSuppressStructurallyModified;
    Blueprint->bSuppressStructurallyModified = true;
    ON_SCOPE_EXIT
    {
        Blueprint->bSuppressStructurallyModified = bPreviousSuppression;
    };
	Blueprint->Modify();

	for (int32 i = Blueprint->NewVariables.Num() - 1; i >= 0; --i)
	{
		const FName VarName = Blueprint->NewVariables[i].VarName;
		FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, VarName);
	}

	TArray<UEdGraph*> AllGraphs;
	FGenerationGraphContext::GetGraphs(Blueprint, AllGraphs);

	UEdGraph* ConstructionScript = FBlueprintEditorUtils::FindUserConstructionScript(Blueprint);
	AllGraphs.Remove(ConstructionScript);

	for (UEdGraph* Graph : AllGraphs)
	{
		FBlueprintEditorUtils::RemoveGraph(Blueprint, Graph, EGraphRemoveFlags::MarkTransient);
	}
	TArray<struct FBPInterfaceDescription> ImplementedInterfaces = Blueprint->ImplementedInterfaces;
	for (FBPInterfaceDescription& InterfaceDescription : ImplementedInterfaces)
	{
		if (InterfaceDescription.Interface)
		{
			const UClass* InterfaceClass = InterfaceDescription.Interface.Get();
			FTopLevelAssetPath InterfacePath(InterfaceClass->GetClassPathName());

			FBlueprintEditorUtils::RemoveInterface(Blueprint, InterfacePath);
		}
	}

	TArray<TObjectPtr<class UTimelineTemplate>> Timelines = Blueprint->Timelines;
	for (UTimelineTemplate* TimelineTemplate : Timelines)
	{
		FBlueprintEditorUtils::RemoveTimeline(Blueprint, TimelineTemplate);
	}

	if (ConstructionScript)
	{
		TArray<UEdGraphNode*> NodesToRemove;
		for (UEdGraphNode* Node : ConstructionScript->Nodes)
		{
			if (!Node->IsA<UK2Node_FunctionEntry>())
			{
				NodesToRemove.Add(Node);
			}
		}

		for (UEdGraphNode* Node : NodesToRemove)
		{
			ConstructionScript->RemoveNode(Node);
		}
	}
	Blueprint->SimpleConstructionScript = nullptr;
	/*
	if (Blueprint->SimpleConstructionScript)
	{
		Blueprint->SimpleConstructionScript->Modify();

		TArray<USCS_Node*> NodesToRemove;
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (!Node->bIsParentComponentNative)
			{
				NodesToRemove.Add(Node);
			}
		}

		for (USCS_Node* Node : NodesToRemove)
		{
			Blueprint->SimpleConstructionScript->RemoveNode(Node);
		}
	}

	if (Blueprint->SimpleConstructionScript)
	{
		Blueprint->SimpleConstructionScript->Modify();
	}

	Blueprint->SimpleConstructionScript->ClearEditorComponentReferences();
	*/
	Blueprint->ClearGarbage();
	Blueprint->MarkPackageDirty();
}


FString UBlueprintNativizationLibrary::ExtractNestedParam(const FString& Input, const FString& ParamName)
{
	int32 StartIndex = Input.Find(ParamName + TEXT("="), ESearchCase::IgnoreCase);
	if (StartIndex == INDEX_NONE)
		return TEXT("");

	// Найти индекс первой скобки после =
	int32 EqualIndex = Input.Find(TEXT("="), ESearchCase::IgnoreCase, ESearchDir::FromStart, StartIndex);
	if (EqualIndex == INDEX_NONE || EqualIndex + 1 >= Input.Len())
		return TEXT("");

	if (Input[EqualIndex + 1] != '(')
	{
		// Не вложенная структура — просто значение
		int32 EndIndex = Input.Find(TEXT(")"), ESearchCase::IgnoreCase, ESearchDir::FromStart, EqualIndex);
		if (EndIndex == INDEX_NONE)
			return Input.Mid(EqualIndex + 1).TrimStartAndEnd();
		else
			return Input.Mid(EqualIndex + 1, EndIndex - EqualIndex - 1).TrimStartAndEnd();
	}

	// Найдена вложенная структура — ищем соответствующую закрывающую скобку
	int32 OpenParens = 0;
	int32 EndIndex = -1;

	for (int32 i = EqualIndex + 1; i < Input.Len(); ++i)
	{
		if (Input[i] == '(')
		{
			++OpenParens;
		}
		else if (Input[i] == ')')
		{
			--OpenParens;
			if (OpenParens == 0)
			{
				EndIndex = i;
				break;
			}
		}
	}

	if (EndIndex != -1)
	{
		return Input.Mid(EqualIndex + 1, EndIndex - EqualIndex).TrimStartAndEnd(); // включая скобки
	}

	return TEXT(""); 
}

FString UBlueprintNativizationLibrary::GetStructPart(const FString& In, int32 Index)
{
	if (In.Contains(TEXT("|")))
	{
		TArray<FString> TopParts;
		In.ParseIntoArray(TopParts, TEXT("|"), false);

		if (!TopParts.IsValidIndex(Index))
		{
			return FString();
		}

		return TopParts[Index];
	}
	else
	{
		TArray<FString> LowParts;
		In.ParseIntoArray(LowParts, TEXT(","), false);

		if (!LowParts.IsValidIndex(Index))
		{
			return FString();
		}

		return LowParts[Index];
	}
}


TMap<UField*, FName> UBlueprintNativizationLibrary::GetAllRegisterUniqueFieldsInNameSubsistem()
{
	TMap<UField*, FName> Result;
	UBlueprintNativizationUniqueNameSubsystem* Subsystem = GEngine->GetEngineSubsystem<UBlueprintNativizationUniqueNameSubsystem>();
	if (Subsystem)
	{
		for (FRegisterNameField RegisterNameField : Subsystem->UniqueFieldNameArray)
		{
            if (!FRetainedBlueprintGeneration::IsRetained(Cast<UClass>(RegisterNameField.Field)))
            {
                Result.Add(RegisterNameField.Field, RegisterNameField.Name);
            }
		}
		return Result;
	}

	return TMap<UField*, FName>();
}

FString UBlueprintNativizationLibrary::GenerateLambdaConstructorUniqueVariableName(UClass* Class, FString MainName)
{
	UBlueprintNativizationUniqueNameSubsystem* Subsystem = GEngine->GetEngineSubsystem<UBlueprintNativizationUniqueNameSubsystem>();
	if (Subsystem)
	{
		return Subsystem->GenerateConstructorLambdaUniqueVariableName(MainName, Class).ToString();
	}
	return FString();
}


FString UBlueprintNativizationLibrary::GetLambdaUniqueVariableNameByNode(FString MainName, UK2Node* LambdaNode, TArray<FGenerateFunctionStruct> EntryNodes)
{
	UBlueprintNativizationUniqueNameSubsystem* Subsystem = GEngine->GetEngineSubsystem<UBlueprintNativizationUniqueNameSubsystem>();
	if (Subsystem)
	{
		return Subsystem->GetLambdaUniqueVariableName(MainName, LambdaNode, EntryNodes).ToString();
	}
	return FString();
}

FString UBlueprintNativizationLibrary::GetLambdaUniqueVariableNameByFunction(FString MainName, UFunction* Function, TArray<FGenerateFunctionStruct> EntryNodes)
{
	UBlueprintNativizationUniqueNameSubsystem* Subsystem = GEngine->GetEngineSubsystem<UBlueprintNativizationUniqueNameSubsystem>();
	if (Subsystem)
	{
		return Subsystem->GetLambdaUniqueVariableName(MainName, Function , EntryNodes).ToString();
	}
	return FString();
}

FString UBlueprintNativizationLibrary::GetLambdaUniqueVariableNameByClass(FString MainName, UClass* OwnerClass, const FString& SourceIdentity)
{
	UBlueprintNativizationUniqueNameSubsystem* Subsystem = GEngine->GetEngineSubsystem<UBlueprintNativizationUniqueNameSubsystem>();
	if (Subsystem)
	{
		return Subsystem->GetLambdaUniqueVariableName(MainName, OwnerClass, SourceIdentity).ToString();
	}
	return FString();
}

bool UBlueprintNativizationLibrary::IsNodeInCurrentFunctionNode(UK2Node* TargetNode,
	UK2Node* StartNode, TArray<FGenerateFunctionStruct> EntryNodes)
{
	if (!TargetNode || !StartNode)
	{
		return false;
	}

	TArray<UK2Node*> ExecNodes = UBlueprintNativizationLibrary::GetAllExecuteNodesInFunction(StartNode, EntryNodes, "None");

	TArray<UK2Node*> PureNodes = UBlueprintNativizationLibrary::GetAllInputNodes(ExecNodes, true);

	TArray<UK2Node*> AllNodes;
	AllNodes.Append(ExecNodes);
	AllNodes.Append(PureNodes);

	if (AllNodes.Contains(TargetNode))
	{
		return true;
	}

	return false;
}

bool UBlueprintNativizationLibrary::IsPinAllLinkInCurrentFunctionNode(UEdGraphPin* EdGraphPin,
	UK2Node* EntryNode, TArray<FGenerateFunctionStruct> EntryNodes)
{
	if (!EdGraphPin || EdGraphPin->Direction != EGPD_Output)
	{
		return false;
	}

	for (UEdGraphPin* LinkedPin : EdGraphPin->LinkedTo)
	{
		if (!LinkedPin)
		{
			continue;
		}

		UK2Node* LinkedNode = Cast<UK2Node>(LinkedPin->GetOwningNode());
		if (!LinkedNode)
		{
			continue;
		}

		if (!IsNodeInCurrentFunctionNode(LinkedNode, EntryNode, EntryNodes))
		{
			return false;
		}
	}

	for (UEdGraphPin* SubPin : EdGraphPin->SubPins)
	{
		if (!IsPinAllLinkInCurrentFunctionNode(SubPin, EntryNode, EntryNodes))
		{
			return false;
		}
	}

	return true;
}

UFunction* UBlueprintNativizationLibrary::GetFunctionByNodeAndEntryNodes(UK2Node* Node, TArray<FGenerateFunctionStruct> EntryNodes)
{
	FGenerateFunctionStruct Result;
	if(GetEntryByNodeAndEntryNodes(Node, EntryNodes, Result))
	{
		if (Result.OriginalUFunction)
		{
			return Result.OriginalUFunction;
		}
		return FindFunctionByFunctionName(Node->GetBlueprint(), FName(*GetFunctionNameByEntryNode(Result.Node)));
	}

	return nullptr;
}

UFunction* UBlueprintNativizationLibrary::GetFunctionByNode(UK2Node* Node)
{
    if (Node->IsA<UK2Node_FunctionEntry>() || Node->IsA<UK2Node_Event>())
    {
        return FindFunctionByFunctionName(Node->GetBlueprint(), FName(*GetFunctionNameByEntryNode(Node)));
    }

	if (UK2Node* EntryNode = GetEntryNodeByNode(Node))
	{
		return FindFunctionByFunctionName(Node->GetBlueprint(), FName(*GetFunctionNameByEntryNode(EntryNode)));
	}

	return nullptr;
}

bool UBlueprintNativizationLibrary::GetEntryByNodeAndEntryNodes(UK2Node* Node, TArray<FGenerateFunctionStruct> EntryNodes, FGenerateFunctionStruct& Result)
{
	for (FGenerateFunctionStruct EntryNode : EntryNodes)
	{
		TArray<UK2Node*> AllNodes = GetAllExecuteNodesInFunction(EntryNode.Node, EntryNodes, "None");
		TArray<UK2Node*> InputNodes = GetAllInputNodes(AllNodes, true);

		AllNodes.Append(InputNodes);

		if (AllNodes.Contains(Node))
		{
			Result = EntryNode;
			return true;
		}
	}

	return false;
}

UK2Node* UBlueprintNativizationLibrary::GetEntryNodeByNode(UK2Node* Node)
{
	TArray<UEdGraph*> EdGraphs;
	FGenerationGraphContext::GetGraphs(Node->GetBlueprint(), EdGraphs);
	TArray<UK2Node*> EntryK2Nodes;
	for (UEdGraph* EdGraph : EdGraphs)
	{
		EntryK2Nodes.Append(GetEntryNodesPerGraph(EdGraph, true));
	}

	for (UK2Node* EntryNode : EntryK2Nodes)
	{
		TArray<UK2Node*> AllNodes = GetAllExecuteNodesInFunction(Cast<UK2Node>(EntryNode), TEXT("None"));
		TArray<UK2Node*> InputNodes = GetAllInputNodes(AllNodes, true);

		AllNodes.Append(InputNodes);

		if (AllNodes.Contains(Node))
		{
			return Cast<UK2Node>(EntryNode);
		}
	}
	return nullptr;
}


FString UBlueprintNativizationLibrary::GetUniqueFieldName(UField* Field)
{
	if (!Field)
	{
		return TEXT("InvalidField");
	}
    if (const UScriptStruct* Structure = Cast<UScriptStruct>(Field))
    {
        if (FRetainedBlueprintGeneration::IsExternalStructure(Structure))
        {
            FStructTranslationRequest Request;
            Request.Structure = Structure;
            Request.Operation = EStructTranslationOperation::DescribeType;
            return UBlueprintNativizationSettingsLibrary::TranslateStructure(Request);
        }
    }
	UBlueprintNativizationUniqueNameSubsystem* Subsystem = GEngine->GetEngineSubsystem<UBlueprintNativizationUniqueNameSubsystem>();
	if (Subsystem)
	{
		return Subsystem->GetUniqueFieldName(Field).ToString();
	}

	return FString();
}

FString UBlueprintNativizationLibrary::RemoveBPPrefix(const FString& In)
{
	if (!In.StartsWith(TEXT("BP"), ESearchCase::CaseSensitive))
		return In;
	int32 Pos = 2;
	const int32 Len = In.Len();
	while (Pos < Len && In[Pos] == '_')
		++Pos;
	FString Result = In.Mid(Pos);
	if (Result.IsEmpty())
		return In;
	return Result;
}

FString UBlueprintNativizationLibrary::ConversionFunctionFlagToString(EConversionFunctionFlag Flag)
{
	switch (Flag)
	{
	case EConversionFunctionFlag::ConvertToCPP: return TEXT("ConvertToCPP");
	case EConversionFunctionFlag::ConvertAsNativeFunction: return TEXT("ConvertAsNativeFunction");
	case EConversionFunctionFlag::Ignore: return TEXT("Ignore");
	default: return TEXT("ConvertToCPP");
	}
}

EConversionFunctionFlag UBlueprintNativizationLibrary::ConversionFunctionFlagFromString(const FString& In)
{
	if (In == TEXT("ConvertAsNativeFunction")) return EConversionFunctionFlag::ConvertAsNativeFunction;
	if (In == TEXT("Ignore")) return EConversionFunctionFlag::Ignore;
	return EConversionFunctionFlag::ConvertToCPP;
}

EConversionFunctionFlag UBlueprintNativizationLibrary::GetConversionFunctionFlagForFunction(UK2Node* ContextNode)
{
	if (!ContextNode) return EConversionFunctionFlag::ConvertToCPP;
	FString Val;
	if (UK2Node_FunctionEntry* FunctionEntry = Cast<UK2Node_FunctionEntry>(ContextNode))
	{
		Val = FunctionEntry->MetaData.GetMetaData(ConversionMetaKey);
	}
	else if (UK2Node_CustomEvent* EventEntry = Cast<UK2Node_CustomEvent>(ContextNode))
	{
		Val = EventEntry->GetUserDefinedMetaData().GetMetaData(ConversionMetaKey);
	}
	if (Val.IsEmpty()) return EConversionFunctionFlag::ConvertToCPP;
	return ConversionFunctionFlagFromString(Val);
}

void UBlueprintNativizationLibrary::SetConversionFunctionFlagForFunction(UK2Node* ContextNode, EConversionFunctionFlag Flag)
{
	if (!ContextNode) return;

	if (UK2Node_FunctionEntry* FunctionEntry = Cast<UK2Node_FunctionEntry>(ContextNode))
	{
		FunctionEntry->MetaData.SetMetaData(ConversionMetaKey, ConversionFunctionFlagToString(Flag));
	}
	else if (UK2Node_CustomEvent* EventEntry = Cast<UK2Node_CustomEvent>(ContextNode))
	{
		EventEntry->GetUserDefinedMetaData().SetMetaData(ConversionMetaKey, ConversionFunctionFlagToString(Flag));
	}

#if WITH_EDITOR
	UPackage* Package = ContextNode->GetBlueprint()->GetPackage();
	if (Package)
	{
		Package->SetDirtyFlag(true);   
		Package->MarkPackageDirty();   
	}
#endif
}

FString UBlueprintNativizationLibrary::ConversionPropertyFlagToString(EConversionPropertyFlag Flag)
{
	switch (Flag)
	{
	case EConversionPropertyFlag::ConvertToCPP: return TEXT("ConvertToCPP");
	case EConversionPropertyFlag::Ignore: return TEXT("Ignore");
	default: return TEXT("ConvertToCPP");
	}
}

FBPVariableDescription* UBlueprintNativizationLibrary::GetVariableDescriptionByPropertyAndBlueprint(
	FProperty* Property,
	UBlueprint* Blueprint
)
{
	if (!Property || !Blueprint)
	{
		return nullptr;
	}

	const FName PropName = Property->GetFName();
	const FString PropNameString = Property->GetName();

	for (FBPVariableDescription& VarDesc : Blueprint->NewVariables)
	{
		if (VarDesc.VarName == PropName)
		{
			return &VarDesc;
		}
	}

	for (FBPVariableDescription& VarDesc : Blueprint->NewVariables)
	{
		if (VarDesc.VarName.ToString() == PropNameString)
		{
			return &VarDesc; 
		}
	}

	return nullptr;
}

UBlueprint* UBlueprintNativizationLibrary::GetBlueprintByProperty(FProperty* Property)
{
	UClass* OwnerClass = Property->GetOwnerClass();
	if (OwnerClass)
	{
		if (OwnerClass && OwnerClass->GetName().StartsWith(TEXT("SKEL_")))
		{
			if (UBlueprint* BP = Cast<UBlueprint>(OwnerClass->ClassGeneratedBy))
			{
				return BP;
			}
		}
	}
	return nullptr;
}

EConversionPropertyFlag UBlueprintNativizationLibrary::ConversionPropertyFlagFromString(const FString& In)
{
	if (In == TEXT("Ignore")) return EConversionPropertyFlag::Ignore;
	return EConversionPropertyFlag::ConvertToCPP;
}

EConversionPropertyFlag UBlueprintNativizationLibrary::GetConversionPropertyFlagForProperty(FProperty* Property, UBlueprint* Blueprint)
{
	if (!Property) return EConversionPropertyFlag::ConvertToCPP;
	if (!Blueprint) return EConversionPropertyFlag::ConvertToCPP;

	
	if (FBPVariableDescription* OutVariableDescription = UBlueprintNativizationLibrary::GetVariableDescriptionByPropertyAndBlueprint(Property, Blueprint))
	{
		if (!OutVariableDescription->HasMetaData(TEXT("ConversionPropertyFlag")))
		{
			return EConversionPropertyFlag::ConvertToCPP;
		}
		FString Val = OutVariableDescription->GetMetaData(TEXT("ConversionPropertyFlag"));
		if (Val.IsEmpty()) return EConversionPropertyFlag::ConvertToCPP;
		return ConversionPropertyFlagFromString(Val);
	}

	return EConversionPropertyFlag::ConvertToCPP;
}

void UBlueprintNativizationLibrary::SetConversionPropertyFlagForProperty(FProperty* Property, UBlueprint* Blueprint, EConversionPropertyFlag Flag)
{
	if (!Property) return;
	if (!Blueprint) return;
	if (FBPVariableDescription* OutVariableDescription = UBlueprintNativizationLibrary::GetVariableDescriptionByPropertyAndBlueprint(Property, Blueprint))
	{
		OutVariableDescription->SetMetaData(TEXT("ConversionPropertyFlag"), *ConversionPropertyFlagToString(Flag));
	}

	#if WITH_EDITOR
		UPackage* Package = Property->GetOutermost();
		if (Package)
		{
			Package->SetDirtyFlag(true);
			Package->MarkPackageDirty();
		}
	#endif
}


FString UBlueprintNativizationLibrary::GetUniqueUInterfaceFieldName(TSubclassOf<UInterface> Field)
{
	return "U" + GetUniqueFieldName(Field);
}

FString UBlueprintNativizationLibrary::GetUniqueIInterfaceFieldName(TSubclassOf<UInterface> Field)
{
	return "I" + GetUniqueFieldName(Field);
}

TArray<UEdGraphPin*> UBlueprintNativizationLibrary::GetFilteredPins(
	UK2Node* Node,
	EPinOutputOrInputFilter DirectionFilter,
	EPinExcludeFilter ExcludeFlags,
	EPinIncludeOnlyFilter IncludeOnly
)
{
	TArray<UEdGraphPin*> Result;

	if (!Node)
	{
		return Result;
	}

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin)
		{
			continue;
		}

		if (Pin->bHidden)
		{
			continue;
		}


		// ���������� �� �����������
		const bool bIsInput = Pin->Direction == EGPD_Input;
		const bool bIsOutput = Pin->Direction == EGPD_Output;
		bool bDirectionPass = false;

		switch (DirectionFilter)
		{
		case EPinOutputOrInputFilter::All:
			bDirectionPass = true;
			break;
		case EPinOutputOrInputFilter::Input:
			bDirectionPass = bIsInput;
			break;
		case EPinOutputOrInputFilter::Ouput:
			bDirectionPass = bIsOutput;
			break;
		}

		if (!bDirectionPass)
		{
			continue;
		}

		const bool bIsExec = Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
		const bool bIsDelegate = Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Delegate;
		const bool bIsOutRef =
			Pin->Direction == EGPD_Output &&
			Pin->PinType.bIsReference &&
			!Pin->PinType.bIsConst;

		if (IncludeOnly != EPinIncludeOnlyFilter::None)
		{
			const bool bIsCorrect =
				(IncludeOnly == EPinIncludeOnlyFilter::ExecPin && bIsExec) ||
				(IncludeOnly == EPinIncludeOnlyFilter::DelegatePin && bIsDelegate) ||
				(IncludeOnly == EPinIncludeOnlyFilter::OutRefOnlyPin && bIsOutRef);

			if (!bIsCorrect)
			{
				continue;
			}
		}

		// Exclude ������
		if (((ExcludeFlags & EPinExcludeFilter::ExecPin) != EPinExcludeFilter::None) && bIsExec)
		{
			continue;
		}
		if (((ExcludeFlags & EPinExcludeFilter::DelegatePin) != EPinExcludeFilter::None) && bIsDelegate)
		{
			continue;
		}


		Result.Add(Pin);
	}

	return Result;
}

int32 UBlueprintNativizationLibrary::LevenshteinDistance_FString(FString A, FString B)
{
	A = A.ToLower();
	B = B.ToLower();

	const int32 N = A.Len();
	const int32 M = B.Len();

	if (N == 0) return M;
	if (M == 0) return N;

	TArray<int32> Prev;
	Prev.SetNumZeroed(M + 1);
	for (int32 j = 0; j <= M; ++j)
	{
		Prev[j] = j;
	}

	TArray<int32> Curr;
	Curr.SetNumZeroed(M + 1);

	for (int32 i = 1; i <= N; ++i)
	{
		Curr[0] = i;
		const TCHAR Ai = A[i - 1];
		for (int32 j = 1; j <= M; ++j)
		{
			const TCHAR Bj = B[j - 1];
			const int32 Cost = (Ai == Bj) ? 0 : 1;
			const int32 InsertCost = Curr[j - 1] + 1;
			const int32 DeleteCost = Prev[j] + 1;
			const int32 ReplaceCost = Prev[j - 1] + Cost;
			Curr[j] = FMath::Min3(InsertCost, DeleteCost, ReplaceCost);
		}
		Prev = Curr;
	}

	return Prev[M];
}

FProperty* UBlueprintNativizationLibrary::FindClosestPropertyByName(TArray<FProperty*>& Properties, const FName& TargetName)
{
	if (Properties.Num() == 0)
	{
		return nullptr;
	}

	 FString TargetStr = TargetName.ToString();
	if (TargetStr.IsEmpty())
	{
		return nullptr;
	}

	for ( FProperty* Prop : Properties)
	{
		if (Prop == nullptr) continue;
		 FString PropName = Prop->GetFName().ToString();
		if (PropName.Equals(TargetStr, ESearchCase::IgnoreCase))
		{
			return Prop;
		}
	}

	 FProperty* BestProp = nullptr;
	int32 BestScore = TNumericLimits<int32>::Max();

	for ( FProperty* Prop : Properties)
	{
		if (Prop == nullptr) continue;
		 FString PropName = Prop->GetFName().ToString();

		if (PropName.Contains(TargetStr, ESearchCase::IgnoreCase, ESearchDir::FromStart))
		{
			return Prop;
		}

		 int32 Dist = LevenshteinDistance_FString(PropName, TargetStr);
		if (Dist < BestScore)
		{
			BestScore = Dist;
			BestProp = Prop;
		}
	}

	return BestProp;
}

TArray<FProperty*> UBlueprintNativizationLibrary::GetAllPropertiesByFunction(UFunction* Function)
{
	TArray<FProperty*> Properties;
	if (!Function) return Properties;

	for (TFieldIterator<FProperty> PropIt(Function); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;
		if (Property)
		{
			Properties.Add(Property);
		}
	}

	return Properties;
}

UEdGraphPin* UBlueprintNativizationLibrary::GetPinByName(TArray<UEdGraphPin*> Pins, FString Name)
{
	for (UEdGraphPin* Pin : Pins)
	{
		FString PinName = Pin->GetName();
		if (PinName == Name)
		{
			return Pin;
		}
	}
	return nullptr;
}

TArray<UEdGraphPin*> UBlueprintNativizationLibrary::GetParentPins(TArray<UEdGraphPin*> Pins)
{
	TArray<UEdGraphPin*> ResultPins;
	for (UEdGraphPin* Pin : Pins)
	{
		ResultPins.AddUnique(GetParentPin(Pin));
	}
	return ResultPins;
}

UEdGraphPin* UBlueprintNativizationLibrary::GetFirstNonKnotPin(UEdGraphPin* Pin,
	int Index,
	bool bIgnoreMacro,
	TArray<UK2Node*>& MacroStack,
	UNativizationV2Subsystem* NativizationV2Subsystem)
{
	if (!Pin)
	{
		return nullptr;
	}

    if (UNativizationV2Subsystem* Generator = FGenerationGraphContext::GetGenerator())
    {
        bIgnoreMacro = false;
        if (!NativizationV2Subsystem)
        {
            NativizationV2Subsystem = Generator;
        }
    }
    if (UK2Node* ContextNode = Cast<UK2Node>(Pin->GetOwningNode()))
    {
        const TArray<UK2Node*> ContextStack = FGenerationGraphContext::GetMacroStack(ContextNode);
        if (!ContextStack.IsEmpty())
        {
            MacroStack = ContextStack;
        }
    }
	TSet<UEdGraphPin*> Visited;
	UEdGraphPin* CurrentPin = Pin;

	if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
	{
		if (CurrentPin->Direction == EEdGraphPinDirection::EGPD_Output)
		{
			if (CurrentPin->LinkedTo.Num() > 0)
			{
				CurrentPin = CurrentPin->LinkedTo[0];
			}
			else
			{
				return nullptr;
			}
		}
	}
	else
	{
		if (CurrentPin->Direction == EEdGraphPinDirection::EGPD_Input)
		{
			if (CurrentPin->LinkedTo.IsValidIndex(Index))
			{
				CurrentPin = CurrentPin->LinkedTo[Index];
			}
			else
			{
				return nullptr;
			}
		}
	}

	while (CurrentPin && !Visited.Contains(CurrentPin))
	{
		Visited.Add(CurrentPin);
		UK2Node* Node = Cast<UK2Node>(CurrentPin->GetOwningNode());
		if (!Node)
		{
			return nullptr;
		}

		bool bProcessedMacro = false;

		if (!bIgnoreMacro)
		{
			if (UK2Node_MacroInstance* MacroNode = Cast<UK2Node_MacroInstance>(Node))
			{
				if (NativizationV2Subsystem)
				{
					FGenerateFunctionStruct GenerateFunctionStruct;
					if (UBlueprintNativizationDataLibrary::FindGenerateFunctionStructByNode(NativizationV2Subsystem->EntryNodes, Node, GenerateFunctionStruct))
					{
						return CurrentPin;
					}

					if (NativizationV2Subsystem->FindTranslatorForNode(Node))
					{
						return CurrentPin;
					}
				}

				UEdGraph* MacroGraph = FGenerationGraphContext::GetMacroGraph(MacroNode);
				if (MacroGraph)
				{
					MacroStack.Add(MacroNode);

					TArray<UK2Node_Tunnel*> Tunnels;
					MacroGraph->GetNodesOfClass<UK2Node_Tunnel>(Tunnels);
					for (UK2Node_Tunnel* Tunnel : Tunnels)
					{
						for (UEdGraphPin* TunnelPin : Tunnel->Pins)
						{
							if (TunnelPin->Direction != CurrentPin->Direction &&
								TunnelPin->PinName == CurrentPin->PinName)
							{
								CurrentPin = TunnelPin;
	
								bProcessedMacro = true;
								break;
							}
						}
						if (bProcessedMacro)
							break;
					}
				}
			}
			// Композитный узел (Custom event graph)
			else if (UK2Node_Composite* CompositeNode = Cast<UK2Node_Composite>(Node))
			{
				if (UEdGraph* CompositeGraph = FGenerationGraphContext::GetMacroGraph(CompositeNode))
				{
					MacroStack.Add(CompositeNode);
					TArray<UK2Node_Tunnel*> TunnelNodes;
					CompositeGraph->GetNodesOfClass<UK2Node_Tunnel>(TunnelNodes);
					for (UK2Node_Tunnel* Tunnel : TunnelNodes)
					{
						for (UEdGraphPin* TunnelPin : Tunnel->Pins)
						{
							if (TunnelPin->Direction != CurrentPin->Direction &&
								TunnelPin->PinName == CurrentPin->PinName )
							{
								CurrentPin = TunnelPin;
								bProcessedMacro = true;
								break;
							}
						}
						if (bProcessedMacro)
							break;
					}
				}
			}
			else if (Node->IsA<UK2Node_Tunnel>() && MacroStack.Num() > 0)
			{
				UK2Node* LastMacro = MacroStack.Pop();
				if (LastMacro)
				{
					for (UEdGraphPin* PinCandidate : LastMacro->Pins)
					{
						if (PinCandidate && PinCandidate->PinName == CurrentPin->PinName)
						{
							CurrentPin = PinCandidate;
							bProcessedMacro = true;
							break;
						}
					}
				}
			}
		}

		if (bProcessedMacro)
		{
			if (CurrentPin->LinkedTo.Num() > 0)
			{
				CurrentPin = CurrentPin->LinkedTo[0];
				continue;
			}
			else
			{
                if (CurrentPin->Direction == EGPD_Input && CurrentPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
                {
                    return CurrentPin;
                }
				return nullptr;
			}
		}


		if (!Node->IsA<UK2Node_Knot>())
		{
			return CurrentPin;
		}

		TArray<UEdGraphPin*> OutPins;
		if (CurrentPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
		{
			OutPins = UBlueprintNativizationLibrary::GetFilteredPins(Node, EPinOutputOrInputFilter::Ouput, EPinExcludeFilter::None, EPinIncludeOnlyFilter::ExecPin);
		}
		else
		{
			OutPins = UBlueprintNativizationLibrary::GetFilteredPins(Node, EPinOutputOrInputFilter::Input, EPinExcludeFilter::ExecPin, EPinIncludeOnlyFilter::None);
		}

		if (OutPins.Num() > 0 && OutPins[0]->LinkedTo.Num() > Index)
		{
			CurrentPin = OutPins[0]->LinkedTo[Index];
		}
		else
		{
			return nullptr;
		}
	}

	return nullptr;
}



FString UBlueprintNativizationLibrary::ValidateBlueprintSources(const TArray<UObject*>& InputTargets)
{
    for (UObject* Target : InputTargets)
    {
        const FString ExclusionReason = FEngineAssetPolicy::GetExclusionReason(Target,
            GetDefault<UBlueprintNativizationV2EditorSettings>()->bExcludeEngineAssetsFromConversion);
        if (!ExclusionReason.IsEmpty())
        {
            return ExclusionReason;
        }
    }
    TSet<UBlueprint*> Blueprints;
    TSet<UObject*> ValidatedDependencies;
    for (UObject* Target : InputTargets)
    {
        if (ValidatedDependencies.Contains(Target))
        {
            continue;
        }
        const TSet<UObject*> Dependencies = GetAllDependencies(Target, {}, {});
        ValidatedDependencies.Append(Dependencies);
        for (UObject* Dependency : Dependencies)
        {
            if (UClass* Class = Cast<UClass>(Dependency))
            {
                Dependency = Class->ClassGeneratedBy;
            }
            if (UBlueprint* Blueprint = Cast<UBlueprint>(Dependency))
            {
                Blueprints.Add(Blueprint);
            }
        }
    }
    TArray<FString> Errors;
    for (UBlueprint* Blueprint : Blueprints)
    {
        if (Blueprint->GeneratedClass)
        {
            const AActor* DefaultActor = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject());
            if (DefaultActor && DefaultActor->IsEditorOnly())
            {
                Errors.Add(FString::Format(TEXT("Editor-only actor Blueprint cannot be exported to a runtime module: {0}. Keep this Blueprint or export it to a separate Editor module."), {Blueprint->GetPathName()}));
            }
        }
        for (UClass* ParentClass = Blueprint->ParentClass; ParentClass; ParentClass = ParentClass->GetSuperClass())
        {
            if (!ParentClass->HasAnyClassFlags(CLASS_Native))
            {
                continue;
            }
            FString ModuleDirectory;
            const FString ModuleName = FPackageName::GetShortName(ParentClass->GetOutermost());
            if (FSourceCodeNavigation::FindModulePath(ModuleName, ModuleDirectory))
            {
                FPaths::NormalizeDirectoryName(ModuleDirectory);
                if (ModuleDirectory.Contains(TEXT("/Source/Editor/")))
                {
                    Errors.Add(FString::Format(TEXT("Blueprint cannot be exported to a runtime module because its native parent belongs to an Editor module: {0} -> {1}. Keep this Blueprint or export it to a separate Editor module."),
                        {Blueprint->GetPathName(), ParentClass->GetPathName()}));
                    break;
                }
            }
        }
        if (Blueprint->Status != BS_Error && Blueprint->GeneratedClass)
        {
            continue;
        }
        Errors.Add(TEXT("Blueprint is unavailable for conversion: ") + Blueprint->GetPathName());
        TArray<UEdGraph*> Graphs;
        Blueprint->GetAllGraphs(Graphs);
        for (UEdGraph* Graph : Graphs)
        {
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (Node && Node->bHasCompilerMessage && Node->ErrorType == EMessageSeverity::Error)
                {
                    Errors.AddUnique(Node->GetPathName() + TEXT(": ") + Node->ErrorMsg);
                }
            }
        }
    }
    if (Errors.IsEmpty())
    {
        return FString();
    }
    Errors.Sort();
    return TEXT("Generation is blocked by source Blueprint validation.\n") + FString::Join(Errors, TEXT("\n"));
}

TSet<UObject*> UBlueprintNativizationLibrary::GetAllDependencies(UObject* InputTarget, TArray<TSubclassOf<UObject>> IgnoreClasses, TArray<UObject*> IgnoreObjects)
{
	TSet<UObject*> AllCollectedObjects;
	TSet<UObject*> CurrentStep;
	CurrentStep.Add(InputTarget);

	while (CurrentStep.Num() > 0)
	{
		TSet<UObject*> NextStep;

		for (UObject* Obj : CurrentStep)
		{
			if (!Obj || AllCollectedObjects.Contains(Obj))
			{
				continue;
			}

            if (!FEngineAssetPolicy::GetExclusionReason(Obj,
                GetDefault<UBlueprintNativizationV2EditorSettings>()->bExcludeEngineAssetsFromConversion).IsEmpty())
            {
                continue;
            }
			AllCollectedObjects.Add(Obj);

			TSet<UObject*> FoundDependencies;
			CollectDependencies(Obj, FoundDependencies);

			for (UObject* Dep : FoundDependencies)
			{
				if (AllCollectedObjects.Contains(Dep))
				{
					continue;
				}
                UBlueprint* DependencyBlueprint = Cast<UBlueprint>(Dep);
                const FString EngineExclusionReason = FEngineAssetPolicy::GetExclusionReason(Dep,
                    GetDefault<UBlueprintNativizationV2EditorSettings>()->bExcludeEngineAssetsFromConversion);
                bool bExcludedDependency = IgnoreObjects.Contains(Dep) || !EngineExclusionReason.IsEmpty();
                if (DependencyBlueprint && DependencyBlueprint->GeneratedClass)
                {
                    bExcludedDependency |= HasAnyChildInClasses(DependencyBlueprint->GeneratedClass, IgnoreClasses);
                }
                if (bExcludedDependency)
                {
                    if (DependencyBlueprint && EngineExclusionReason.IsEmpty())
                    {
                        TSet<UObject*> ValueDependencies;
                        CollectDependencies(DependencyBlueprint, ValueDependencies);
                        for (UObject* ValueDependency : ValueDependencies)
                        {
                            if ((ValueDependency->IsA<UUserDefinedStruct>() || ValueDependency->IsA<UUserDefinedEnum>()) &&
                                !IgnoreObjects.Contains(ValueDependency) &&
                                FEngineAssetPolicy::GetExclusionReason(ValueDependency,
                                    GetDefault<UBlueprintNativizationV2EditorSettings>()->bExcludeEngineAssetsFromConversion).IsEmpty())
                            {
                                NextStep.Add(ValueDependency);
                            }
                        }
                    }
                    continue;
                }
				if (!AllCollectedObjects.Contains(Dep) &&
					(Dep->IsA<UEnum>() || Dep->IsA<UStruct>() || Dep->IsA<UBlueprint>()))
				{
					if (Cast<UBlueprint>(Dep))
					{
						if (UClass* Class = Cast<UBlueprint>(Dep)->GeneratedClass)
						{
							if (HasAnyChildInClasses(Class, IgnoreClasses))
							{
								continue;
							}
						}
					}
					NextStep.Add(Dep);
				}
			}
		}

		CurrentStep = MoveTemp(NextStep);
	}

	return AllCollectedObjects;
}

bool UBlueprintNativizationLibrary::HasAnyChildInClasses(UClass* Class, TArray<TSubclassOf<UObject>> Classes)
{
	bool SucsessFoundIgnoreClass = false;
	for (TSubclassOf<UObject> CheckClass : Classes)
	{
		if (Class->IsChildOf(CheckClass) || Class == CheckClass)
		{
			return true;
		}
	}
	return false;
}


bool UBlueprintNativizationLibrary::HasAnyChildInClasses(UClass* Class, TArray<UClass*> Classes)
{
	bool SucsessFoundIgnoreClass = false;
	for (TSubclassOf<UObject> CheckClass : Classes)
	{
		if (Class->IsChildOf(CheckClass) || Class == CheckClass)
		{
			return true;
		}
	}
	return false;
}

bool UBlueprintNativizationLibrary::HasAnyChildInClasses(UClass* Class, TArray<TSoftClassPtr<UObject>> Classes)
{
	bool SucsessFoundIgnoreClass = false;
	for (TSoftClassPtr<UObject> CheckClass : Classes)
	{
		if (Class->IsChildOf(CheckClass.Get()) || Class == CheckClass.Get())
		{
			return true;
		}
	}
	return false;
}


void UBlueprintNativizationLibrary::CollectDependencies(UObject* InputTarget, TSet<UObject*>& OutObjects)
{
	if (!InputTarget)
	{
		return;
	}

    auto AddDependency = [&](UObject* Object)
    {
        if (UClass* Class = Cast<UClass>(Object))
        {
            Object = Class->ClassGeneratedBy;
        }
        if (Object && (Object->IsA<UBlueprint>() || Object->IsA<UUserDefinedStruct>() || Object->IsA<UUserDefinedEnum>()))
        {
            OutObjects.Add(Object);
        }
    };
    UStruct* Structure = Cast<UStruct>(InputTarget);
    if (UBlueprint* Blueprint = Cast<UBlueprint>(InputTarget))
    {
        Structure = Blueprint->GeneratedClass;
        AddDependency(Blueprint->ParentClass);
        for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
        {
            AddDependency(Interface.Interface);
        }
        TArray<UEdGraph*> Graphs;
        FGenerationGraphContext::GetGraphs(Blueprint, Graphs);
        for (UEdGraph* Graph : Graphs)
        {
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (INodeDependingOnEnumInterface* EnumNode = Cast<INodeDependingOnEnumInterface>(Node))
                {
                    AddDependency(EnumNode->GetEnum());
                }
                for (UEdGraphPin* Pin : Node->Pins)
                {
                    AddDependency(Pin->PinType.PinSubCategoryObject.Get());
                    AddDependency(Pin->PinType.PinValueType.TerminalSubCategoryObject.Get());
                    AddDependency(Pin->DefaultObject);
                    if (Pin->DefaultObject)
                    {
                        AddDependency(Pin->DefaultObject->GetClass());
                    }
                }
            }
        }
    }
    if (Structure)
    {
        TArray<FProperty*> Properties;
        for (TFieldIterator<FProperty> Property(Structure); Property; ++Property)
        {
            Properties.Add(*Property);
        }
        for (TFieldIterator<UFunction> Function(Structure); Function; ++Function)
        {
            for (TFieldIterator<FProperty> Parameter(*Function); Parameter && Parameter->HasAnyPropertyFlags(CPF_Parm); ++Parameter)
            {
                Properties.Add(*Parameter);
            }
        }
        for (FProperty* Property : Properties)
        {
            FEdGraphPinType Type;
            ConvertPropertyToPinType(Property, Type);
            AddDependency(Type.PinSubCategoryObject.Get());
            AddDependency(Type.PinValueType.TerminalSubCategoryObject.Get());
        }
    }

	const FName ObjectPath = InputTarget->GetOutermost()->GetFName();
	const FAssetIdentifier AssetId(ObjectPath);

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetIdentifier> Dependencies;
	AssetRegistry.GetDependencies(AssetId, Dependencies);

	for (const FAssetIdentifier& DepId : Dependencies)
	{
        if (DepId.PackageName.IsNone() || DepId.PackageName.ToString().StartsWith(TEXT("/Script/")))
        {
            continue;
        }
        TArray<FAssetData> Assets;
        AssetRegistry.GetAssetsByPackageName(DepId.PackageName, Assets);
        for (const FAssetData& Asset : Assets)
        {
            UClass* AssetClass = Asset.GetClass();
            if (AssetClass && (AssetClass->IsChildOf(UBlueprint::StaticClass()) ||
                AssetClass->IsChildOf(UUserDefinedStruct::StaticClass()) || AssetClass->IsChildOf(UUserDefinedEnum::StaticClass())))
            {
                AddDependency(Asset.GetAsset());
            }
        }
	}
}


TArray<UActorComponent*> UBlueprintNativizationLibrary::GetClassComponentTemplates(TArray<FName>& FoundComponentNames, const TSubclassOf<AActor> ActorClass, const TSubclassOf<UActorComponent> ComponentClass, bool bIgnoreSuperClasses)
{
	/* Use a Map to avoid reinsertion of the same overridden template in a parent */
	TMap<FName, UActorComponent*> ComponentMap = {};

	// Check Classes
	if (IsValid(ActorClass) && IsValid(ComponentClass))
	{
		// Insert Native Components First
		if (const AActor* CDO = ActorClass.GetDefaultObject())
		{
			for (UActorComponent* ComponentItr : CDO->GetComponents())
			{
				if (ComponentItr->GetClass()->IsChildOf(ComponentClass))
				{
					ComponentMap.Emplace(ComponentItr->GetFName(), ComponentItr);
				}
			}
		}

		// Now find Blueprint Components and/or overrides
		UBlueprintGeneratedClass* BPClass = Cast<UBlueprintGeneratedClass>(ActorClass);
		if (BPClass)
		{
			// Move to the Top-Most BP Class, then move downards
			TArray<UBlueprintGeneratedClass*, TInlineAllocator<4>> GeneratedClasses;
			if (bIgnoreSuperClasses)
			{
				GeneratedClasses.Emplace(BPClass);
			}
			else
			{
				while (BPClass)
				{
					GeneratedClasses.Emplace(BPClass);
					BPClass = Cast<UBlueprintGeneratedClass>(BPClass->GetSuperClass());
				}
			}

			// Now go from top-most parent to lowest descendant, checking for overrides on the way
			for (int32 Idx = GeneratedClasses.Num() - 1; Idx >= 0; Idx--)
			{
				// Not all BP's have an SCS (apparently)
				UBlueprintGeneratedClass* CurrentBPClass = GeneratedClasses[Idx];
				if (CurrentBPClass->SimpleConstructionScript)
				{
					// No specified name, just return first component class that matches.
					const TArray<USCS_Node*>& AllSCSNodes = CurrentBPClass->SimpleConstructionScript->GetAllNodes();
					for (const USCS_Node* NodeItr : AllSCSNodes)
					{
						if (NodeItr->ComponentClass->IsChildOf(ComponentClass))
						{
							ComponentMap.Add(NodeItr->GetVariableName(), NodeItr->GetActualComponentTemplate(Cast<UBlueprintGeneratedClass>(ActorClass)));
						}
					}
				}

				// Search Inherited Components
				if (UInheritableComponentHandler* Handler = CurrentBPClass->GetInheritableComponentHandler())
				{
					TArray<FComponentOverrideRecord>::TIterator InheritedComponentItr = Handler->CreateRecordIterator();
					for (InheritedComponentItr; InheritedComponentItr; ++InheritedComponentItr)
					{
						const FComponentOverrideRecord& Record = *InheritedComponentItr;
						if (Record.ComponentClass->IsChildOf(ComponentClass) && Record.ComponentTemplate)
						{
							ComponentMap.Add(Record.ComponentKey.GetSCSVariableName(), Record.ComponentTemplate);
						}
					}
				}
			}
		}
	}

	ComponentMap.GenerateKeyArray(FoundComponentNames);

	TArray<UActorComponent*> ReturnVal = {};
	ComponentMap.GenerateValueArray(ReturnVal);

	return ReturnVal;
}

USceneComponent* UBlueprintNativizationLibrary::GetParentComponentTemplate(UActorComponent* ActorComponent)
{
	if (USCS_Node* Node = FindSCSNodeForComponent(ActorComponent))
	{
        UBlueprintGeneratedClass* BlueprintClass = Cast<UBlueprintGeneratedClass>(ActorComponent->GetOuter());
        if (!BlueprintClass && ActorComponent->GetOwner())
        {
            BlueprintClass = Cast<UBlueprintGeneratedClass>(ActorComponent->GetOwner()->GetClass());
        }
        if (USCS_Node* ParentNode = Node->GetSCS()->FindParentNode(Node))
        {
            return Cast<USceneComponent>(ParentNode->GetActualComponentTemplate(BlueprintClass));
        }
		if (BlueprintClass)
		{
			return Node->GetParentComponentTemplate(BlueprintClass);
		}
	}
    if (USceneComponent* SceneComponent = Cast<USceneComponent>(ActorComponent))
    {
        return SceneComponent->GetAttachParent();
    }
	return nullptr;
}

FName UBlueprintNativizationLibrary::GetComponentAttachmentSocketName(UActorComponent* ActorComponent)
{
    if (const USCS_Node* Node = FindSCSNodeForComponent(ActorComponent))
    {
        return Node->AttachToName;
    }
    if (const USceneComponent* SceneComponent = Cast<USceneComponent>(ActorComponent))
    {
        return SceneComponent->GetAttachSocketName();
    }
    return NAME_None;
}



bool UBlueprintNativizationLibrary::IsRootComponentTemplate(UActorComponent* ActorComponent)
{
    if (!ActorComponent || !ActorComponent->IsA<USceneComponent>())
    {
        return false;
    }
	if (const USCS_Node* Node = FindSCSNodeForComponent(ActorComponent))
	{
        return Node->IsRootNode() && Node->ParentComponentOrVariableName.IsNone() &&
            Node->ParentComponentOwnerClassName.IsNone() && !Node->bIsParentComponentNative;
	}
	return false;
}

FName UBlueprintNativizationLibrary::GetRealBPPropertyName(UActorComponent* ActorComponent)
{
	if (const USCS_Node* Node = FindSCSNodeForComponent(ActorComponent))
	{
		return Node->GetVariableName();
	}
	return NAME_None;
}

FString UBlueprintNativizationLibrary::GetUniquePropertyComponentGetterName(UActorComponent* ActorComponent, const TArray<FGenerateFunctionStruct>& EntryNodes, FString* ComponentPropertyName)
{
	if (!ActorComponent)
	{
		return "";
	}

	UClass* OwnerClass = Cast<UClass>(ActorComponent->GetOuter());
	if (!OwnerClass)
	{
		if (AActor* Owner = ActorComponent->GetOwner())
        {
            OwnerClass = Owner->GetClass();
        }
		if (!OwnerClass)
		{
			return "";
		}
	}

	FName Name = GetRealBPPropertyName(ActorComponent);

	if (Name == NAME_None)
	{
		FString ComponentName = ActorComponent->GetName();
        ComponentName.RemoveFromEnd(TEXT("_GEN_VARIABLE"));
        Name = FName(*ComponentName);
	}

	FProperty* AnswerProperty = nullptr;

	for (TFieldIterator<FProperty> PropIt(OwnerClass); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;

		if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Property))
		{
			if (ObjProp->PropertyClass->IsChildOf(UActorComponent::StaticClass()))
			{
				UActorComponent* PropComponent = Cast<UActorComponent>(ObjProp->GetObjectPropertyValue_InContainer(OwnerClass->GetDefaultObject()));
				if (PropComponent == ActorComponent)
				{
					AnswerProperty = Property;
					break;
				}
				else if (Name == Property->GetName())
				{
					AnswerProperty = Property;
					break;
				}
			}
		}
	}
	if (AnswerProperty)
	{

        if (ComponentPropertyName)
        {
            *ComponentPropertyName = GetUniquePropertyName(AnswerProperty);
        }
		FGetterAndSetterPropertyDescriptor OutGetterAndSetterDescriptorDesc;
        if (UBlueprintNativizationSettingsLibrary::FindGetterAndSetterDescriptorDescriptorByProperty(AnswerProperty, OutGetterAndSetterDescriptorDesc) &&
            !OutGetterAndSetterDescriptorDesc.GetterPropertyFunctionName.IsEmpty())
        {
            return OutGetterAndSetterDescriptorDesc.GetterPropertyFunctionName + TEXT("()");
        }
        if (AnswerProperty->GetOwnerStruct()->IsNative() &&
            AnswerProperty->HasAnyPropertyFlags(CPF_NativeAccessSpecifierPrivate | CPF_NativeAccessSpecifierProtected))
        {
            return FRetainedBlueprintGeneration::ReadProperty(AnswerProperty, TEXT("this"));
        }
		else
		{
			return GetUniquePropertyName(AnswerProperty);
		}
	}
	else
	{
        if (ComponentPropertyName)
        {
            *ComponentPropertyName = Name.ToString();
        }
        return Name.ToString();
	}
}

UActorComponent* UBlueprintNativizationLibrary::GetComponentByPropertyName(FString PropertyName, UBlueprint* Blueprint)
{
	USCS_Node* Node = FindSCSNodeForPropertyName(PropertyName, Blueprint);
	if (Node)
	{
		if (UBlueprintGeneratedClass* BPClass = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass))
		{
			return Node->GetActualComponentTemplate(BPClass);
		}
	}
	else
	{
		for (TFieldIterator<FProperty> PropIt(Blueprint->GeneratedClass); PropIt; ++PropIt)
		{
			FProperty* Property = *PropIt;

			if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Property))
			{
				if (ObjProp->PropertyClass->IsChildOf(UActorComponent::StaticClass()))
				{
					UActorComponent* PropComponent = Cast<UActorComponent>(ObjProp->GetObjectPropertyValue_InContainer(Blueprint->GeneratedClass->GetDefaultObject()));
					if (PropertyName == Property->GetName())
					{
						return PropComponent;
					}
				}
			}
		}
	}

	return nullptr;
}


USCS_Node* UBlueprintNativizationLibrary::FindSCSNodeForComponent(UActorComponent* ActorComponent)
{
	if (!ActorComponent)
	{
		return nullptr;
	}

	UClass* OwnerClass = Cast<UClass>(ActorComponent->GetOuter());
	if (!OwnerClass)
	{
        if (AActor* Owner = ActorComponent->GetOwner())
        {
            OwnerClass = Owner->GetClass();
        }
		if (!OwnerClass)
		{
			return nullptr;
		}
	}


    UBlueprintGeneratedClass* ComponentClass = Cast<UBlueprintGeneratedClass>(OwnerClass);
    for (UClass* CurrentClass = OwnerClass; CurrentClass; CurrentClass = CurrentClass->GetSuperClass())
    {
        UBlueprintGeneratedClass* BlueprintClass = Cast<UBlueprintGeneratedClass>(CurrentClass);
        if (BlueprintClass && BlueprintClass->SimpleConstructionScript)
        {
            for (USCS_Node* ComponentNode : BlueprintClass->SimpleConstructionScript->GetAllNodes())
            {
                if (ComponentNode->GetActualComponentTemplate(ComponentClass) == ActorComponent)
                {
                    return ComponentNode;
                }
            }
        }
    }

	return nullptr;
}

USCS_Node* UBlueprintNativizationLibrary::FindSCSNodeForPropertyName(FString PropertyName, UBlueprint* Blueprint)
{

	if (UBlueprintGeneratedClass* BPClass = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass))
	{
		if (BPClass->SimpleConstructionScript)
		{
			const TArray<USCS_Node*>& AllSCSNodes = BPClass->SimpleConstructionScript->GetAllNodes();
			for (USCS_Node* NodeItr : AllSCSNodes)
			{
				if (NodeItr->GetVariableName() == PropertyName)
				{
					return NodeItr;
				}
			}
		}
	}

	return nullptr;
}

TArray<FString> UBlueprintNativizationLibrary::OpenFileDialog(FString FileTypes)
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		FString OutFileName;
		TArray<FString> OutFiles;
		const FString Title = TEXT("ChooseFile");
		const FString DefaultPath = FPaths::ProjectDir();

		bool bOpened = DesktopPlatform->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			Title,
			DefaultPath,
			TEXT(""),
			FileTypes,
			EFileDialogFlags::None,
			OutFiles
		);

		if (bOpened && OutFiles.Num() > 0)
		{
			OutFileName = OutFiles[0];
			UE_LOG(LogTemp, Log, TEXT("ChoosenFile: %s"), *OutFileName);
			return OutFiles;
		}
	}
	return TArray<FString>();
}


FString UBlueprintNativizationLibrary::GetPinType(FEdGraphPinType& PinType, bool HideUselessTypeDeclaration)
{
    if (PinType.IsContainer())
    {
        FEdGraphPinType ElementType = PinType;
        ElementType.ContainerType = EPinContainerType::None;
        ElementType.bIsReference = false;
        const FString ElementName = GetPinType(ElementType, false);
        if (PinType.IsMap())
        {
            FEdGraphPinType ValueType;
            ValueType.PinCategory = PinType.PinValueType.TerminalCategory;
            ValueType.PinSubCategory = PinType.PinValueType.TerminalSubCategory;
            ValueType.PinSubCategoryObject = PinType.PinValueType.TerminalSubCategoryObject;
            return FString::Format(TEXT("TMap<{0}, {1}>"), { ElementName, GetPinType(ValueType, false) });
        }
        if (PinType.IsSet())
        {
            return FString::Format(TEXT("TSet<{0}>"), { ElementName });
        }
        return FString::Format(TEXT("TArray<{0}>"), { ElementName });
    }
    if (PinType.PinCategory == UEdGraphSchema_K2::PC_Object && !PinType.PinSubCategoryObject.IsValid())
    {
        return TEXT("UObject*");
    }
    if (UEnum* Enumeration = Cast<UEnum>(PinType.PinSubCategoryObject.Get()))
    {
        FString EnumType = GetUniqueFieldName(Enumeration);
        if (!Enumeration->CppType.IsEmpty())
        {
            EnumType = Enumeration->CppType;
            if (Enumeration->GetCppForm() != UEnum::ECppForm::EnumClass)
            {
                return TEXT("TEnumAsByte<") + EnumType + TEXT(">");
            }
        }
        return EnumType;
    }
    if (PinType.PinCategory == UEdGraphSchema_K2::PC_Real)
    {
        if (PinType.PinSubCategory == UEdGraphSchema_K2::PC_Float)
        {
            return TEXT("float");
        }
        return TEXT("double");
    }

	auto GetTypeName = [](const FName& Category, UObject* SubCategoryObject) -> FString
		{
			if (Category == UEdGraphSchema_K2::PC_Struct && SubCategoryObject)
			{
                if (UScriptStruct* Structure = Cast<UScriptStruct>(SubCategoryObject))
                {
                    if (UBlueprintNativizationSettingsLibrary::FindStructTranslator(Structure))
                    {
                        FStructTranslationRequest Request;
                        Request.Structure = Structure;
                        Request.Operation = EStructTranslationOperation::DescribeType;
                        return UBlueprintNativizationSettingsLibrary::TranslateStructure(Request);
                    }
                }
				return FString::Format(TEXT("{0}"), { UBlueprintNativizationLibrary::GetUniqueFieldName(Cast<UField>(SubCategoryObject))});
			}
			if (Category == UEdGraphSchema_K2::PC_Object && SubCategoryObject)
			{
				UClass* ObjectClass = CastChecked<UClass>(SubCategoryObject);
                FString TypeName = FRetainedBlueprintGeneration::GetClassName(ObjectClass) + TEXT("*");
                return TypeName;
			}
            if (Category == UEdGraphSchema_K2::PC_Interface && SubCategoryObject)
            {
                if (FRetainedBlueprintGeneration::IsRetained(Cast<UClass>(SubCategoryObject)))
                {
                    FNativeApiCatalog::AddError(TEXT("A Blueprint-only interface must be included in generation: ") + SubCategoryObject->GetPathName());
                }
                return FString::Format(TEXT("TScriptInterface<{0}>"), {
                    UBlueprintNativizationLibrary::GetUniqueIInterfaceFieldName(Cast<UClass>(SubCategoryObject)) });
            }
            if (Category == UEdGraphSchema_K2::PC_SoftObject && SubCategoryObject)
            {
                return FString::Format(TEXT("TSoftObjectPtr<{0}>"), { FRetainedBlueprintGeneration::GetClassName(Cast<UClass>(SubCategoryObject)) });
            }
            if (Category == UEdGraphSchema_K2::PC_SoftClass && SubCategoryObject)
            {
                return FString::Format(TEXT("TSoftClassPtr<{0}>"), { FRetainedBlueprintGeneration::GetClassName(Cast<UClass>(SubCategoryObject)) });
            }
			if (Category == UEdGraphSchema_K2::PC_Class && SubCategoryObject)
			{
				return FString::Format(TEXT("TSubclassOf<{0}>"), { FRetainedBlueprintGeneration::GetClassName(Cast<UClass>(SubCategoryObject))});
			}
			if (Category == UEdGraphSchema_K2::PC_Enum && SubCategoryObject)
			{
				return FString::Format(TEXT("TEnumAsByte<{0}>"), { UBlueprintNativizationLibrary::GetUniqueFieldName(SubCategoryObject->GetClass()) });
			}
			return Category.ToString(); // ������� ���
		};

	if (PinType.PinSubCategoryObject.IsValid())
	{
		return GetTypeName(PinType.PinCategory, PinType.PinSubCategoryObject.Get());
	}

	// ������� ���
	if (HideUselessTypeDeclaration)
	{
		return "";
	}
	else
	{
		if (PinType.PinCategory == UEdGraphSchema_K2::PC_String)
		{
			return TEXT("FString");
		}
		if (PinType.PinCategory == UEdGraphSchema_K2::PC_Text)
		{
			return TEXT("FText");
		}
		if (PinType.PinCategory == UEdGraphSchema_K2::PC_Name)
		{
			return TEXT("FName");
		}
		if (PinType.PinCategory == UEdGraphSchema_K2::PC_Int)
		{
			return TEXT("int32");
		}
        if (PinType.PinCategory == UEdGraphSchema_K2::PC_Int64)
        {
            return TEXT("int64");
        }
		if (PinType.PinCategory == UEdGraphSchema_K2::PC_Byte)
		{
			return TEXT("uint8");
		}
		if (PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
		{
			return TEXT("bool");
		}
		if (PinType.PinCategory == UEdGraphSchema_K2::PC_Float)
		{
			return TEXT("float");
		}
		if (PinType.PinCategory == UEdGraphSchema_K2::PC_Double)
		{
			return TEXT("double");
		}

		return PinType.PinCategory.ToString();
	}
}

FString UBlueprintNativizationLibrary::GetPropertyType(FProperty* Property)
{
	if (!Property)
	{
		return TEXT("void");
	}
    if (FInterfaceProperty* InterfaceProperty = CastField<FInterfaceProperty>(Property))
    {
        return TEXT("TScriptInterface<") + GetUniqueIInterfaceFieldName(InterfaceProperty->InterfaceClass) + TEXT(">");
    }
    if (FMulticastDelegateProperty* Delegate = CastField<FMulticastDelegateProperty>(Property))
    {
        if (Delegate->GetOwnerClass() && !Delegate->GetOwnerClass()->HasAnyClassFlags(CLASS_Native))
        {
            return TEXT("F") + GetUniqueFieldName(Delegate->GetOwnerClass()).RightChop(1) + GetUniquePropertyName(Delegate);
        }
        return Delegate->GetCPPType(nullptr, CPPF_BlueprintCppBackend);
    }

	if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
	{
		FString InnerType = UBlueprintNativizationLibrary::GetPropertyType(ArrayProp->Inner);
		return FString::Format(TEXT("TArray<{0}>"), { InnerType });
	}
	else if (FSetProperty* SetProp = CastField<FSetProperty>(Property))
	{
		FString ElemType = UBlueprintNativizationLibrary::GetPropertyType(SetProp->ElementProp);
		return FString::Format(TEXT("TSet<{0}>"), { ElemType });
	}
	else if (FMapProperty* MapProp = CastField<FMapProperty>(Property))
	{
		FString KeyType = UBlueprintNativizationLibrary::GetPropertyType(MapProp->KeyProp);
		FString ValueType = UBlueprintNativizationLibrary::GetPropertyType(MapProp->ValueProp);
		return FString::Format(TEXT("TMap<{0},{1}>"), { KeyType, ValueType });
	}

	if (FSoftClassProperty* SoftClass = CastField<FSoftClassProperty>(Property))
	{
		return FString::Format(
			TEXT("TSoftClassPtr<{0}>"),
			{ FRetainedBlueprintGeneration::GetClassName(SoftClass->MetaClass) }
		);
	}
	else if (FSoftObjectProperty* SoftObject = CastField<FSoftObjectProperty>(Property))
	{
		return FString::Format(
			TEXT("TSoftObjectPtr<{0}>"),
			{ FRetainedBlueprintGeneration::GetClassName(SoftObject->PropertyClass) }
		);
	}
	else if (FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
	{
		return FString::Format(
			TEXT("TSubclassOf<{0}>"),
			{ FRetainedBlueprintGeneration::GetClassName(ClassProperty->MetaClass) }
		);
	}
	else if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
	{
		FString Type = FRetainedBlueprintGeneration::GetClassName(ObjectProperty->PropertyClass) + TEXT("*");
        if (ObjectProperty->PropertyClass->HasAnyClassFlags(CLASS_Const))
        {
            Type = TEXT("const ") + Type;
        }
        return Type;
	}
	else if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
	{
		if (EnumProperty->GetEnum())
		{
			if (!EnumProperty->GetEnum()->CppType.IsEmpty())
            {
                return EnumProperty->GetCPPType(nullptr, 0);
            }
            return UBlueprintNativizationLibrary::GetUniqueFieldName(EnumProperty->GetEnum());
		}
		else
		{
			return TEXT("WrongEnumType");
		}
	}
	else if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
	{
		if (ByteProperty->Enum)
		{
			if (!ByteProperty->Enum->CppType.IsEmpty())
            {
                return ByteProperty->GetCPPType(nullptr, 0);
            }
            return UBlueprintNativizationLibrary::GetUniqueFieldName(ByteProperty->Enum);
		}
		else
		{
			return TEXT("uint8");
		}
	}
	else if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		if (StructProperty->Struct)
		{
			return UBlueprintNativizationLibrary::GetUniqueFieldName(StructProperty->Struct);
		}
		else
		{
			return TEXT("Struct");
		}
	}
	else
	{
		FString ExtendType;
		FString Type = Property->GetCPPType(&ExtendType, 0U);

		const bool bIsReference = Property->HasAnyPropertyFlags(CPF_ReferenceParm) ||
			(Property->HasAnyPropertyFlags(CPF_OutParm) && !Property->HasAnyPropertyFlags(CPF_ReturnParm));
		const bool bIsConst = Property->HasAnyPropertyFlags(CPF_ConstParm);

		FString FinalType;

		if (bIsConst)
		{
			FinalType += TEXT("const ");
		}

		FinalType += Type;

		if (bIsReference)
		{
			FinalType += TEXT("&");
		}

		FinalType += ExtendType;

		return FinalType;
	}
}


TArray<FProperty*> UBlueprintNativizationLibrary::GetExposeOnSpawnProperties(UClass* Class)
{
	TArray<FProperty*> Result;

	if (!Class)
	{
		return Result;
	}

	for (TFieldIterator<FProperty> It(Class); It; ++It)
	{
		FProperty* Property = *It;
		if (Property->HasAnyPropertyFlags(CPF_ExposeOnSpawn))
		{
			Result.Add(Property);
		}
	}

	return Result;
}

bool UBlueprintNativizationLibrary::CheckAnySubPinLinked(UEdGraphPin* Pin)
{
	if (!Pin)
	{
		return false;
	}

	if (Pin->LinkedTo.Num() > 0)
	{
		return true;
	}

	for (UEdGraphPin* SubPin : Pin->SubPins)
	{
		if (CheckAnySubPinLinked(SubPin))
		{
			return true;
		}
	}

	return false;
}

void UBlueprintNativizationLibrary::ConvertPropertyToPinType(const FProperty* Property, FEdGraphPinType& OutPinType)
{
    OutPinType = FEdGraphPinType();
    if (!Property || !GetDefault<UEdGraphSchema_K2>()->ConvertPropertyToPinType(Property, OutPinType))
    {
        OutPinType.PinCategory = UEdGraphSchema_K2::PC_Wildcard;
    }
}

bool UBlueprintNativizationLibrary::ContainsAssetInPinType(const FEdGraphPinType& PinType, FString DefaultValue, UObject* DefaultObject)
{
	FString Content;

	auto TrimFirstAndLast = [](const FString& Str) -> FString
		{
			return (Str.Len() > 1) ? Str.Mid(1, Str.Len() - 2) : FString();
		};

	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Object ||
		PinType.PinCategory == UEdGraphSchema_K2::PC_Class ||
		PinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject ||
		PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass)
	{
		if (!DefaultObject)
		{
			DefaultObject = LoadObject<UObject>(nullptr, *DefaultValue);
		}

		if (DefaultObject && DefaultObject->IsAsset())
		{
			return true;
		}
		return false;
	}

	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
	{
		UScriptStruct* Struct = Cast<UScriptStruct>(PinType.PinSubCategoryObject);
		if (Struct)
		{
			int Count = 0;

			for (TFieldIterator<FProperty> It(Struct); It; ++It)
			{
				FProperty* Property = *It;
				TArray<FString> Parts;
				SplitIgnoringParentheses(TrimFirstAndLast(DefaultValue), Parts);
				FString ValueStr = "";
				for (FString Part : Parts)
				{
					if (Part.Contains(Property->GetName()))
					{
						ValueStr = ExtractNestedParam(Part, Property->GetName());
						break;
					}
				}

				if (ValueStr.IsEmpty())
				{
					ValueStr = GetStructPart(DefaultValue, Count);
				}

				FEdGraphPinType InnerPinType;
				ConvertPropertyToPinType(Property, InnerPinType);

				if (ContainsAssetInPinType(InnerPinType, ValueStr, nullptr))
				{
					return true;
				}
				Count++;
			}
			return false;
		}
	}
	else if (PinType.IsArray() && PinType.IsSet())
	{
		TArray<FString> Tokens;
		DefaultValue.ParseIntoArray(Tokens, TEXT(","), true);

		for (const FString& Token : Tokens)
		{
			FString ElemVar;
			FEdGraphPinType CopyType = PinType;
			CopyType.ContainerType = EPinContainerType::None;

			if (ContainsAssetInPinType(CopyType, Token, DefaultObject))
			{
				return true;
			}
		}
		return false;
	}
	else if (PinType.IsMap())
	{
		TArray<FString> Tokens;
		DefaultValue.ParseIntoArray(Tokens, TEXT(","), true);

		for (const FString& Token : Tokens)
		{
			FString ElemVar;
			FEdGraphPinType CopyType = PinType;
			CopyType.ContainerType = EPinContainerType::None;

			FString Trim = TrimFirstAndLast(Token);
			TArray<FString> Parts;
			Trim.ParseIntoArray(Parts, TEXT(","), false);

			if (ContainsAssetInPinType(CopyType, Token, DefaultObject))
			{
				return true;
			}
		}
		return false;
	}

	return false;
}

void UBlueprintNativizationLibrary::SplitIgnoringParentheses(const FString& Input, TArray<FString>& OutParts)
{
	OutParts.Reset();

	FString Current;
	int32 ParenthesesDepth = 0;

	for (int32 i = 0; i < Input.Len(); ++i)
	{
		TCHAR Ch = Input[i];

		if (Ch == '(')
		{
			ParenthesesDepth++;
			Current.AppendChar(Ch);
		}
		else if (Ch == ')')
		{
			ParenthesesDepth = FMath::Max(0, ParenthesesDepth - 1);
			Current.AppendChar(Ch);
		}
		else if (Ch == ',' && ParenthesesDepth == 0)
		{
			OutParts.Add(Current.TrimStartAndEnd());
			Current.Empty();
		}
		else
		{
			Current.AppendChar(Ch);
		}
	}

	if (!Current.IsEmpty())
	{
		OutParts.Add(Current.TrimStartAndEnd());
	}
}

bool UBlueprintNativizationLibrary::IsAvailibleOneLineDefaultConstructor(FEdGraphPinType Type)
{
	if (Type.PinCategory == UEdGraphSchema_K2::PC_Struct && Type.PinSubCategoryObject.IsValid())
	{
		if (UBlueprintNativizationSettingsLibrary::FindStructTranslator(Cast<UScriptStruct>(Type.PinSubCategoryObject.Get())))
		{
			return true;
		}
		if (UStruct* Struct = Cast<UStruct>(Type.PinSubCategoryObject.Get()))
		{
			FConstructorDescriptor ConstructorDescriptor;
			if (UBlueprintNativizationSettingsLibrary::FindConstructorDescriptorByStruct(Struct, ConstructorDescriptor) || !Struct->IsNative())
			{
				return true;
			}
			else
			{
				return false;
			}
		}
	}
	return true;
}


UEdGraphPin* UBlueprintNativizationLibrary::GetParentPin(UEdGraphPin* Pin)
{
	if (Pin->ParentPin)
	{
		return GetParentPin(Pin->ParentPin);
	}
	else
	{
		return Pin;
	}
}

TArray<UEdGraphPin*> UBlueprintNativizationLibrary::GetParentPathPins(UEdGraphPin* ChildPin)
{
	TArray<UEdGraphPin*> ResultArray;
	
	while (ChildPin)
	{
		ResultArray.Add(ChildPin);
		ChildPin = ChildPin->ParentPin;
	}

	return ResultArray;
}

UEdGraph* UBlueprintNativizationLibrary::GetGraphByCompositeNode(UBlueprint* Blueprint, UK2Node_Composite* GraphNode_Composite)
{
	TArray<UEdGraph*> AllGraphs;
	FGenerationGraphContext::GetGraphs(Blueprint, AllGraphs);
	for (UEdGraph* Graph : AllGraphs)
	{
		if (Graph && Graph->GetOuter()->IsA<UK2Node_Composite>() && Graph->GetOuter() == GraphNode_Composite)
		{
			if (Graph->GetOuter() == GraphNode_Composite)
			{
				return Graph;
			}
		}
	}
	return nullptr;
}

TArray<UK2Node*> UBlueprintNativizationLibrary::GetReturnNodes(UK2Node* InputNode, TArray<FGenerateFunctionStruct> EntryNodes)
{
	if (!InputNode)
	{
		return TArray<UK2Node*>();
	}

	 UEdGraph* Graph = InputNode->GetGraph();
	if (!Graph)
	{
		return TArray<UK2Node*>();
	}

	return GetReturnNodesPerGraph(Graph);
}

TArray<UK2Node*> UBlueprintNativizationLibrary::GetReturnNodesPerGraph(UEdGraph* EdGraph)
{
	const EGraphType GraphType = EdGraph->GetSchema()->GetGraphType(EdGraph);
	TArray<UK2Node*> Nodes;

	if (GraphType == GT_Function)
	{
		for (UEdGraphNode* Node : EdGraph->Nodes)
		{
			if (Cast<UK2Node_FunctionResult>(Node))
			{
				Nodes.Add(Cast<UK2Node>(Node));
			}
		}

	}
	else if (GraphType == GT_Macro)
	{
		for (UEdGraphNode* Node : EdGraph->Nodes)
		{
			if (UK2Node_Tunnel*  Tunnel = Cast<UK2Node_Tunnel>(Node))
			{
				if (Tunnel->bCanHaveInputs)
				{
					Nodes.Add(Cast<UK2Node>(Node));
				}
			}
		}
	}


	return Nodes;
}
bool UBlueprintNativizationLibrary::FindFieldModuleName(UField* InField, FString& ModuleName)
{
    if (UClass* Class = Cast<UClass>(InField))
    {
        InField = FRetainedBlueprintGeneration::GetStorageClass(Class);
    }
	bool bResult = false;

	if (InField)
	{
		UPackage* FieldPackage = InField->GetOutermost();

		if (FieldPackage)
		{
			FName ShortFieldPackageName = FPackageName::GetShortFName(FieldPackage->GetFName());

			if (FModuleManager::Get().IsModuleLoaded(ShortFieldPackageName))
			{
				FModuleStatus ModuleStatus;
				if (ensure(FModuleManager::Get().QueryModule(ShortFieldPackageName, ModuleStatus)))
				{
					ModuleName = FPaths::GetBaseFilename(ModuleStatus.FilePath);
					bResult = true;
				}
			}
		}
	}

	return bResult;
}

FString UBlueprintNativizationLibrary::GetEnumValueName(const UEnum* Enumeration, int64 Value)
{
    if (!Enumeration->IsA<UUserDefinedEnum>())
    {
        return Enumeration->GetNameStringByValue(Value);
    }
    if (Value == Enumeration->GetMaxEnumValue())
    {
        return TEXT("MAX");
    }
    TSet<FName> Names = { FName(TEXT("MAX")) };
    UBlueprintNativizationUniqueNameSubsystem* NameSubsystem = GEngine->GetEngineSubsystem<UBlueprintNativizationUniqueNameSubsystem>();
    for (int32 EntryIndex = 0; EntryIndex < Enumeration->NumEnums(); ++EntryIndex)
    {
        FString BaseName = NameSubsystem->SanitizeIdentifier(Enumeration->GetDisplayNameTextByIndex(EntryIndex).ToString());
        BaseName[0] = FChar::ToUpper(BaseName[0]);
        FString Name = BaseName;
        int32 Suffix = 1;
        while (Names.Contains(FName(*Name)))
        {
            Name = BaseName + FString::FromInt(Suffix);
            ++Suffix;
        }
        Names.Add(FName(*Name));
        if (Enumeration->GetValueByIndex(EntryIndex) == Value)
        {
            return Name;
        }
    }
    return FString();
}

FString UBlueprintNativizationLibrary::GenerateEnumLiteral(const UEnum* Enumeration, int64 Value)
{
    const FString Enumerator = GetEnumValueName(Enumeration, Value);
    const FString TypeName = GetUniqueFieldName(const_cast<UEnum*>(Enumeration));
    if (!Enumerator.IsEmpty())
    {
        return TypeName + TEXT("::") + Enumerator;
    }
    return FString::Format(TEXT("static_cast<{0}>({1})"), {TypeName, Value});
}

FString UBlueprintNativizationLibrary::ConvertArrayValue(UEdGraphPin* SourcePin, const FEdGraphPinType& TargetType, const FString& Expression)
{
    if (!SourcePin || !SourcePin->PinType.IsArray() || !TargetType.IsArray())
    {
        return Expression;
    }
    const FEdGraphPinType& SourceType = SourcePin->PinType;
    if (SourceType.PinCategory != TargetType.PinCategory ||
        (TargetType.PinCategory != UEdGraphSchema_K2::PC_Object && TargetType.PinCategory != UEdGraphSchema_K2::PC_Class))
    {
        return Expression;
    }
    UClass* SourceClass = Cast<UClass>(SourceType.PinSubCategoryObject.Get());
    UClass* TargetClass = Cast<UClass>(TargetType.PinSubCategoryObject.Get());
    if (!SourceClass || !TargetClass || SourceClass == TargetClass || !SourceClass->IsChildOf(TargetClass))
    {
        return Expression;
    }
    FEdGraphPinType ConvertedType = TargetType;
    const FString TypeName = GetPinType(ConvertedType, false);
    return TEXT("([](const auto& SourceValues)\n{\n") + TypeName + TEXT(" ConvertedValues;\nConvertedValues.Reserve(SourceValues.Num());\nfor (const auto& Element : SourceValues)\n{\nConvertedValues.Add(Element);\n}\nreturn ConvertedValues;\n})(") + Expression + TEXT(")");
}

FString UBlueprintNativizationLibrary::GetNetworkEventBodyName(UFunction* Function)
{
    UFunction* RootFunction = GetRootFunctionDeclaration(Function);
    if (!RootFunction || !RootFunction->GetOwnerClass()->ClassGeneratedBy ||
        !RootFunction->HasAnyFunctionFlags(FUNC_Net) || RootFunction->HasAnyFunctionFlags(FUNC_Private | FUNC_Static))
    {
        return FString();
    }
    const FString FunctionName = GetUniqueFunctionName(RootFunction, TEXT(""));
    return GEngine->GetEngineSubsystem<UBlueprintNativizationUniqueNameSubsystem>()->GetLambdaUniqueVariableName(
        TEXT("Receive") + FunctionName, RootFunction->GetOwnerClass()).ToString();
}
