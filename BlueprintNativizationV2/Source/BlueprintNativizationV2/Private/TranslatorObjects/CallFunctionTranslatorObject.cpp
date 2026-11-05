/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright 2025 AO All Rights Reserved.
 */

#include "TranslatorObjects/CallFunctionTranslatorObject.h"
#include "BlueprintNativizationUniqueNameSubsystem.h"
#include "K2Node_VariableGet.h"
#include "NativeApiCatalog.h"
#include "K2Node_MakeArray.h"
#include "K2Node_MakeContainer.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_FunctionEntry.h"
#include "RetainedBlueprintGeneration.h"
#include "BlueprintNativizationSubsystem.h"
#include "GenerationGraphContext.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSettings.h"
#include "Engine/UserDefinedEnum.h"
#include "K2Node_Message.h"
#include "K2Node_CallFunctionOnMember.h"
#include "K2Node_CallParentFunction.h"
#include "K2Node_CommutativeAssociativeBinaryOperator.h"
#include "K2Node_PromotableOperator.h"
#include "BlueprintTypePromotion.h"
#include "TranslatorObjects/InputEventTranslatorObject.h"
#include "Kismet/KismetTextLibrary.h"
#include "K2Node_IfThenElse.h"
#include "NativeFunctionExpressions.h"

static FString GetCallResultPurpose(UK2Node_CallFunction* Call)
{
    UEdGraphPin* ReturnPin = Call->GetReturnValuePin();
    if (ReturnPin && ReturnPin->LinkedTo.Num() == 1)
    {
        const UEdGraphPin* Destination = ReturnPin->LinkedTo[0];
        const FString Purpose = Destination->PinName.ToString();
        if (Purpose.Len() > 1 && Purpose != TEXT("self") && Purpose != TEXT("Condition") &&
            Purpose != UEdGraphSchema_K2::PN_ReturnValue.ToString())
        {
            return Purpose + TEXT("Value");
        }
    }
    FString FunctionName = Call->FunctionReference.GetMemberName().ToString();
    if (FunctionName.StartsWith(TEXT("K2_")))
    {
        FunctionName.RightChopInline(3);
    }
    return FunctionName + TEXT("Result");
}



static FGenerateResultStruct ResolveMemberCallTarget(UK2Node_CallFunction* Call, FGenerateResultStruct Target,
    UNativizationV2Subsystem* Subsystem)
{
    if (UK2Node_CallFunctionOnMember* MemberCall = Cast<UK2Node_CallFunctionOnMember>(Call))
    {
        FProperty* Property = MemberCall->MemberVariableToCallOn.ResolveMember<FProperty>(Call->GetBlueprint()->GeneratedClass);
        if (!Property)
        {
            FNativeApiCatalog::AddError(TEXT("Cannot resolve member function receiver: ") + Call->GetPathName());
            return {};
        }
        FString Receiver = Target.Code;
        if (Receiver.IsEmpty())
        {
            Receiver = TEXT("this");
        }
        Target.Code = FRetainedBlueprintGeneration::PreparePropertyRead(Property, Receiver, Call, Subsystem, Target.Preparations);
    }
    return Target;
}

