#include "BlueprintConstructionCompatibility.h"
#include "BlueprintNativizationLibrary.h"
#include "Components/SceneComponent.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "K2Node_AddComponent.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CallParentFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/UnrealType.h"

class FConstructionComponentResolver
{
public:
    static USceneComponent* FindRoot(UBlueprint* Blueprint)
    {
        if (Blueprint->GeneratedClass)
        {
            if (AActor* Defaults = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject()))
            {
                if (Defaults->GetRootComponent())
                {
                    return Defaults->GetRootComponent();
                }
            }
        }
        for (UBlueprint* Owner = Blueprint; Owner; Owner = GetParent(Owner))
        {
            if (Owner->SimpleConstructionScript)
            {
                USCS_Node* RootNode = nullptr;
                USceneComponent* Root = Owner->SimpleConstructionScript->GetSceneRootComponentTemplate(false, &RootNode);
                if (RootNode && Blueprint->GeneratedClass)
                {
                    return Cast<USceneComponent>(RootNode->GetActualComponentTemplate(Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass)));
                }
                if (Root)
                {
                    return Root;
                }
            }
        }
        return nullptr;
    }

    static UBlueprint* GetParent(UBlueprint* Blueprint)
    {
        if (Blueprint && Blueprint->ParentClass)
        {
            return Cast<UBlueprint>(Blueprint->ParentClass->ClassGeneratedBy);
        }
        return nullptr;
    }

    static USceneComponent* ResolvePin(UBlueprint* Blueprint, UEdGraphPin* Input)
    {
        if (!Input || Input->LinkedTo.Num() != 1)
        {
            return nullptr;
        }
        TArray<UK2Node*> MacroStack;
        UEdGraphPin* Output = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Input, 0, true, MacroStack, nullptr);
        if (!Output)
        {
            return nullptr;
        }
        if (UK2Node_AddComponent* Creation = Cast<UK2Node_AddComponent>(Output->GetOwningNode()))
        {
            return Cast<USceneComponent>(Creation->GetTemplateFromNode());
        }
        UK2Node_VariableGet* Variable = Cast<UK2Node_VariableGet>(Output->GetOwningNode());
        if (!Variable || !Variable->VariableReference.IsSelfContext())
        {
            return nullptr;
        }
        const FName VariableName = Variable->VariableReference.GetMemberName();
        for (UBlueprint* Owner = Blueprint; Owner; Owner = GetParent(Owner))
        {
            if (Owner->SimpleConstructionScript)
            {
                if (USCS_Node* ComponentNode = Owner->SimpleConstructionScript->FindSCSNode(VariableName))
                {
                    return Cast<USceneComponent>(ComponentNode->GetActualComponentTemplate(Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass)));
                }
            }
        }
        FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Blueprint->GeneratedClass, VariableName);
        if (Property)
        {
            return Cast<USceneComponent>(Property->GetObjectPropertyValue_InContainer(Blueprint->GeneratedClass->GetDefaultObject()));
        }
        return nullptr;
    }

    static UEdGraph* FindGraph(UBlueprint* Blueprint, FName Name)
    {
        for (UBlueprint* Owner = Blueprint; Owner; Owner = GetParent(Owner))
        {
            TArray<UEdGraph*> Graphs;
            Owner->GetAllGraphs(Graphs);
            for (UEdGraph* Graph : Graphs)
            {
                if (Graph->GetFName() == Name)
                {
                    return Graph;
                }
            }
        }
        return nullptr;
    }

    static bool RequestsStaticMobility(UBlueprint* Blueprint, UEdGraphPin* Input)
    {
        if (!Input)
        {
            return false;
        }
        if (Input->LinkedTo.IsEmpty())
        {
            return Input->DefaultValue == TEXT("Static");
        }
        if (Input->LinkedTo.Num() != 1)
        {
            return false;
        }
        TArray<UK2Node*> MacroStack;
        UEdGraphPin* Source = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Input, 0, true, MacroStack, nullptr);
        UK2Node_VariableGet* Variable = nullptr;
        if (Source)
        {
            Variable = Cast<UK2Node_VariableGet>(Source->GetOwningNode());
        }
        if (!Variable || !Variable->VariableReference.IsSelfContext() || Variable->VariableReference.IsLocalScope())
        {
            return false;
        }
        FProperty* Property = FindFProperty<FProperty>(Blueprint->GeneratedClass, Variable->VariableReference.GetMemberName());
        FNumericProperty* Numeric = CastField<FNumericProperty>(Property);
        UEnum* MobilityEnum = nullptr;
        if (FByteProperty* Byte = CastField<FByteProperty>(Property))
        {
            MobilityEnum = Byte->Enum;
        }
        if (FEnumProperty* Enumeration = CastField<FEnumProperty>(Property))
        {
            Numeric = Enumeration->GetUnderlyingProperty();
            MobilityEnum = Enumeration->GetEnum();
        }
        if (!Numeric || !Numeric->IsInteger() || MobilityEnum != StaticEnum<EComponentMobility::Type>())
        {
            return false;
        }
        return Numeric->GetSignedIntPropertyValue(Property->ContainerPtrToValuePtr<void>(Blueprint->GeneratedClass->GetDefaultObject())) == EComponentMobility::Static;
    }
};

