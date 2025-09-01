/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/CallFunctionTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"
FString UCallFunctionTranslatorObject::GenerateCodeFromNode(
	UK2Node* Node,
	FString EntryPinName,
	TArray<FVisitedNodeStack> VisitedNodes,
	TArray<UK2Node*> MacroStack,
	UNativizationV2Subsystem* NativizationV2Subsystem)
{
	FString Content;
	UK2Node_CallFunction* CallFuncNode = Cast<UK2Node_CallFunction>(Node);
	if (!CallFuncNode) return Content;

	UFunction* Function = CallFuncNode->GetTargetFunction();
	if (!Function) return Content;

	UClass* OwnerClass = Function->GetOwnerClass();
	FString FuncName = UBlueprintNativizationLibrary::GetUniqueFunctionName(Function, "");

	auto BuildArgs = [&](bool bIsStatic) -> FString
		{
			FString Args;
			bool bFirst = true;

			FString WorldContextParamName;
			UEdGraphPin* WorldContextPin = nullptr;
			if (bIsStatic)
			{
				WorldContextParamName = Function->GetMetaData(TEXT("WorldContext"));
				if (!WorldContextParamName.IsEmpty())
				{
					WorldContextPin = UBlueprintNativizationLibrary::GetPinByName(CallFuncNode->Pins, WorldContextParamName);
				}
			}

			UEdGraphPin* ReturnPin = CallFuncNode->GetReturnValuePin();
			UEdGraphPin* SelfPin = UBlueprintNativizationLibrary::GetPinByName(Node->Pins, TEXT("self"));

			for (TFieldIterator<FProperty> ParamIt(Function); ParamIt && (ParamIt->PropertyFlags & CPF_Parm); ++ParamIt)
			{
				FString ParamCPPName = ParamIt->GetNameCPP();
				UEdGraphPin* Pin = UBlueprintNativizationLibrary::GetPinByName(CallFuncNode->Pins, ParamCPPName);
				if (!Pin) continue;

				if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec || Pin == ReturnPin)
				{
					continue;
				}
				if (!bIsStatic && Pin && Pin->PinName == TEXT("self"))
				{
					continue;
				}

				if (!bFirst) Args += TEXT(", ");
				bFirst = false;

				if (bIsStatic && WorldContextPin && WorldContextPin == Pin)
				{
					Args += TEXT("this");
					continue;
				}
				const bool bIsOut = (ParamIt->PropertyFlags & CPF_OutParm) && !(ParamIt->PropertyFlags & CPF_ReturnParm);
				if (bIsOut && Pin->Direction == EGPD_Output)
				{
					Args += UBlueprintNativizationLibrary::GetUniquePinName(Pin, NativizationV2Subsystem->EntryNodes);
				}
				else if (Pin->Direction == EGPD_Input)
				{
					TArray<UK2Node*> LocalMacroStack = MacroStack;
					Args += NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, Pin, 0, LocalMacroStack);
				}
				else
				{
					Args += UBlueprintNativizationLibrary::GetUniquePinName(Pin, NativizationV2Subsystem->EntryNodes);
				}
			}

			return Args;
		};

	if (!Function->HasAnyFunctionFlags(EFunctionFlags::FUNC_Static))
	{
		UEdGraphPin* SelfPin = UBlueprintNativizationLibrary::GetPinByName(Node->Pins, TEXT("self"));
		FString Args = BuildArgs(false);

		int32 NumCalls = 1;
		if (SelfPin)
		{
			NumCalls = FMath::Max(1, SelfPin->LinkedTo.Num());
		}

		for (int32 Count = 0; Count < NumCalls; ++Count)
		{
			FString ObjectExpr;
			if (SelfPin)
			{
				TArray<UK2Node*> LocalMacroStack = MacroStack;
				ObjectExpr = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, SelfPin, 0, LocalMacroStack);
			}

			UEdGraphPin* RetPin = CallFuncNode->GetReturnValuePin();
			if (RetPin && RetPin->LinkedTo.Num()>0)
			{
				if (ObjectExpr.IsEmpty())
				{
					FString RetVar = UBlueprintNativizationLibrary::GetUniquePinName(RetPin, NativizationV2Subsystem->EntryNodes);
					Content += FString::Format(TEXT("{0} = {1}({2});"), {
						*RetVar,
						*FuncName,
						*Args
						});
				}
				else
				{
					FString RetVar = UBlueprintNativizationLibrary::GetUniquePinName(RetPin, NativizationV2Subsystem->EntryNodes);
					Content += FString::Format(TEXT("{0} = {1}->{2}({3});"), {
						*RetVar,
						*ObjectExpr,
						*FuncName,
						*Args
						});
				}
			}
			else
			{
				if (ObjectExpr.IsEmpty())
				{
					Content += FString::Format(TEXT("{0}({1});"), {
					*FuncName,
					*Args
					});
				}
				else
				{
					Content += FString::Format(TEXT("{0}->{1}({2});"), {
					*ObjectExpr,
					*FuncName,
					*Args
					});
				}
			}
		}
	}
	else 
	{
		FString Args = BuildArgs(true);

		UEdGraphPin* RetPin = CallFuncNode->GetReturnValuePin();
		FString OwnerName = OwnerClass ? UBlueprintNativizationLibrary::GetUniqueFieldName(OwnerClass) : FString(TEXT("/*UnknownOwner*/"));

		if (RetPin && RetPin->LinkedTo.Num() > 0)
		{
			FString RetVar = UBlueprintNativizationLibrary::GetUniquePinName(RetPin, NativizationV2Subsystem->EntryNodes);
			Content += FString::Format(TEXT("{0} = {1}::{2}({3});"), {
				*RetVar,
				*OwnerName,
				*FuncName,
				*Args
				});
		}
		else
		{
			Content += FString::Format(TEXT("{0}::{1}({2});"), {
				*OwnerName,
				*FuncName,
				*Args
				});
		}
	}

	return Content;
}