static bool IsMutableArgumentSource(UEdGraphPin* SourcePin, TArray<UK2Node*> MacroStack,
    UNativizationV2Subsystem* Subsystem)
{
    while (SourcePin && !SourcePin->PinType.bIsConst)
    {
        if (SourcePin->GetOwningNode()->IsA<UK2Node_MakeContainer>())
        {
            return false;
        }
        if (UK2Node_BreakStruct* Break = Cast<UK2Node_BreakStruct>(SourcePin->GetOwningNode()))
        {
            FProperty* Member = FindFProperty<FProperty>(Break->StructType, SourcePin->PinName);
            if (!Member || Member->HasAnyPropertyFlags(CPF_BlueprintReadOnly) ||
                Break->StructType->HasMetaData(TEXT("HasNativeBreak")) ||
                UBlueprintNativizationSettingsLibrary::FindStructTranslator(Break->StructType))
            {
                return false;
            }
            UEdGraphPin* StructurePin = Break->FindPin(Break->StructType->GetFName(), EGPD_Input);
            if (!StructurePin)
            {
                for (UEdGraphPin* Candidate : Break->Pins)
                {
                    if (Candidate->Direction == EGPD_Input && Candidate->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
                    {
                        StructurePin = Candidate;
                        break;
                    }
                }
            }
            SourcePin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(StructurePin, 0, true, MacroStack, Subsystem);
            continue;
        }
        return SourcePin->PinType.bIsReference || SourcePin->GetOwningNode()->IsA<UK2Node_VariableGet>();
    }
    return false;
}

FString UCallFunctionTranslatorObject::GenerateCodeFromNode(UK2Node* Node,
	FString EntryPinName,
	TArray<FVisitedNodeStack> VisitedNodes,
	TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	FString Content;
	UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(Node);
	if (!CallFunctionNode) return Content;

    UFunction* OriginalFunction = CallFunctionNode->GetTargetFunction();
    if (OriginalFunction && OriginalFunction->HasAnyFunctionFlags(FUNC_Static))
    {
        FRetainedBlueprintGeneration::RegisterFunctionLibraryDependency(OriginalFunction->GetOwnerClass());
        FRetainedBlueprintGeneration::RegisterFunctionLibraryDependency(
            CallFunctionNode->FunctionReference.GetMemberParentClass(CallFunctionNode->GetBlueprint()->GeneratedClass));
    }
	UFunction* Function = UBlueprintNativizationLibrary::GetFunctionDeclaration(OriginalFunction);
	if (!Function) return Content;

    UEdGraphPin* AssignedObject = CallFunctionNode->FindPin(TEXT("Object"), EGPD_Input);
    UEdGraphPin* AssignedName = CallFunctionNode->FindPin(TEXT("PropertyName"), EGPD_Input);
    UEdGraphPin* AssignedValue = CallFunctionNode->FindPin(TEXT("Value"), EGPD_Input);
    if (Function->GetOutermost()->GetName() == TEXT("/Script/Engine") &&
        Function->HasAnyFunctionFlags(FUNC_Static) && Function->GetName().StartsWith(TEXT("Set")) &&
        Function->GetName().EndsWith(TEXT("PropertyByName")) && AssignedObject && AssignedName && AssignedValue &&
        AssignedName->LinkedTo.IsEmpty() && !AssignedObject->LinkedTo.IsEmpty())
    {
        UClass* AssignedClass = Cast<UClass>(AssignedObject->LinkedTo[0]->PinType.PinSubCategoryObject.Get());
        if (AssignedClass)
        {
            if (FProperty* Property = FindFProperty<FProperty>(AssignedClass, *AssignedName->DefaultValue))
            {
                FGenerateResultStruct Receiver = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, AssignedObject, 0, MacroStack);
                FGenerateResultStruct Value = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, AssignedValue, 0, MacroStack);
                if (CastField<FEnumProperty>(Property) ||
                    (CastField<FByteProperty>(Property) && CastFieldChecked<FByteProperty>(Property)->Enum))
                {
                    Value.Code = TEXT("static_cast<") + UBlueprintNativizationLibrary::GetPropertyType(Property) + TEXT(">(") + Value.Code + TEXT(")");
                }
                Receiver.Preparations.Append(Value.Preparations);
                Content += GenerateNewPreparations(Preparations, Receiver.Preparations);
                Preparations.Append(Receiver.Preparations);
                if (FRetainedBlueprintGeneration::IsRetained(Property->GetOwnerClass()) ||
                    Property->HasAnyPropertyFlags(CPF_NativeAccessSpecifierPrivate | CPF_NativeAccessSpecifierProtected))
                {
                    return Content + FRetainedBlueprintGeneration::WriteProperty(Property, Receiver.Code, Value.Code);
                }
                return Content + TEXT("{\nauto* PropertyTarget = ") + Receiver.Code +
                    TEXT(";\nif (IsValid(PropertyTarget))\n{\nPropertyTarget->") + UBlueprintNativizationLibrary::GetUniquePropertyName(Property) +
                    TEXT(" = ") + Value.Code + TEXT(";\n}\n}\n");
            }
        }
    }
    if (Function->GetName() == TEXT("SetArrayPropertyByName") &&
        Function->GetOutermost()->GetName() == TEXT("/Script/Engine") && AssignedObject && AssignedName && AssignedValue)
    {
        FGenerateResultStruct Receiver = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, AssignedObject, 0, MacroStack);
        FGenerateResultStruct Name = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, AssignedName, 0, MacroStack);
        FGenerateResultStruct Value = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, AssignedValue, 0, MacroStack);
        Receiver.Preparations.Append(Name.Preparations);
        Receiver.Preparations.Append(Value.Preparations);
        Content += GenerateNewPreparations(Preparations, Receiver.Preparations);
        Preparations.Append(Receiver.Preparations);
        const FString ValueName = NativizationV2Subsystem->GetNodeStateName(Node, TEXT("AssignedArray"));
        return Content + TEXT("{\nconst auto ") + ValueName + TEXT(" = ") + Value.Code +
            TEXT(";\nUKismetArrayLibrary::GenericArray_SetArrayPropertyByName(") + Receiver.Code + TEXT(", ") +
            Name.Code + TEXT(", &") + ValueName + TEXT(");\n}\n");
    }
    UClass* OwnerClass = Function->GetOwnerClass();

    if (Function->HasAnyFunctionFlags(FUNC_Static))
    {
        FRetainedBlueprintGeneration::RegisterFunctionLibraryDependency(OwnerClass);
    }
	FString FunctionName = UBlueprintNativizationLibrary::GetUniqueFunctionName(Function, "");

    TArray<FGenerateResultStruct> Targets;
    FGenerationPreparations NewPreparations;
    FString ArrayIteration;
    FString ArrayCondition;
    const FString EvaluationPurpose = NativizationV2Subsystem->InputEvaluationPurpose;
    TGuardValue<FString> InvocationPurpose(NativizationV2Subsystem->InputEvaluationPurpose, EvaluationPurpose);
    UEdGraphPin* ReceiverPin = CallFunctionNode->FindPin(UEdGraphSchema_K2::PN_Self);
    TArray<UK2Node*> ReceiverStack = MacroStack;
    UEdGraphPin* ReceiverSource = nullptr;
    if (ReceiverPin && ReceiverPin->LinkedTo.Num() == 1)
    {
        ReceiverSource = UBlueprintNativizationLibrary::GetFirstNonKnotPin(ReceiverPin, 0, true, ReceiverStack, NativizationV2Subsystem);
    }
    const bool bArrayReceiver = !Function->HasAnyFunctionFlags(FUNC_Static) && ReceiverSource &&
        ReceiverSource->PinType.IsArray() && CallFunctionNode->AllowMultipleSelfs(true);
    if (bArrayReceiver)
    {
        ArrayIteration = NativizationV2Subsystem->GetNodeStateName(Node, TEXT("ReceiverIndex"), true);
        TGuardValue<FString> ConditionPurpose(NativizationV2Subsystem->InputEvaluationPurpose, EvaluationPurpose + TEXT(":ReceiverCondition"));
        const FGenerateResultStruct Array = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, ReceiverPin, 0, MacroStack);
        ArrayCondition = FString::Join(Array.Preparations.Array(), TEXT("\n")) +
            TEXT("\nif (") + ArrayIteration + TEXT(" >= (") + Array.Code + TEXT(").Num())\n{\nbreak;\n}\n");
    }
    if (bArrayReceiver)
    {
        NativizationV2Subsystem->InputEvaluationPurpose = EvaluationPurpose + TEXT(":ReceiverInvocation");
    }
    auto WrapInvocation = [&](const FString& Invocation)
    {
        if (bArrayReceiver)
        {
            return TEXT("for (int32 ") + ArrayIteration + TEXT(" = 0; ; ++") + ArrayIteration + TEXT(")\n{\n") +
                ArrayCondition + Invocation + TEXT("}\n");
        }
        return Invocation;
    };
    if (!Function->HasAnyFunctionFlags(FUNC_Static))
    {
        UEdGraphPin* TargetPin = CallFunctionNode->FindPin(TEXT("self"));
        int32 TargetCount = 1;
        if (TargetPin)
        {
            TargetCount = FMath::Max(1, TargetPin->LinkedTo.Num());
        }
        for (int32 TargetIndex = 0; TargetIndex < TargetCount; ++TargetIndex)
        {
            FGenerateResultStruct Target;
            if (TargetPin)
            {
                Target = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, TargetPin, TargetIndex, MacroStack);
            }
            if (bArrayReceiver)
            {
                const FString ArrayName = NativizationV2Subsystem->GetNodeStateName(Node, TEXT("ReceiverArray"), true);
                const FString TargetName = NativizationV2Subsystem->GetNodeStateName(Node, TEXT("ReceiverObject"), true);
                FEdGraphPinType ElementType = ReceiverSource->PinType;
                ElementType.ContainerType = EPinContainerType::None;
                ElementType.bIsReference = false;
                ElementType.bIsConst = false;
                Target.Preparations.Add(TEXT("auto&& ") + ArrayName + TEXT(" = ") + Target.Code + TEXT(";\n") +
                    UBlueprintNativizationLibrary::GetPinType(ElementType, false) + TEXT(" ") + TargetName +
                    TEXT("{};\nif (") + ArrayName + TEXT(".IsValidIndex(") + ArrayIteration + TEXT("))\n{\n") +
                    TargetName + TEXT(" = ") + ArrayName + TEXT("[") + ArrayIteration + TEXT("];\n}\n"));
                Target.Code = TargetName;
            }
            Target = ResolveMemberCallTarget(CallFunctionNode, Target, NativizationV2Subsystem);
            NewPreparations.Append(Target.Preparations);
            Targets.Add(Target);
        }
    }
    UClass* CallingClass = nullptr;
    if (Targets.Num() == 1 && (Targets[0].Code.IsEmpty() || Targets[0].Code == TEXT("this")))
    {
        CallingClass = CallFunctionNode->GetBlueprint()->GeneratedClass;
    }
    if (!CallFunctionNode->IsA<UK2Node_CallParentFunction>() && FRetainedBlueprintGeneration::UsesReflection(Function, CallingClass))
    {
        if (Function->HasAnyFunctionFlags(FUNC_Static))
        {
            Targets.Add(FGenerateResultStruct(FString()));
        }
        for (const FGenerateResultStruct& Target : Targets)
        {
            FGenerateResultStruct Invocation = FRetainedBlueprintGeneration::Call(CallFunctionNode, Target.Code, MacroStack, NativizationV2Subsystem);
            Invocation.Code = AdaptReturnValue(CallFunctionNode, Invocation.Code, Invocation.Preparations);
            FGenerationPreparations InvocationPreparations = NewPreparations;
            InvocationPreparations.Append(Invocation.Preparations);
            Invocation.Preparations = MoveTemp(InvocationPreparations);
            Content += GenerateNewPreparations(Preparations, Invocation.Preparations);
            Preparations.Append(Invocation.Preparations);
            UEdGraphPin* ReturnPin = CallFunctionNode->GetReturnValuePin();
            if (ReturnPin && UBlueprintNativizationLibrary::CheckAnySubPinLinked(ReturnPin))
            {
                Content += UBlueprintNativizationLibrary::GetUniquePinName(ReturnPin, NativizationV2Subsystem->EntryNodes) + TEXT(" = ") + Invocation.Code + TEXT(";\n");
            }
            else
            {
                Content += Invocation.Code + TEXT(";\n");
            }
        }
        return WrapInvocation(Content);
    }
    TMap<FName, FString> ArgumentOverrides;
    FString BeforeCall;
    FString AfterCall;
    FRetainedBlueprintGeneration::PrepareNativeArguments(CallFunctionNode, MacroStack, NativizationV2Subsystem,
        ArgumentOverrides, NewPreparations, BeforeCall, AfterCall);
	FString Args;
    TArray<FString> ArgumentExpressions;
	BuildArgs(Function->HasAnyFunctionFlags(EFunctionFlags::FUNC_Static), Args, NewPreparations, CallFunctionNode, MacroStack, NativizationV2Subsystem, ArgumentOverrides, &ArgumentExpressions);
	Content += GenerateNewPreparations(Preparations, NewPreparations);
	Preparations.Append(NewPreparations);


    auto GenerateStandardCall = [&]()
    {
        auto AppendInvocation = [&](const FString& OriginalInvocation)
        {
            const FString Invocation = OriginalInvocation;
            Content += BeforeCall;
            UEdGraphPin* ReturnPin = CallFunctionNode->GetReturnValuePin();
            if (ReturnPin && UBlueprintNativizationLibrary::CheckAnySubPinLinked(ReturnPin))
            {
                FGenerationPreparations ReturnPreparations;
                const FString ReturnValue = AdaptReturnValue(CallFunctionNode, Invocation, ReturnPreparations);
                Content += FString::Join(ReturnPreparations.Array(), TEXT(""));
                Content += UBlueprintNativizationLibrary::GetUniquePinName(ReturnPin, NativizationV2Subsystem->EntryNodes) +
                    TEXT(" = ") + ReturnValue + TEXT(";\n");
            }
            else
            {
                Content += Invocation + TEXT(";\n");
            }
            Content += AfterCall;
        };
        if (Function->HasAnyFunctionFlags(FUNC_Static))
        {
            AppendInvocation(GenerateStaticFunctionCall(Function, Args, &ArgumentExpressions, CallFunctionNode));
        }
        else
        {
            for (const FGenerateResultStruct& Target : Targets)
            {
                if (!Function->GetOwnerClass()->HasAnyClassFlags(CLASS_Interface))
                {
                    FString ResultName;
                    UEdGraphPin* ReturnPin = CallFunctionNode->GetReturnValuePin();
                    if (ReturnPin && UBlueprintNativizationLibrary::CheckAnySubPinLinked(ReturnPin))
                    {
                        ResultName = UBlueprintNativizationLibrary::GetUniquePinName(ReturnPin, NativizationV2Subsystem->EntryNodes);
                    }
                    const FString Statement = GenerateInstanceFunctionStatement(CallFunctionNode, Target.Code, Args, ResultName, ArgumentExpressions, NativizationV2Subsystem);
                    if (!Statement.IsEmpty())
                    {
                        Content += BeforeCall + Statement + AfterCall;
                        continue;
                    }
                }
                AppendInvocation(GenerateInstanceFunctionCall(CallFunctionNode, Target.Code, Args));
            }
        }
    };

    GenerateStandardCall();
    return WrapInvocation(Content);
}


