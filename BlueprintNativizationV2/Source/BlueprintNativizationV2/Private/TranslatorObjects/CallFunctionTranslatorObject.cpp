/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/CallFunctionTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSettings.h"


FString UCallFunctionTranslatorObject::GenerateCodeFromNode(UK2Node* Node,
	FString EntryPinName,
	TArray<FVisitedNodeStack> VisitedNodes,
	TArray<UK2Node*> MacroStack, TSet<FString>& Preparations, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	FString Content;
	UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(Node);
	if (!CallFunctionNode) return Content;

	UFunction* Function = CallFunctionNode->GetTargetFunction();
	if (!Function) return Content;

	UClass* OwnerClass = Function->GetOwnerClass();
	FString FunctionName = UBlueprintNativizationLibrary::GetUniqueFunctionName(Function, "");

	FString Args;
	TSet<FString> NewPreparations;
	BuildArgs(Function->HasAnyFunctionFlags(EFunctionFlags::FUNC_Static), Args, NewPreparations, CallFunctionNode, MacroStack, NativizationV2Subsystem);
	Content += GenerateNewPreparations(Preparations, NewPreparations);
	Preparations.Append(NewPreparations);

	const UBlueprintNativizationV2EditorSettings* Settings = GetDefault<UBlueprintNativizationV2EditorSettings>();

	auto GenerateStandardCall = [&]()
		{
			if (!Function->HasAnyFunctionFlags(EFunctionFlags::FUNC_Static))
			{
				UEdGraphPin* SelfPin = UBlueprintNativizationLibrary::GetPinByName(Node->Pins, TEXT("self"));

				int32 NumCalls = 1;
				if (SelfPin)
				{
					NumCalls = FMath::Max(1, SelfPin->LinkedTo.Num());
				}

				for (int32 Count = 0; Count < NumCalls; ++Count)
				{
					FString InputResultStruct;
					if (SelfPin)
					{
						TArray<UK2Node*> LocalMacroStack = MacroStack;
						InputResultStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, SelfPin, 0, LocalMacroStack).Code;
					}

					UEdGraphPin* RetPin = CallFunctionNode->GetReturnValuePin();
					if (RetPin && RetPin->LinkedTo.Num() > 0)
					{
						if (InputResultStruct.IsEmpty())
						{
							FString RetVar = UBlueprintNativizationLibrary::GetUniquePinName(RetPin, NativizationV2Subsystem->EntryNodes);
							Content += FString::Format(TEXT("{0} = {1}({2});"), {
								*RetVar,
								*FunctionName,
								*Args
								});
						}
						else
						{
							FString RetVar = UBlueprintNativizationLibrary::GetUniquePinName(RetPin, NativizationV2Subsystem->EntryNodes);
							Content += FString::Format(TEXT("{0} = {1}->{2}({3});"), {
								*RetVar,
								*InputResultStruct,
								*FunctionName,
								*Args
								});
						}
					}
					else
					{
						if (InputResultStruct.IsEmpty())
						{
							Content += FString::Format(TEXT("{0}({1});"), {
							*FunctionName,
							*Args
								});
						}
						else
						{
							Content += FString::Format(TEXT("{0}->{1}({2});"), {
							*InputResultStruct,
							*FunctionName,
							*Args
								});
						}
					}
				}
			}
			else
			{
				UEdGraphPin* RetPin = CallFunctionNode->GetReturnValuePin();
				FString OwnerName = OwnerClass ? UBlueprintNativizationLibrary::GetUniqueFieldName(OwnerClass) : FString(TEXT("/*UnknownOwner*/"));

				if (RetPin && RetPin->LinkedTo.Num() > 0)
				{
					FString RetVar = UBlueprintNativizationLibrary::GetUniquePinName(RetPin, NativizationV2Subsystem->EntryNodes);
					Content += FString::Format(TEXT("{0} = {1}::{2}({3});"), {
						*RetVar,
						*OwnerName,
						*FunctionName,
						*Args
						});
				}
				else
				{
					Content += FString::Format(TEXT("{0}::{1}({2});"), {
						*OwnerName,
						*FunctionName,
						*Args
						});
				}
			}
		};

	if (!Settings->bReplaceCallToIgnoreAssetsToDirectBlueprintCall)
	{
		GenerateStandardCall();
		return Content;
	}

	UClass* Class = Function->GetOwnerClass();
	if (UBlueprintNativizationLibrary::HasAnyChildInClasses(Class, Settings->IgnoreClassToRefGenerate) && !Function->HasAnyFunctionFlags(EFunctionFlags::FUNC_Static))
	{
		UEdGraphPin* SelfPin = UBlueprintNativizationLibrary::GetPinByName(Node->Pins, TEXT("self"));

		int32 NumCalls = 1;
		if (SelfPin)
		{
			NumCalls = FMath::Max(1, SelfPin->LinkedTo.Num());
		}

		for (int32 Count = 0; Count < NumCalls; ++Count)
		{
			FString InputResultStruct;
			if (SelfPin)
			{
				TArray<UK2Node*> LocalMacroStack = MacroStack;
				InputResultStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, SelfPin, 0, LocalMacroStack).Code;
			}

			UEdGraphPin* RetPin = CallFunctionNode->GetReturnValuePin();

			if (InputResultStruct.IsEmpty())
			{
				GenerateStandardCall();
				continue;
			}

			FString ParamsStructType = FunctionName + TEXT("_Parms");
			Content += FString::Format(TEXT("{0} Parms;"), { *ParamsStructType });

			FString ReturnPropName;
			for (TFieldIterator<FProperty> It(Function); It; ++It)
			{
				FProperty* Prop = *It;
				if (!Prop->HasAnyPropertyFlags(CPF_Parm)) continue;
				if (Prop->HasAnyPropertyFlags(CPF_ReturnParm))
				{
					ReturnPropName = Prop->GetName();
					continue;
				}

				FString PropName = Prop->GetName();
				UEdGraphPin* ParamPin = UBlueprintNativizationLibrary::GetPinByName(Node->Pins, *PropName);
				if (!ParamPin) continue;

				TArray<UK2Node*> LocalMacroStackForParam = MacroStack;
				FString ParamCode = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, ParamPin, 0, LocalMacroStackForParam).Code;
				if (!ParamCode.IsEmpty())
				{
					Content += ParamCode;
					Content += FString::Format(TEXT("Parms.{0} = {1};"), { *PropName, *ParamCode });
				}
			}

			Content += FString::Printf(TEXT("UFunction* __Func_%s = %s->FindFunction(FName(TEXT(\"%s\")));\n"), *FunctionName, *InputResultStruct, *FunctionName);
			Content += FString::Printf(TEXT("if(__Func_%s) { %s->ProcessEvent(__Func_%s, &Parms); }\n"), *FunctionName, *InputResultStruct, *FunctionName);

			if (!ReturnPropName.IsEmpty() && RetPin && RetPin->LinkedTo.Num() > 0)
			{
				FString RetVar = UBlueprintNativizationLibrary::GetUniquePinName(RetPin, NativizationV2Subsystem->EntryNodes);
				Content += FString::Format(TEXT("{0} = Parms.{1};"), { *RetVar, *ReturnPropName });
			}
		}
		return Content;
	}
	else
	{
		GenerateStandardCall();
		return Content;
	}

	return Content;
}