FString UCallFunctionTranslatorObject::GenerateInputParameterCodeForNode(
	UK2Node* Node,
	UEdGraphPin* Pin,
	int PinIndex,
	TArray<UK2Node*> MacroStack,
	UNativizationV2Subsystem* NativizationV2Subsystem)
{
	if (Node && Node->IsNodePure() && Cast<UK2Node_CallFunction>(Node))
	{
		UK2Node_CallFunction* CallFuncNode = Cast<UK2Node_CallFunction>(Node);
		UFunction* Function = CallFuncNode->GetTargetFunction();
		if (!Function) return FString();

		FString FuncName = UBlueprintNativizationLibrary::GetUniqueFunctionName(Function, "");
		UClass* OwnerClass = Function->GetOwnerClass();

		UEdGraphPin* ReturnPin = CallFuncNode->GetReturnValuePin();
		UEdGraphPin* SelfPin = UBlueprintNativizationLibrary::GetPinByName(Node->Pins, TEXT("self"));

		auto ResolveInputPinValue = [&](UEdGraphPin* FnPin, int InPinIndex = 0) -> FString
			{
				if (!FnPin) return FString();

				TArray<UK2Node*> LocalMacroStack = MacroStack;
				return NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, FnPin, InPinIndex, LocalMacroStack);
			};

		if (Pin == ReturnPin)
		{
			if (Function->HasAnyFunctionFlags(FUNC_Static))
			{
				FString Args;
				bool bFirst = true;

				FString WorldContextParamName = Function->GetMetaData(TEXT("WorldContext"));
				UEdGraphPin* WorldContextPin = nullptr;
				if (!WorldContextParamName.IsEmpty())
				{
					WorldContextPin = UBlueprintNativizationLibrary::GetPinByName(Node->Pins, WorldContextParamName);
				}

				for (TFieldIterator<FProperty> ParamIt(Function); ParamIt && (ParamIt->PropertyFlags & CPF_Parm); ++ParamIt)
				{
					UEdGraphPin* ItPin = UBlueprintNativizationLibrary::GetPinByName(Node->Pins, ParamIt->GetNameCPP());
					if (!ItPin) continue;

					if (ItPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec ||
						ItPin == ReturnPin)
					{
						continue;
					}

					if (ItPin->Direction == EGPD_Input)
					{
						if (!bFirst) Args += TEXT(", ");
						bFirst = false;

						if (WorldContextPin && WorldContextPin == ItPin)
						{
							Args += TEXT("this");
						}
						else
						{
							FString Arg = ResolveInputPinValue(ItPin, 0);
							if (!Arg.IsEmpty())
							{
								Args += Arg;
							}
						}
					}
					else
					{
						if (!bFirst) Args += TEXT(", ");
						bFirst = false;
						Args += UBlueprintNativizationLibrary::GetUniquePinName(ItPin, NativizationV2Subsystem->EntryNodes);
					}
				}

				return FString::Format(TEXT("{0}::{1}({2})"), {
					*UBlueprintNativizationLibrary::GetUniqueFieldName(OwnerClass),
					*FuncName,
					*Args
					});
			}
			else
			{
				FString ObjectExpr;
				if (SelfPin)
				{
					TArray<UK2Node*> LocalMacroStack = MacroStack;
					ObjectExpr = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, SelfPin, PinIndex, LocalMacroStack);
				}

				FString Args;
				bool bFirst = true;
				for (TFieldIterator<FProperty> ParamIt(Function); ParamIt && (ParamIt->PropertyFlags & CPF_Parm); ++ParamIt)
				{
					UEdGraphPin* ItPin = UBlueprintNativizationLibrary::GetPinByName(Node->Pins, ParamIt->GetNameCPP());
					if (!ItPin) continue;
					if (ItPin == ReturnPin) continue;
					if (ItPin == SelfPin) continue;
					if (ItPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;

					if (!bFirst) Args += TEXT(", ");
					bFirst = false;

					if (ItPin->Direction == EGPD_Input)
					{
						FString Arg = ResolveInputPinValue(ItPin, 0);
						if (!Arg.IsEmpty())
						{
							Args += Arg;
						}
					}
					else
					{
						Args += UBlueprintNativizationLibrary::GetUniquePinName(ItPin, NativizationV2Subsystem->EntryNodes);
					}
				}
;
				if (ObjectExpr.IsEmpty())
				{
					return  FString::Format(TEXT("{0}({1})"), {*FuncName, *Args });
				}
				else
				{
					return FString::Format(TEXT("{0}->{1}({2})"), { *ObjectExpr, *FuncName, *Args });
				}

			}
		}
		else
		{
			if (Pin)
			{
				TArray<UK2Node*> LocalMacroStack = MacroStack;
				return NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, Pin, PinIndex, LocalMacroStack);
			}

			return UBlueprintNativizationLibrary::GetUniquePinName(
				UBlueprintNativizationLibrary::GetParentPin(Pin),
				NativizationV2Subsystem->EntryNodes);
		}
	}

	return UBlueprintNativizationLibrary::GetUniquePinName(
		UBlueprintNativizationLibrary::GetParentPin(Pin),
		NativizationV2Subsystem->EntryNodes);
}