void UCallFunctionTranslatorObject::BuildArgs(bool bIsStatic, FString& Code, FGenerationPreparations& Preparation, UK2Node_CallFunction* CallFunctionNode, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem, const TMap<FName, FString>& ArgumentOverrides, TArray<FString>* ArgumentExpressions)
{
	UFunction* Function = UBlueprintNativizationLibrary::GetFunctionDeclaration(CallFunctionNode->GetTargetFunction());
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
    const FString DefaultToSelf = Function->GetMetaData(TEXT("DefaultToSelf"));

	for (TFieldIterator<FProperty> ParamIt(Function); ParamIt && (ParamIt->PropertyFlags & CPF_Parm); ++ParamIt)
	{
        if (const FString* Override = ArgumentOverrides.Find(ParamIt->GetFName()))
        {
            AnswerArguments.Add(*Override);
            continue;
        }
		FString ParamCPPName = ParamIt->GetNameCPP();
		UEdGraphPin* Pin = CallFunctionNode->FindPin(ParamIt->GetFName());

		if (!Pin) continue;

		if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec || Pin == ReturnPin)
		{
			continue;
		}
		if (!bIsStatic && Pin && Pin->PinName == TEXT("self"))
		{
			continue;
		}

        if (bIsStatic && WorldContextPin == Pin && Pin->LinkedTo.IsEmpty())
        {
            FString Context = TEXT("this");
            if (CallFunctionNode->GetBlueprint()->BlueprintType == BPTYPE_FunctionLibrary)
            {
                TArray<UK2Node_FunctionEntry*> Entries;
                CallFunctionNode->GetGraph()->GetNodesOfClass(Entries);
                for (UK2Node_FunctionEntry* Entry : Entries)
                {
                    if (UEdGraphPin* ContextPin = Entry->FindPin(TEXT("__WorldContext")))
                    {
                        Context = UBlueprintNativizationLibrary::GetUniquePinName(ContextPin, NativizationV2Subsystem->EntryNodes);
                        break;
                    }
                }
            }
            if (Context == TEXT("this") && CallFunctionNode->GetBlueprint()->GeneratedClass->HasAnyClassFlags(CLASS_Const) &&
                !Function->GetOwnerClass()->HasAnyClassFlags(CLASS_Native))
            {
                Context = TEXT("const_cast<") + FRetainedBlueprintGeneration::GetClassName(CallFunctionNode->GetBlueprint()->GeneratedClass) + TEXT("*>(this)");
            }
            AnswerArguments.Add(Context);
            continue;
        }
        if (Pin->PinName.ToString() == DefaultToSelf && Pin->LinkedTo.IsEmpty() &&
            !Pin->DefaultObject && Pin->DefaultValue.IsEmpty() &&
            CallFunctionNode->GetBlueprint()->BlueprintType != BPTYPE_FunctionLibrary)
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
            const FName LibraryName = Function->GetOwnerClass()->GetFName();
            const bool bEngineContainerWriter = Function->GetOutermost()->GetName() == TEXT("/Script/Engine") &&
                (LibraryName == TEXT("KismetArrayLibrary") || LibraryName == TEXT("KismetMapLibrary") || LibraryName == TEXT("KismetSetLibrary")) &&
                !Function->HasAnyFunctionFlags(FUNC_BlueprintPure);
            const bool bMutableContainer = Pin->PinType.IsContainer() &&
                (ParamIt->HasAnyPropertyFlags(CPF_OutParm | CPF_ReferenceParm) || bEngineContainerWriter) &&
                (!ParamIt->HasAnyPropertyFlags(CPF_ConstParm) ||
                    bEngineContainerWriter);
            TGuardValue<bool> StorageScope(NativizationV2Subsystem->bRequiresInputStorage, bMutableContainer);
			FGenerateResultStruct InputResultStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(CallFunctionNode, Pin, 0, MacroStack);
			Preparation.Append(InputResultStruct.Preparations);
            InputResultStruct.Code = ConvertPromotableArgument(CallFunctionNode, Pin, *ParamIt,
                InputResultStruct.Code, NativizationV2Subsystem);
            if (FDelegateProperty* DelegateParameter = CastField<FDelegateProperty>(*ParamIt))
            {
                const FString DelegateType = DelegateParameter->GetCPPType(nullptr, CPPF_BlueprintCppBackend);
                const FString SourceName = NativizationV2Subsystem->GetNodeStateName(CallFunctionNode, Pin->PinName.ToString() + TEXT("SourceCallback"), true);
                const FString CallbackName = NativizationV2Subsystem->GetNodeStateName(CallFunctionNode, Pin->PinName.ToString() + TEXT("Callback"), true);
                Preparation.Add(TEXT("FScriptDelegate ") + SourceName + TEXT(" = ") + InputResultStruct.Code + TEXT(";\n") +
                    DelegateType + TEXT(" ") + CallbackName + TEXT(";\n") + CallbackName + TEXT(".BindUFunction(") +
                    SourceName + TEXT(".GetUObject(), ") + SourceName + TEXT(".GetFunctionName());\n"));
                InputResultStruct.Code = CallbackName;
            }
            FByteProperty* ByteParameter = CastField<FByteProperty>(*ParamIt);
            UEnum* Enumeration = Cast<UEnum>(Pin->PinType.PinSubCategoryObject.Get());
            if (ByteParameter && !ByteParameter->Enum && Enumeration &&
                (Enumeration->IsA<UUserDefinedEnum>() || Enumeration->GetCppForm() == UEnum::ECppForm::EnumClass))
            {
                InputResultStruct.Code = TEXT("static_cast<uint8>(") + InputResultStruct.Code + TEXT(")");
            }
            if ((ParamIt->HasAnyPropertyFlags(CPF_OutParm | CPF_ReferenceParm) || Pin->PinType.IsContainer()) &&
                !ParamIt->HasAnyPropertyFlags(CPF_ConstParm) && Pin->Direction == EGPD_Input)
            {
                UEdGraphPin* SourcePin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Pin, 0, true, MacroStack, NativizationV2Subsystem);
                if (!IsMutableArgumentSource(SourcePin, MacroStack, NativizationV2Subsystem))
                {
                    FEdGraphPinType ValueType = Pin->PinType;
                    ValueType.bIsReference = false;
                    ValueType.bIsConst = false;
                    const FString Temporary = NativizationV2Subsystem->GetNodeStateName(CallFunctionNode, TEXT("Argument") + Pin->PinName.ToString());
                    InputResultStruct.Preparations.Add(UBlueprintNativizationLibrary::GetPinType(ValueType, false) + TEXT(" ") + Temporary +
                        TEXT(" = ") + InputResultStruct.Code + TEXT(";\n"));
                    Preparation.Append(InputResultStruct.Preparations);
                    InputResultStruct.Code = Temporary;
                }
            }
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
    if (ArgumentExpressions)
    {
        *ArgumentExpressions = AnswerArguments;
    }
};