void UCallFunctionTranslatorObject::BuildArgs(bool bIsStatic, FString& Code, TSet<FString>& Preparation, UK2Node_CallFunction* CallFunctionNode, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	UFunction* Function = CallFunctionNode->GetTargetFunction();
	TArray<FString> AnswerArguments;

	FString WorldContextParamName;

	UEdGraphPin* WorldContextPin = nullptr;
	if (bIsStatic)
	{
		WorldContextParamName = Function->GetMetaData(TEXT("WorldContext"));
		if (!WorldContextParamName.IsEmpty())
		{
			WorldContextPin = UBlueprintNativizationLibrary::GetPinByName(CallFunctionNode->Pins, WorldContextParamName);
		}
	}

	UEdGraphPin* ReturnPin = CallFunctionNode->GetReturnValuePin();
	UEdGraphPin* SelfPin = UBlueprintNativizationLibrary::GetPinByName(CallFunctionNode->Pins, TEXT("self"));

	for (TFieldIterator<FProperty> ParamIt(Function); ParamIt && (ParamIt->PropertyFlags & CPF_Parm); ++ParamIt)
	{
		FString ParamCPPName = ParamIt->GetNameCPP();
		UEdGraphPin* Pin = UBlueprintNativizationLibrary::FindClosestPinByName(CallFunctionNode->Pins, *ParamCPPName, 1);
		
		if (!Pin) continue;

		if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec || Pin == ReturnPin)
		{
			continue;
		}
		if (!bIsStatic && Pin && Pin->PinName == TEXT("self"))
		{
			continue;
		}

		if (bIsStatic && WorldContextPin && WorldContextPin == Pin)
		{
			AnswerArguments.Add(TEXT("this"));
			continue;
		}
		const bool bIsOut = (ParamIt->PropertyFlags & CPF_OutParm) && !(ParamIt->PropertyFlags & CPF_ReturnParm);
		if (bIsOut && Pin->Direction == EGPD_Output)
		{
			AnswerArguments.Add(UBlueprintNativizationLibrary::GetUniquePinName(Pin, NativizationV2Subsystem->EntryNodes));

			/*
			FGenerateResultStruct InputParameterCode = NativizationV2Subsystem->GenerateInputParameterCodeForNode(CallFunctionNode, Pin, 0, MacroStack);
			
			Preparation.Append(InputParameterCode.Preparations);
			Preparation.Add(FString::Format(TEXT("{0} {1} = {2};"), { *UBlueprintNativizationLibrary::GetPinType(Pin->PinType, true),
				UBlueprintNativizationLibrary::GetUniquePinName(Pin, NativizationV2Subsystem->EntryNodes),
				InputParameterCode.Code }));
			*/
		}
		else if (Pin->Direction == EGPD_Input)
		{
			FGenerateResultStruct InputResultStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(CallFunctionNode, Pin, 0, MacroStack);
			Preparation.Append(InputResultStruct.Preparations);
			AnswerArguments.Add(InputResultStruct.Code);
			/*
			UEdGraphPin* NextPin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Pin, 0, false, MacroStack, NativizationV2Subsystem);
			if (NextPin)
			{
				if (UK2Node_CallFunction* LocalFunction = Cast<UK2Node_CallFunction>(NextPin->GetOwningNode()))
				{
					TArray<FProperty*> Properties = UBlueprintNativizationLibrary::GetAllPropertiesByFunction(LocalFunction->GetTargetFunction());

					FProperty* Property = UBlueprintNativizationLibrary::FindClosestPropertyByName(Properties, NextPin->PinName);

					if ((Property->PropertyFlags & CPF_OutParm) && !(Property->PropertyFlags & CPF_ReturnParm && NextPin->Direction == EGPD_Output))
					{
						AnswerArguments.Add(UBlueprintNativizationLibrary::GetUniquePinName(NextPin, NativizationV2Subsystem->EntryNodes));
						Preparation.Add(FString::Format(TEXT("{0};"), { InputResultStruct.Code }));
						continue;
					}
				}
			}
			AnswerArguments.Add(InputResultStruct.Code);
			Preparation.Append(InputResultStruct.Preparations);*/
		}
		else
		{
			AnswerArguments.Add(UBlueprintNativizationLibrary::GetUniquePinName(Pin, NativizationV2Subsystem->EntryNodes));
		}
	}

	Code = FString::Join(AnswerArguments, TEXT(", "));
};