TSet<FString> UCallFunctionTranslatorObject::GenerateLocalVariables(UK2Node* InputNode, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	TSet<FString> Contents;
	Contents.Append(Super::GenerateLocalVariables(InputNode, MacroStack, NativizationV2Subsystem));

	if (InputNode->IsNodePure())
	{
		return Contents;
	}
	
	UK2Node_CallFunction* CallFuncNode = Cast<UK2Node_CallFunction>(InputNode);

	TArray<UEdGraphPin*> PropertyPins;

	UFunction* Function = CallFuncNode->GetTargetFunction();
	if (Function)
	{
		for (TFieldIterator<FProperty> ParamIt(Function); ParamIt && (ParamIt->PropertyFlags & CPF_Parm); ++ParamIt)
		{
			FProperty* Property = *ParamIt;

			if (!(Property->HasAnyPropertyFlags(CPF_OutParm) && (Property->HasAnyPropertyFlags(CPF_ReferenceParm) || !Property->HasAnyPropertyFlags(CPF_ReturnParm))))
			{
				continue;
			}

			UEdGraphPin* OutputPin = UBlueprintNativizationLibrary::GetPinByName(CallFuncNode->Pins, Property->GetNameCPP());
			if (OutputPin->Direction != EEdGraphPinDirection::EGPD_Output)
			{
				continue;
			}
			if (OutputPin == CallFuncNode->GetReturnValuePin())
			{
				continue;
			}

			FGenerateFunctionStruct InputGenerateFunctionStruct;
			UBlueprintNativizationLibrary::GetEntryByNodeAndEntryNodes(InputNode,NativizationV2Subsystem->EntryNodes, InputGenerateFunctionStruct);

			if (!UBlueprintNativizationLibrary::CheckAnySubPinLinked(OutputPin) || UBlueprintNativizationLibrary::IsPinAllLinkInCurrentFunctionNode(OutputPin, InputGenerateFunctionStruct.Node, NativizationV2Subsystem->EntryNodes))
			{
				PropertyPins.Add(OutputPin);
			}
		}
		TArray<UEdGraphPin*> RootPropertyPins = UBlueprintNativizationLibrary::GetParentPins(PropertyPins);

		for (UEdGraphPin* RootPropertyPin : RootPropertyPins)
		{
			FString VarName = UBlueprintNativizationLibrary::GetUniquePinName(RootPropertyPin, NativizationV2Subsystem->EntryNodes);
			Contents.Add(FString::Format(TEXT("{0} {1} = {2};\n"), { *UBlueprintNativizationLibrary::GetPinType(RootPropertyPin->PinType, true), *VarName, UBlueprintNativizationLibrary::GenerateOneLineDefaultConstructor(RootPropertyPin->GetName(), RootPropertyPin->PinType, RootPropertyPin->DefaultValue, RootPropertyPin->DefaultObject, NativizationV2Subsystem->LeftAllAssetRefInBlueprint) }));
		}
	}

	return Contents;
}

TSet<FString> UCallFunctionTranslatorObject::GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	TSet<FString> Includes;

	if (UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(Node))
	{
		if (UFunction* Function = CallFunctionNode->GetTargetFunction())
		{
			if (Function->IsNative())
			{
				if (UClass* OwnerClass = Function->GetOwnerClass())
				{
					FString IncludePath;
					if (NativizationV2Subsystem->GetIncludeFromField(OwnerClass, IncludePath))
					{
						Includes.Add(IncludePath);
					}
				}
			}
		}
	}

	return Includes;
}

TSet<FString> UCallFunctionTranslatorObject::GenerateCSPrivateDependencyModuleNames(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	TSet<FString> ModuleSet;
	if (UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(Node))
	{
		if (UFunction* Function = CallFunctionNode->GetTargetFunction())
		{
			FString ModuleName;
			if (UBlueprintNativizationLibrary::FindFieldModuleName(Function, ModuleName))
			{
				TArray<FString> Modules;
				ModuleName.ParseIntoArray(Modules, TEXT("-"), true);

				ModuleSet.Add(FString::Format(TEXT("\"{0}\""), { Modules[1] }));
			}
		}
	}

	return ModuleSet;
}