FGenerateResultStruct UCallFunctionTranslatorObject::GenerateInputParameterCodeForNode(
    UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem)
{
    Pin = UBlueprintNativizationLibrary::GetParentPin(Pin);
    UK2Node_CallFunction* Call = CastChecked<UK2Node_CallFunction>(Node);
    if (!Call->IsNodePure())
    {
        return UBlueprintNativizationLibrary::GetUniquePinName(UBlueprintNativizationLibrary::GetParentPin(Pin), Subsystem->EntryNodes);
    }
    UEdGraphPin* ReturnPin = Call->GetReturnValuePin();
    if (const FGenerateResultStruct* Cached = Subsystem->PureFunctionCalls.Find(Subsystem->GetEvaluationKey(Node)))
    {
        FGenerateResultStruct Result = *Cached;
        if (Pin != ReturnPin)
        {
            Result.Code = UBlueprintNativizationLibrary::GetUniquePinName(Pin, Subsystem->EntryNodes);
        }
        return Result;
    }
    UFunction* OriginalFunction = Call->GetTargetFunction();
    if (OriginalFunction && OriginalFunction->HasAnyFunctionFlags(FUNC_Static))
    {
        FRetainedBlueprintGeneration::RegisterFunctionLibraryDependency(OriginalFunction->GetOwnerClass());
        FRetainedBlueprintGeneration::RegisterFunctionLibraryDependency(
            Call->FunctionReference.GetMemberParentClass(Call->GetBlueprint()->GeneratedClass));
    }
    UFunction* Function = UBlueprintNativizationLibrary::GetFunctionDeclaration(OriginalFunction);
    const FString FunctionName = UBlueprintNativizationLibrary::GetUniqueFunctionName(Function, TEXT(""));
    TArray<UEdGraphPin*> OperatorInputs;
    UK2Node_PromotableOperator* Promotable = Cast<UK2Node_PromotableOperator>(Call);
    UK2Node_CommutativeAssociativeBinaryOperator* Commutative = Cast<UK2Node_CommutativeAssociativeBinaryOperator>(Call);
    if ((Promotable && !FTypePromotion::IsComparisonFunc(Function)) || Commutative)
    {
        for (UEdGraphPin* Input : Call->Pins)
        {
            if (Input->Direction == EGPD_Input && !Input->ParentPin && !Input->bHidden &&
                Input->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec && Input->PinName != UEdGraphSchema_K2::PN_Self)
            {
                OperatorInputs.Add(Input);
            }
        }
    }
    if (OperatorInputs.Num() > 2)
    {
        FGenerateResultStruct Result;
        const FGenerateResultStruct FirstValue = Subsystem->GenerateInputParameterCodeForNode(Node, OperatorInputs[0], 0, MacroStack);
        Result.Preparations.Append(FirstValue.Preparations);
        FString PreviousValue = Subsystem->GetNodeStateName(Node, TEXT("OperatorArgument0"), true);
        Result.Preparations.Add(TEXT("const auto ") + PreviousValue + TEXT(" = ") + FirstValue.Code + TEXT(";\n"));
        UFunction* StepFunction = Function;
        for (int32 InputIndex = 1; InputIndex < OperatorInputs.Num(); ++InputIndex)
        {
            if (Promotable && InputIndex > 1)
            {
                const TArray<UEdGraphPin*> Types = {ReturnPin, OperatorInputs[InputIndex], ReturnPin};
                if (UFunction* Matching = FTypePromotion::FindBestMatchingFunc(Promotable->GetOperationName(), Types))
                {
                    StepFunction = Matching;
                }
            }
            TArray<FProperty*> Parameters;
            for (TFieldIterator<FProperty> Parameter(StepFunction); Parameter; ++Parameter)
            {
                if (Parameter->HasAnyPropertyFlags(CPF_Parm) && !Parameter->HasAnyPropertyFlags(CPF_ReturnParm))
                {
                    Parameters.Add(*Parameter);
                }
            }
            if (Parameters.Num() != 2)
            {
                FNativeApiCatalog::AddError(TEXT("A variadic operator requires a binary function: ") + Call->GetPathName());
                return {};
            }
            UEdGraphPin* PreviousType = ReturnPin;
            if (InputIndex == 1)
            {
                PreviousType = OperatorInputs[0];
            }
            const FString Left = ConvertPromotableArgument(Call, PreviousType, Parameters[0], PreviousValue, Subsystem);
            const FGenerateResultStruct Value = Subsystem->GenerateInputParameterCodeForNode(Node, OperatorInputs[InputIndex], 0, MacroStack);
            Result.Preparations.Append(Value.Preparations);
            const FString ArgumentName = Subsystem->GetNodeStateName(Node, TEXT("OperatorArgument") + FString::FromInt(InputIndex), true);
            Result.Preparations.Add(TEXT("const auto ") + ArgumentName + TEXT(" = ") + Value.Code + TEXT(";\n"));
            const FString Right = ConvertPromotableArgument(Call, OperatorInputs[InputIndex], Parameters[1], ArgumentName, Subsystem);
            const TArray<FString> Arguments = {Left, Right};
            const FString Invocation = GenerateStaticFunctionCall(StepFunction, FString::Join(Arguments, TEXT(", ")), &Arguments);
            PreviousValue = Subsystem->GetNodeStateName(Node, TEXT("OperatorStep") + FString::FromInt(InputIndex), true);
            Result.Preparations.Add(TEXT("const auto ") + PreviousValue + TEXT(" = ") + Invocation + TEXT(";\n"));
        }
        Result.Code = PreviousValue;
        Subsystem->PureFunctionCalls.Add(Subsystem->GetEvaluationKey(Node), Result);
        return Result;
    }

    FString ProjectionInput;
    TArray<TPair<FString, FString>> ProjectionOutputs;
    if (FNativeFunctionExpressions::Projection(Function, ProjectionInput, ProjectionOutputs))
    {
        FGenerateResultStruct Result = Subsystem->GenerateInputParameterCodeForNode(Node, Call->FindPinChecked(ProjectionInput), 0, MacroStack);
        const FString SourceName = Subsystem->GetNodeStateName(Node, TEXT("StructureSource"), true);
        FString Statements = TEXT("{\nconst auto& ") + SourceName + TEXT(" = ") + Result.Code + TEXT(";\n");
        for (const auto& Output : ProjectionOutputs)
        {
            UEdGraphPin* OutputPin = Call->FindPinChecked(Output.Key);
            if (UBlueprintNativizationLibrary::CheckAnySubPinLinked(OutputPin))
            {
                Statements += UBlueprintNativizationLibrary::GetUniquePinName(OutputPin, Subsystem->EntryNodes) +
                    TEXT(" = ") + SourceName + TEXT(".") + Output.Value + TEXT(";\n");
            }
            else if (Output.Value.Contains(TEXT("(")))
            {
                const FString EvaluationName = Subsystem->GetNodeStateName(Node, Output.Key + TEXT("Evaluation"), true);
                Statements += TEXT("[[maybe_unused]] const auto ") + EvaluationName + TEXT(" = ") + SourceName + TEXT(".") + Output.Value + TEXT(";\n");
            }
        }
        Result.Preparations.Add(Statements + TEXT("}\n"));
        Result.Code = UBlueprintNativizationLibrary::GetUniquePinName(Pin, Subsystem->EntryNodes);
        Subsystem->PureFunctionCalls.Add(Subsystem->GetEvaluationKey(Node), Result);
        return Result;
    }
    FString Arguments;
    FGenerationPreparations Preparations;
    FString FunctionExpression;
    FString InstanceTarget;
    if (Function->HasAnyFunctionFlags(FUNC_Static))
    {
        FRetainedBlueprintGeneration::RegisterFunctionLibraryDependency(Function->GetOwnerClass());
        FunctionExpression = UBlueprintNativizationLibrary::GetUniqueFieldName(Function->GetOwnerClass()) + TEXT("::") + FunctionName;
    }
    else
    {
        const FGenerateResultStruct Target = ResolveMemberCallTarget(Call, Subsystem->GenerateInputParameterCodeForNode(Node, Call->FindPin(TEXT("self")), 0, MacroStack), Subsystem);
        Preparations.Append(Target.Preparations);
        InstanceTarget = Target.Code;
        FunctionExpression = FunctionName;
        if (!Target.Code.IsEmpty())
        {
            FunctionExpression = Target.Code + TEXT("->") + FunctionName;
        }
    }
    UClass* CallingClass = nullptr;
    if (InstanceTarget.IsEmpty() || InstanceTarget == TEXT("this"))
    {
        CallingClass = Call->GetBlueprint()->GeneratedClass;
    }
    if (!Call->IsA<UK2Node_CallParentFunction>() && FRetainedBlueprintGeneration::UsesReflection(Function, CallingClass))
    {
        FGenerateResultStruct Invocation = FRetainedBlueprintGeneration::Call(Call, InstanceTarget, MacroStack, Subsystem);
        Invocation.Code = AdaptReturnValue(Call, Invocation.Code, Invocation.Preparations);
        FGenerationPreparations InvocationPreparations = Preparations;
        InvocationPreparations.Append(Invocation.Preparations);
        Invocation.Preparations = MoveTemp(InvocationPreparations);
        FString ResultName;
        if (ReturnPin)
        {
            ResultName = Subsystem->GetNodeStateName(Node, GetCallResultPurpose(Call), true);
            Invocation.Preparations.Add(TEXT("const auto ") + ResultName + TEXT(" = ") + Invocation.Code + TEXT(";\n"));
        }
        else
        {
            Invocation.Preparations.Add(Invocation.Code + TEXT(";\n"));
        }
        FGenerateResultStruct Result(ResultName, Invocation.Preparations);
        Subsystem->PureFunctionCalls.Add(Subsystem->GetEvaluationKey(Node), Result);
        if (Pin != ReturnPin)
        {
            Result.Code = UBlueprintNativizationLibrary::GetUniquePinName(Pin, Subsystem->EntryNodes);
        }
        return Result;
    }
    TMap<FName, FString> ArgumentOverrides;
    FString BeforeCall;
    FString AfterCall;
    FRetainedBlueprintGeneration::PrepareNativeArguments(Call, MacroStack, Subsystem,
        ArgumentOverrides, Preparations, BeforeCall, AfterCall);
    TArray<FString> ArgumentExpressions;
    BuildArgs(Function->HasAnyFunctionFlags(FUNC_Static), Arguments, Preparations, Call, MacroStack, Subsystem, ArgumentOverrides, &ArgumentExpressions);
    FString Invocation = FunctionExpression + TEXT("(") + Arguments + TEXT(")");
    if (Function->HasAnyFunctionFlags(FUNC_Static))
    {
        Invocation = GenerateStaticFunctionCall(Function, Arguments, &ArgumentExpressions, Call);
    }
    else
    {
        Invocation = GenerateInstanceFunctionCall(Call, InstanceTarget, Arguments);
    }
    if (Function->GetOwnerClass() == UKismetTextLibrary::StaticClass() && Function->GetFName() == TEXT("Conv_NumericPropertyToText"))
    {
        UEdGraphPin* Value = Call->FindPinChecked(TEXT("Value"));
        if (UEnum* Enumeration = Cast<UEnum>(Value->PinType.PinSubCategoryObject.Get()))
        {
            Invocation = FString::Format(TEXT("StaticEnum<{0}>()->GetDisplayNameTextByValue(static_cast<int64>({1}))"),
                { UBlueprintNativizationLibrary::GetUniqueFieldName(Enumeration), Arguments });
        }
        else if (Value->PinType.PinCategory == UEdGraphSchema_K2::PC_Real)
        {
            Invocation = TEXT("FText::AsNumber(static_cast<double>(") + Arguments + TEXT("))");
        }
        else
        {
            Invocation = TEXT("FText::AsNumber(static_cast<int64>(") + Arguments + TEXT("))");
        }
    }
    bool bSingleCondition = Subsystem->DataConsumer && Subsystem->DataConsumer->IsA<UK2Node_IfThenElse>() &&
        ReturnPin && ReturnPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean && ReturnPin->LinkedTo.Num() == 1;
    for (TFieldIterator<FProperty> Parameter(Function); bSingleCondition && Parameter; ++Parameter)
    {
        if (Parameter->HasAnyPropertyFlags(CPF_OutParm) && !Parameter->HasAnyPropertyFlags(CPF_ReturnParm | CPF_ConstParm))
        {
            bSingleCondition = false;
        }
    }
    if (Pin == ReturnPin && BeforeCall.IsEmpty() && AfterCall.IsEmpty() &&
        (Function->HasAnyFunctionFlags(FUNC_Static) || InstanceTarget.IsEmpty() || InstanceTarget == TEXT("this")) &&
        (bSingleCondition || CanInlineArithmetic(Call, Subsystem->DataConsumer)))
    {
        Invocation = AdaptReturnValue(Call, Invocation, Preparations);
        FGenerateResultStruct Result(Invocation, Preparations);
        Subsystem->PureFunctionCalls.Add(Subsystem->GetEvaluationKey(Node), Result);
        return Result;
    }
    FString ResultName;
    if (ReturnPin)
    {
        ResultName = Subsystem->GetNodeStateName(Node, GetCallResultPurpose(Call), true);
        FString Statement;
        Statement = GenerateInstanceFunctionStatement(Call, InstanceTarget, Arguments, ResultName, ArgumentExpressions, Subsystem);
        if (!Statement.IsEmpty())
        {
            Preparations.Add(UBlueprintNativizationLibrary::GetPinType(ReturnPin->PinType, false) + TEXT(" ") + ResultName + TEXT("{};\n") +
                BeforeCall + Statement + AfterCall);
        }
        else
        {
            FGenerationPreparations ReturnPreparations;
            Invocation = AdaptReturnValue(Call, Invocation, ReturnPreparations);
            Preparations.Add(BeforeCall + FString::Join(ReturnPreparations.Array(), TEXT("")) +
                FString::Format(TEXT("const auto {0} = {1};\n"), { ResultName, Invocation }) + AfterCall);
        }
    }
    else
    {
        const FString Statement = GenerateInstanceFunctionStatement(Call, InstanceTarget, Arguments, FString(), ArgumentExpressions, Subsystem);
        if (!Statement.IsEmpty())
        {
            Preparations.Add(BeforeCall + Statement + AfterCall);
        }
        else
        {
            Preparations.Add(BeforeCall + Invocation + TEXT(";\n") + AfterCall);
        }
    }
    FGenerateResultStruct Result(ResultName, Preparations);
    Subsystem->PureFunctionCalls.Add(Subsystem->GetEvaluationKey(Node), Result);
    if (Pin != ReturnPin)
    {
        Result.Code = UBlueprintNativizationLibrary::GetUniquePinName(Pin, Subsystem->EntryNodes);
    }
    return Result;
}