FGenerateResultStruct UCallFunctionTranslatorObject::GenerateInputParameterCodeForNode(
	UK2Node* Node,
	UEdGraphPin* Pin,
	int PinIndex,
	TArray<UK2Node*> MacroStack,
	UNativizationV2Subsystem* NativizationV2Subsystem)
{
	if (Node && Node->IsNodePure())
	{
		if (UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(Node))
		{
			UFunction* Function = CallFunctionNode->GetTargetFunction();
			if (!Function) return FString();

			FString FunctionName = UBlueprintNativizationLibrary::GetUniqueFunctionName(Function, "");
			UClass* OwnerClass = Function->GetOwnerClass();

			UEdGraphPin* ReturnPin = CallFunctionNode->GetReturnValuePin();
			UEdGraphPin* SelfPin = UBlueprintNativizationLibrary::GetPinByName(Node->Pins, TEXT("self"));

			bool bFirst = true;
			FString Args;
			TSet<FString> Preparations;

			BuildArgs(Function->HasAnyFunctionFlags(FUNC_Static), Args, Preparations, CallFunctionNode, MacroStack, NativizationV2Subsystem);

			TArray<FProperty*> Properties = UBlueprintNativizationLibrary::GetAllPropertiesByFunction(Function);
			FProperty* Property = UBlueprintNativizationLibrary::FindClosestPropertyByName(Properties, *Pin->GetName());
			const bool bIsOut = (Property->PropertyFlags & CPF_OutParm) && !(Property->PropertyFlags & CPF_ReturnParm);
			if (bIsOut)
			{
				Preparations.Add(FString::Format(TEXT("{0}::{1}({2});"), { *UBlueprintNativizationLibrary::GetUniqueFieldName(OwnerClass), *FunctionName, *Args }));
				return FGenerateResultStruct(UBlueprintNativizationLibrary::GetUniquePinName(Pin, NativizationV2Subsystem->EntryNodes), Preparations);

			}
			else
			{
				if (ReturnPin == Pin)
				{
					if (Function->HasAnyFunctionFlags(FUNC_Static))
					{
						return FGenerateResultStruct(
							FString::Format(TEXT("{0}::{1}({2})"), { *UBlueprintNativizationLibrary::GetUniqueFieldName(OwnerClass), *FunctionName, *Args }),
							Preparations
						);
					}
					else
					{
						
						FGenerateResultStruct GenerateResult = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, SelfPin, 0, MacroStack);
						
						Preparations.Append(GenerateResult.Preparations);

						if (GenerateResult.Code.IsEmpty())
						{
							return FGenerateResultStruct(
								FString::Format(TEXT("{0}({1})"), {*FunctionName, *Args }),
								Preparations
							);
						}
						else
						{
							return FGenerateResultStruct(
								FString::Format(TEXT("{0}->{1}({2})"), { GenerateResult.Code, *FunctionName, *Args }),
								Preparations
							);
						}
					}
				}
			}
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

	UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(InputNode);

	TArray<UEdGraphPin*> PropertyPins;

	UFunction* Function = CallFunctionNode->GetTargetFunction();
	if (Function)
	{
		for (TFieldIterator<FProperty> ParamIt(Function); ParamIt && (ParamIt->PropertyFlags & CPF_Parm); ++ParamIt)
		{
			FProperty* Property = *ParamIt;

			if (!(Property->HasAnyPropertyFlags(CPF_OutParm) && (Property->HasAnyPropertyFlags(CPF_ReferenceParm) || !Property->HasAnyPropertyFlags(CPF_ReturnParm))))
			{
				continue;
			}

			UEdGraphPin* OutputPin = UBlueprintNativizationLibrary::GetPinByName(CallFunctionNode->Pins, Property->GetNameCPP());
			if (OutputPin->Direction != EEdGraphPinDirection::EGPD_Output)
			{
				continue;
			}
			if (OutputPin == CallFunctionNode->GetReturnValuePin())
			{
				continue;
			}

			FGenerateFunctionStruct InputGenerateFunctionStruct;
			UBlueprintNativizationLibrary::GetEntryByNodeAndEntryNodes(InputNode,NativizationV2Subsystem->EntryNodes, InputGenerateFunctionStruct);

			if (!UBlueprintNativizationLibrary::CheckAnySubPinLinked(OutputPin) ||
				UBlueprintNativizationLibrary::IsPinAllLinkInCurrentFunctionNode(OutputPin, InputGenerateFunctionStruct.Node, NativizationV2Subsystem->EntryNodes))
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