TArray<FConstructionAttachmentConflict> FBlueprintConstructionCompatibility::FindConflicts(UBlueprint* Blueprint)
{
    TArray<FConstructionAttachmentConflict> Conflicts;
    if (!Blueprint->GeneratedClass || !Blueprint->ParentClass || !Blueprint->ParentClass->IsChildOf(AActor::StaticClass()))
    {
        return Conflicts;
    }
    TArray<UEdGraph*> PendingGraphs;
    TSet<UEdGraph*> VisitedGraphs;
    TSet<USceneComponent*> ComponentsWithMobilityChanges;
    UEdGraph* Construction = FBlueprintEditorUtils::FindUserConstructionScript(Blueprint);
    if (Construction)
    {
        PendingGraphs.Add(Construction);
    }
    while (!PendingGraphs.IsEmpty())
    {
        UEdGraph* Graph = PendingGraphs.Pop();
        if (VisitedGraphs.Contains(Graph))
        {
            continue;
        }
        VisitedGraphs.Add(Graph);
        for (UK2Node* Entry : UBlueprintNativizationLibrary::GetEntryNodesPerGraph(Graph, false))
        {
            for (UK2Node* Node : UBlueprintNativizationLibrary::GetAllExecuteNodesInFunction(Entry))
            {
                if (!Node->IsNodeEnabled())
                {
                    continue;
                }
                USceneComponent* Component = nullptr;
                USceneComponent* Parent = nullptr;
                if (UK2Node_AddComponent* Creation = Cast<UK2Node_AddComponent>(Node))
                {
                    UEdGraphPin* ManualAttachment = Creation->FindPin(TEXT("bManualAttachment"));
                    UEdGraphPin* Owner = Creation->FindPin(UEdGraphSchema_K2::PN_Self);
                    UEdGraphPin* DeferredFinish = Creation->FindPin(TEXT("bDeferredFinish"));
                    if (ManualAttachment && ManualAttachment->LinkedTo.IsEmpty() &&
                        ManualAttachment->DefaultValue.Equals(TEXT("false"), ESearchCase::IgnoreCase) &&
                        (!DeferredFinish || (DeferredFinish->LinkedTo.IsEmpty() &&
                            DeferredFinish->DefaultValue.Equals(TEXT("false"), ESearchCase::IgnoreCase))) &&
                        Owner && Owner->LinkedTo.IsEmpty() && !Owner->DefaultObject)
                    {
                        Component = Cast<USceneComponent>(Creation->GetTemplateFromNode());
                        Parent = FConstructionComponentResolver::FindRoot(Blueprint);
                    }
                }
                if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
                {
                    UFunction* Function = Call->GetTargetFunction();
                    if (Function && Function == USceneComponent::StaticClass()->FindFunctionByName(TEXT("SetMobility")))
                    {
                        if (USceneComponent* ChangedComponent = FConstructionComponentResolver::ResolvePin(Blueprint, Call->FindPin(UEdGraphSchema_K2::PN_Self)))
                        {
                            ComponentsWithMobilityChanges.Add(ChangedComponent);
                            UEdGraphPin* Target = Call->FindPin(UEdGraphSchema_K2::PN_Self);
                            TArray<UK2Node*> MacroStack;
                            UEdGraphPin* Source = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Target, 0, true, MacroStack, nullptr);
                            UK2Node_AddComponent* Creation = nullptr;
                            if (Source)
                            {
                                Creation = Cast<UK2Node_AddComponent>(Source->GetOwningNode());
                            }
                            USceneComponent* Root = FConstructionComponentResolver::FindRoot(Blueprint);
                            if (Creation && Root && Root->Mobility == EComponentMobility::Movable &&
                                FConstructionComponentResolver::RequestsStaticMobility(Blueprint, Call->FindPin(TEXT("NewMobility"))))
                            {
                                UEdGraphPin* ManualAttachment = Creation->FindPin(TEXT("bManualAttachment"));
                                UEdGraphPin* Owner = Creation->FindPin(UEdGraphSchema_K2::PN_Self);
                                UEdGraphPin* DeferredFinish = Creation->FindPin(TEXT("bDeferredFinish"));
                                if (ManualAttachment && ManualAttachment->LinkedTo.IsEmpty() &&
                                    ManualAttachment->DefaultValue.Equals(TEXT("false"), ESearchCase::IgnoreCase) &&
                                    (!DeferredFinish || (DeferredFinish->LinkedTo.IsEmpty() &&
                                        DeferredFinish->DefaultValue.Equals(TEXT("false"), ESearchCase::IgnoreCase))) &&
                                    Owner && Owner->LinkedTo.IsEmpty() && !Owner->DefaultObject)
                                {
                                    FConstructionAttachmentConflict Conflict;
                                    Conflict.Blueprint = Call->GetBlueprint();
                                    Conflict.Component = ChangedComponent;
                                    Conflict.Parent = Root;
                                    Conflict.MobilityInput = Call->FindPin(TEXT("NewMobility"));
                                    Conflict.Description = FString::Format(TEXT("{0} / {1} / {2}: SetMobility requests Static for a created component attached to Movable parent {3}. Re-registration outside Blueprint UCS rejects this attachment. TryFix replaces this proven Static request with Movable; arbitrary expressions are not rewritten."),
                                        {Blueprint->GetPathName(), Graph->GetName(), Node->NodeGuid.ToString(), Root->GetPathName()});
                                    Conflicts.Add(MoveTemp(Conflict));
                                }
                            }
                        }
                    }
                    if (Function && Function == USceneComponent::StaticClass()->FindFunctionByName(TEXT("K2_AttachToComponent")))
                    {
                        Component = FConstructionComponentResolver::ResolvePin(Blueprint, Call->FindPin(UEdGraphSchema_K2::PN_Self));
                        Parent = FConstructionComponentResolver::ResolvePin(Blueprint, Call->FindPin(TEXT("Parent")));
                    }
                    else if (Function && Function == AActor::StaticClass()->FindFunctionByName(TEXT("FinishAddComponent")))
                    {
                        UEdGraphPin* ManualAttachment = Call->FindPin(TEXT("bManualAttachment"));
                        UEdGraphPin* Owner = Call->FindPin(UEdGraphSchema_K2::PN_Self);
                        if (ManualAttachment && ManualAttachment->LinkedTo.IsEmpty() &&
                            ManualAttachment->DefaultValue.Equals(TEXT("false"), ESearchCase::IgnoreCase) &&
                            Owner && Owner->LinkedTo.IsEmpty() && !Owner->DefaultObject)
                        {
                            Component = FConstructionComponentResolver::ResolvePin(Blueprint, Call->FindPin(TEXT("Component")));
                            Parent = FConstructionComponentResolver::FindRoot(Blueprint);
                        }
                    }
                    else if (Function && (Call->FunctionReference.IsSelfContext() || Call->IsA<UK2Node_CallParentFunction>()))
                    {
                        UBlueprint* SearchBlueprint = Blueprint;
                        if (Call->IsA<UK2Node_CallParentFunction>())
                        {
                            SearchBlueprint = FConstructionComponentResolver::GetParent(Graph->GetTypedOuter<UBlueprint>());
                        }
                        if (SearchBlueprint)
                        {
                            if (UEdGraph* CalledGraph = FConstructionComponentResolver::FindGraph(SearchBlueprint, Function->GetFName()))
                            {
                                PendingGraphs.Add(CalledGraph);
                            }
                        }
                    }
                }
                if (Component && Parent && Component->Mobility == EComponentMobility::Static && Parent->Mobility == EComponentMobility::Movable)
                {
                    UBlueprint* TemplateOwner = Component->GetTypedOuter<UBlueprint>();
                    if (!TemplateOwner)
                    {
                        if (UBlueprintGeneratedClass* OwnerClass = Component->GetTypedOuter<UBlueprintGeneratedClass>())
                        {
                            TemplateOwner = Cast<UBlueprint>(OwnerClass->ClassGeneratedBy);
                        }
                    }
                    FConstructionAttachmentConflict Conflict;
                    Conflict.Blueprint = TemplateOwner;
                    Conflict.Component = Component;
                    Conflict.Parent = Parent;
                    Conflict.Description = FString::Format(TEXT("{0} / {1} / {2}: Attachment defaults conflict: Static component {3} attaches to Movable parent {4} outside Blueprint UCS context. TryFix changes the component template to Movable. Runtime mobility changes and dynamic parents require manual review."),
                        {Blueprint->GetPathName(), Graph->GetName(), Node->NodeGuid.ToString(), Component->GetPathName(), Parent->GetPathName()});
                    Conflicts.Add(MoveTemp(Conflict));
                }
            }
        }
    }
    Conflicts.RemoveAll([&ComponentsWithMobilityChanges](const FConstructionAttachmentConflict& Conflict)
    {
        return ComponentsWithMobilityChanges.Contains(Conflict.Parent);
    });
    return Conflicts;
}