TSet<FString> UCallFunctionTranslatorObject::GenerateLocalVariables(UK2Node* InputNode, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	TSet<FString> Contents;
	Contents.Append(Super::GenerateLocalVariables(InputNode, MacroStack, NativizationV2Subsystem));

	UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(InputNode);

	TArray<UEdGraphPin*> PropertyPins;

	UFunction* Function = UBlueprintNativizationLibrary::GetFunctionDeclaration(CallFunctionNode->GetTargetFunction());
	if (Function)
	{
        FString ProjectionInput;
        TArray<TPair<FString, FString>> ProjectionOutputs;
        const bool bProjection = FNativeFunctionExpressions::Projection(Function, ProjectionInput, ProjectionOutputs);
		for (TFieldIterator<FProperty> ParamIt(Function); ParamIt && (ParamIt->PropertyFlags & CPF_Parm); ++ParamIt)
		{
			FProperty* Property = *ParamIt;

			if (!(Property->HasAnyPropertyFlags(CPF_OutParm) && (Property->HasAnyPropertyFlags(CPF_ReferenceParm) || !Property->HasAnyPropertyFlags(CPF_ReturnParm))))
			{
				continue;
			}

			UEdGraphPin* OutputPin = UBlueprintNativizationLibrary::GetPinByName(CallFunctionNode->Pins, Property->GetNameCPP());
			if (!OutputPin || OutputPin->Direction != EEdGraphPinDirection::EGPD_Output)
			{
				continue;
			}
			if (OutputPin == CallFunctionNode->GetReturnValuePin())
			{
				continue;
			}
            if (bProjection && !UBlueprintNativizationLibrary::CheckAnySubPinLinked(OutputPin))
            {
                continue;
            }

			FGenerateFunctionStruct InputGenerateFunctionStruct;
			UBlueprintNativizationLibrary::GetEntryByNodeAndEntryNodes(InputNode,NativizationV2Subsystem->EntryNodes, InputGenerateFunctionStruct);

			if (FGenerationGraphContext::IsFunctionGraph(InputNode) || CallFunctionNode->IsNodePure() || !UBlueprintNativizationLibrary::CheckAnySubPinLinked(OutputPin) ||
				UBlueprintNativizationLibrary::IsPinAllLinkInCurrentFunctionNode(OutputPin, InputGenerateFunctionStruct.Node, NativizationV2Subsystem->EntryNodes))
			{
				PropertyPins.Add(OutputPin);
			}
		}
		TArray<UEdGraphPin*> RootPropertyPins = UBlueprintNativizationLibrary::GetParentPins(PropertyPins);

		for (UEdGraphPin* RootPropertyPin : RootPropertyPins)
		{
            if (GEngine->GetEngineSubsystem<UBlueprintNativizationUniqueNameSubsystem>()->IsPersistentPinVariable(RootPropertyPin))
            {
                continue;
            }
			FString VarName = UBlueprintNativizationLibrary::GetUniquePinName(RootPropertyPin, NativizationV2Subsystem->EntryNodes);
			Contents.Add(FString::Format(TEXT("{0} {1} = {2};\n"), { *UBlueprintNativizationLibrary::GetPinType(RootPropertyPin->PinType, false), *VarName, UBlueprintNativizationLibrary::GenerateOneLineDefaultConstructor(RootPropertyPin->GetName(), RootPropertyPin->PinType, RootPropertyPin->DefaultValue, RootPropertyPin->DefaultObject) }));
		}
	}

	return Contents;
}

TSet<FString> UCallFunctionTranslatorObject::GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	TSet<FString> Includes;
    if (UInputEventTranslatorObject::HasInputBinding(Node))
    {
        Includes.Append(GetMutableDefault<UInputEventTranslatorObject>()->GenerateCppIncludeInstructions(Node, NativizationV2Subsystem));
    }

    bool bReflectionOnlyStaticCall = false;
	if (UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(Node))
	{
		if (UFunction* Function = UBlueprintNativizationLibrary::GetFunctionDeclaration(CallFunctionNode->GetTargetFunction()))
		{
			if (Function->IsNative() || Function->GetOwnerClass()->HasAnyClassFlags(CLASS_Interface) || FRetainedBlueprintGeneration::UsesReflection(Function))
			{
                bReflectionOnlyStaticCall = Function->HasAnyFunctionFlags(FUNC_Static) && FRetainedBlueprintGeneration::UsesReflection(Function);
				if (UClass* OwnerClass = Function->GetOwnerClass())
				{
					FString IncludePath;
					if (!bReflectionOnlyStaticCall && NativizationV2Subsystem->GetIncludeFromField(OwnerClass, IncludePath))
					{
						Includes.Add(IncludePath);
					}
				}
			}
		}
	}

    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (bReflectionOnlyStaticCall && Pin->PinName == UEdGraphSchema_K2::PN_Self)
        {
            continue;
        }
        FString IncludePath;
        UField* PinField = Cast<UField>(Pin->PinType.PinSubCategoryObject.Get());
        if (PinField && NativizationV2Subsystem->GetIncludeFromField(PinField, IncludePath))
        {
            Includes.Add(IncludePath);
        }
        UField* DefaultField = Cast<UField>(Pin->DefaultObject);
        if (DefaultField && NativizationV2Subsystem->GetIncludeFromField(DefaultField, IncludePath))
        {
            Includes.Add(IncludePath);
        }
    }

	return Includes;
}

TSet<FString> UCallFunctionTranslatorObject::GenerateCSPrivateDependencyModuleNames(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	TSet<FString> ModuleSet;
	if (UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(Node))
	{
		if (UFunction* Function = UBlueprintNativizationLibrary::GetFunctionDeclaration(CallFunctionNode->GetTargetFunction()))
		{
            FString Expression;
            TSet<FString> Includes;
            TSet<FString> ForwardingModules;
            if (FNativeApiCatalog::FindPublicForwarding(Function, Expression, Includes, ForwardingModules))
            {
                for (const FString& Dependency : ForwardingModules)
                {
                    ModuleSet.Add(FString::Format(TEXT("\"{0}\""), {Dependency}));
                }
                return ModuleSet;
            }
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

FString UCallFunctionTranslatorObject::AdaptReturnValue(UK2Node_CallFunction* Call, const FString& Expression,
    FGenerationPreparations& Preparations) const
{
    UEdGraphPin* ReturnPin = Call->GetReturnValuePin();
    FProperty* Property = UBlueprintNativizationLibrary::GetFunctionDeclaration(Call->GetTargetFunction())->GetReturnProperty();
    if (ReturnPin && ReturnPin->PinType.IsArray() && ReturnPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object)
    {
        FArrayProperty* Array = CastField<FArrayProperty>(Property);
        FObjectPropertyBase* Element = nullptr;
        if (Array)
        {
            Element = CastField<FObjectPropertyBase>(Array->Inner);
        }
        UClass* OutputClass = Cast<UClass>(ReturnPin->PinType.PinSubCategoryObject.Get());
        if (Element && OutputClass && OutputClass != Element->PropertyClass && OutputClass->IsChildOf(Element->PropertyClass))
        {
            UNativizationV2Subsystem* Subsystem = GEngine->GetEngineSubsystem<UNativizationV2Subsystem>();
            const FString ResultName = Subsystem->GetNodeStateName(Call, TEXT("ConvertedResult"), true);
            Preparations.Add(UBlueprintNativizationLibrary::GetPinType(ReturnPin->PinType, false) + TEXT(" ") +
                ResultName + TEXT(";\n{\nconst auto SourceValues = ") + Expression + TEXT(";\n") +
                ResultName + TEXT(".Reserve(SourceValues.Num());\nfor (auto* ElementValue : SourceValues)\n{\n") +
                ResultName + TEXT(".Add(Cast<") + FRetainedBlueprintGeneration::GetClassName(OutputClass) +
                TEXT(">(ElementValue));\n}\n}\n"));
            return ResultName;
        }
    }
    FObjectPropertyBase* ReturnProperty = CastField<FObjectPropertyBase>(Property);
    if (ReturnPin && ReturnPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object && ReturnProperty)
    {
        FString ObjectExpression = Expression;
        UFunction* Function = UBlueprintNativizationLibrary::GetFunctionDeclaration(Call->GetTargetFunction());
        if (Function->GetOwnerClass()->HasAnyClassFlags(CLASS_Native) && !UBlueprintNativizationLibrary::GetPinType(ReturnPin->PinType, false).StartsWith(TEXT("const ")))
        {
            if (const TSharedPtr<FJsonObject> OwnerType = FNativeApiCatalog::FindType(Function->GetOwnerClass()))
            {
                for (const TSharedPtr<FJsonValue>& Value : OwnerType->GetArrayField(TEXT("Callables")))
                {
                    const TSharedPtr<FJsonObject> Callable = Value->AsObject();
                    if (Callable->GetStringField(TEXT("Name")) != Function->GetName())
                    {
                        continue;
                    }
                    FString ReturnType = Callable->GetStringField(TEXT("ReturnType"));
                    ReturnType.ReplaceInline(TEXT(" "), TEXT(""));
                    if (ReturnType.Contains(TEXT("*")) &&
                        (ReturnType.StartsWith(TEXT("const")) || ReturnType.Contains(TEXT("const*"))))
                    {
                        ObjectExpression = FString::Format(TEXT("const_cast<{0}*>({1})"),
                            {FRetainedBlueprintGeneration::GetClassName(ReturnProperty->PropertyClass), Expression});
                        break;
                    }
                }
            }
        }
        UClass* OutputClass = Cast<UClass>(ReturnPin->PinType.PinSubCategoryObject.Get());
        if (OutputClass && OutputClass != ReturnProperty->PropertyClass && OutputClass->IsChildOf(ReturnProperty->PropertyClass))
        {
            return FString::Format(TEXT("Cast<{0}>({1})"), { FRetainedBlueprintGeneration::GetClassName(OutputClass), ObjectExpression });
        }
        return ObjectExpression;
    }
    return Expression;
}

FString UCallFunctionTranslatorObject::GenerateInstanceFunctionStatement(UK2Node_CallFunction* Call,
    const FString& Target, const FString& Arguments, const FString& ResultName,
    const TArray<FString>& ArgumentExpressions, UNativizationV2Subsystem* Subsystem) const
{
    UFunction* Function = UBlueprintNativizationLibrary::GetFunctionDeclaration(Call->GetTargetFunction());
    if (Target.IsEmpty() || Target == TEXT("this") || Function->HasAnyFunctionFlags(FUNC_Static) ||
        Function->GetOwnerClass()->HasAnyClassFlags(CLASS_Interface) || Call->IsA<UK2Node_CallParentFunction>())
    {
        return FString();
    }
    const FString Invocation = TEXT("FunctionTarget->") +
        UBlueprintNativizationLibrary::GetUniqueFunctionName(Function, TEXT("")) + TEXT("(") + Arguments + TEXT(")");
    FString Body = Invocation;
    if (!ResultName.IsEmpty())
    {
        FGenerationPreparations ReturnPreparations;
        const FString ReturnValue = AdaptReturnValue(Call, Invocation, ReturnPreparations);
        Body = FString::Join(ReturnPreparations.Array(), TEXT("")) + ResultName + TEXT(" = ") + ReturnValue;
    }
    if (Subsystem->ValidatedCallNode == Call && Subsystem->ValidatedCallReceiver == Target && ArgumentExpressions.IsEmpty())
    {
        Body.ReplaceInline(TEXT("FunctionTarget->"), *(Target + TEXT("->")), ESearchCase::CaseSensitive);
        return Body + TEXT(";\n");
    }
    bool bIsDirectTarget = FChar::IsAlpha(Target[0]) || Target[0] == TEXT('_');
    for (const TCHAR Character : Target)
    {
        if (!FChar::IsAlnum(Character) && Character != TEXT('_'))
        {
            bIsDirectTarget = false;
            break;
        }
    }
    FString Statement;
    if (bIsDirectTarget)
    {
        Body.ReplaceInline(TEXT("FunctionTarget->"), *(Target + TEXT("->")), ESearchCase::CaseSensitive);
        Statement = TEXT("if (IsValid(") + Target + TEXT("))\n{\n") + Body + TEXT(";\n}\n");
    }
    else
    {
        Statement = TEXT("if (const auto FunctionTarget = ") + Target +
            TEXT("; IsValid(FunctionTarget))\n{\n") + Body + TEXT(";\n}\n");
    }
    if (!ResultName.IsEmpty() && !Call->IsNodePure())
    {
        Statement += TEXT("else\n{\n") + ResultName + TEXT(" = {};\n}\n");
    }
    return Statement;
}

FString UCallFunctionTranslatorObject::GenerateInstanceFunctionCall(UK2Node_CallFunction* Call,
    const FString& Target, const FString& Arguments) const
{
    UFunction* Function = UBlueprintNativizationLibrary::GetFunctionDeclaration(Call->GetTargetFunction());
    if (Function->GetOwnerClass()->HasAnyClassFlags(CLASS_Interface))
    {
        FString Receiver = Target;
        if (Receiver.IsEmpty())
        {
            Receiver = TEXT("this");
        }
        if (UEdGraphPin* SelfPin = Call->FindPin(UEdGraphSchema_K2::PN_Self))
        {
            TArray<UK2Node*> MacroStack;
            UEdGraphPin* SourcePin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(SelfPin, 0, true, MacroStack,
                GEngine->GetEngineSubsystem<UNativizationV2Subsystem>());
            if ((SourcePin && SourcePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Interface) ||
                (!SourcePin && Receiver != TEXT("this") && SelfPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Interface))
            {
                Receiver = TEXT("(") + Receiver + TEXT(").GetObject()");
            }
        }
        const FString InterfaceName = UBlueprintNativizationLibrary::GetUniqueIInterfaceFieldName(Function->GetOwnerClass());
        const FString MethodName = UBlueprintNativizationLibrary::GetUniqueFunctionName(Function, TEXT(""));
        FString ReturnType = TEXT("void");
        FString ReturnPrefix;
        FString Fallback;
        if (FProperty* ReturnProperty = Function->GetReturnProperty())
        {
            ReturnType = UBlueprintNativizationLibrary::GetPropertyType(ReturnProperty);
            ReturnPrefix = TEXT("return ");
            Fallback = TEXT("return {};\n");
        }
        const UClass* InterfaceClass = Function->GetOwnerClass();
        const bool bGeneratedInterfaceEvent = !InterfaceClass->HasAnyClassFlags(CLASS_Native) &&
            !FRetainedBlueprintGeneration::IsRetained(Function->GetOwnerClass()) &&
            UNativizationV2Subsystem::ShouldGenerateBlueprintNativeEvent(Function);
        if (Function->HasAnyFunctionFlags(FUNC_Event) || bGeneratedInterfaceEvent)
        {
            FString ExecuteArguments = TEXT("InterfaceObject");
            if (!Arguments.IsEmpty())
            {
                ExecuteArguments += TEXT(", ") + Arguments;
            }
            const FString InterfaceClassName = UBlueprintNativizationLibrary::GetUniqueUInterfaceFieldName(Function->GetOwnerClass());
            return TEXT("([&]() -> ") + ReturnType + TEXT("\n{\nconst auto InterfaceObject = ") + Receiver +
                TEXT(";\nif (IsValid(InterfaceObject) && InterfaceObject->GetClass()->ImplementsInterface(") +
                InterfaceClassName + TEXT("::StaticClass()))\n{\n") + ReturnPrefix + InterfaceName + TEXT("::Execute_") +
                MethodName + TEXT("(") + ExecuteArguments + TEXT(");\n}\n") + Fallback + TEXT("}())");
        }
        return TEXT("([&]() -> ") + ReturnType + TEXT("\n{\nif (auto* InterfaceTarget = Cast<") + InterfaceName + TEXT(">(") + Receiver +
            TEXT("))\n{\n") + ReturnPrefix + TEXT("InterfaceTarget->") + MethodName + TEXT("(") + Arguments + TEXT(");\n}\n") + Fallback + TEXT("}())");
    }
    const FString Invocation = UBlueprintNativizationLibrary::GetUniqueFunctionName(Function, TEXT("")) +
        TEXT("(") + Arguments + TEXT(")");
    if (Target.IsEmpty() || Target == TEXT("this"))
    {
        return Invocation;
    }
    FString ReturnType = TEXT("void");
    FString Body = TEXT("FunctionTarget->") + Invocation + TEXT(";\n");
    FString Fallback;
    if (FProperty* ReturnProperty = Function->GetReturnProperty())
    {
        ReturnType = UBlueprintNativizationLibrary::GetPropertyType(ReturnProperty);
        if (ReturnProperty->IsA<FObjectPropertyBase>())
        {
            FGenerationPreparations ReturnPreparations;
            const FString ReturnValue = AdaptReturnValue(Call, TEXT("FunctionTarget->") + Invocation, ReturnPreparations);
            Body = FString::Join(ReturnPreparations.Array(), TEXT("")) + TEXT("return ") + ReturnValue + TEXT(";\n");
        }
        else
        {
            Body = TEXT("return ") + Body;
        }
        Fallback = TEXT("return {};\n");
    }
    return TEXT("([&]() -> ") + ReturnType + TEXT("\n{\nconst auto FunctionTarget = ") + Target +
        TEXT(";\nif (IsValid(FunctionTarget))\n{\n") + Body + TEXT("}\n") + Fallback + TEXT("}())");
}
