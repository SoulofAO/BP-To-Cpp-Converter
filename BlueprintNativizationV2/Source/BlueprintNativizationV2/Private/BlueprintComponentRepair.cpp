/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#include "BlueprintComponentRepair.h"

#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Misc/PackageName.h"
#include "UObject/LinkerLoad.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "UObject/UnrealType.h"

TArray<UActorComponent*> FBlueprintComponentRepair::FindUnmatchedConstructionComponents(AActor* Actor)
{
    TArray<UActorComponent*> Components;
    TArray<const UBlueprintGeneratedClass*> Hierarchy;
    if (!Actor || Actor->IsTemplate() || Actor->IsActorBeingDestroyed() ||
        !UBlueprintGeneratedClass::GetGeneratedClassesHierarchy(Actor->GetClass(), Hierarchy))
    {
        return Components;
    }
    for (UActorComponent* Component : Actor->GetComponents())
    {
        if (!IsValid(Component) || Component->IsBeingDestroyed() || Component->IsVisualizationComponent() ||
            Component->CreationMethod != EComponentCreationMethod::SimpleConstructionScript ||
            Component->GetArchetype() != Component->GetClass()->GetDefaultObject())
        {
            continue;
        }
        bool bHasSource = false;
        for (const UBlueprintGeneratedClass* BlueprintClass : Hierarchy)
        {
            if (BlueprintClass->SimpleConstructionScript &&
                BlueprintClass->SimpleConstructionScript->FindSCSNode(Component->GetFName()))
            {
                bHasSource = true;
                break;
            }
        }
        for (TFieldIterator<FObjectPropertyBase> Property(Actor->GetClass()); Property && !bHasSource; ++Property)
        {
            bHasSource = Property->GetObjectPropertyValue_InContainer(Actor) == Component;
        }
        if (!bHasSource)
        {
            Components.Add(Component);
        }
    }
    return Components;
}

bool FBlueprintComponentRepair::HasRepairs() const
{
    return !BrokenNodes.IsEmpty() || !MissingPackages.IsEmpty();
}

FBlueprintComponentRepair FBlueprintComponentRepair::Inspect(UBlueprint* Blueprint)
{
    FBlueprintComponentRepair Repair;
    if (!Blueprint->ParentClass || !Blueprint->GeneratedClass || !Blueprint->SimpleConstructionScript)
    {
        return Repair;
    }

    USimpleConstructionScript* ConstructionScript = Blueprint->SimpleConstructionScript;
    for (USCS_Node* Node : ConstructionScript->GetAllNodes())
    {
        if (Node && !Node->ComponentClass && !Node->ComponentTemplate &&
            (ConstructionScript->GetRootNodes().Contains(Node) || ConstructionScript->FindParentNode(Node)))
        {
            Repair.BrokenNodes.Add(Node);
            Repair.Diagnostics.Add(FString::Format(
                TEXT("{0}: component {1} has neither a class nor a template. TryFix can remove its SCS node while promoting its children; the missing component's behavior cannot be restored."),
                {Blueprint->GetPathName(), Node->GetVariableName().ToString()}));
        }
    }

    FLinkerLoad* Linker = Blueprint->GetLinker();
    if (!Linker)
    {
        return Repair;
    }
    // SCS validation can remove a broken node on load while its failed export remains in the saved package.
    for (const FObjectExport& Export : Linker->ExportMap)
    {
        if (Export.Object || !Export.ObjectName.ToString().EndsWith(TEXT("_GEN_VARIABLE")) ||
            !Export.ClassIndex.IsImport() || !Export.OuterIndex.IsExport() ||
            !Linker->ExportMap.IsValidIndex(Export.OuterIndex.ToExport()) ||
            Linker->ExportMap[Export.OuterIndex.ToExport()].Object != Blueprint->GeneratedClass)
        {
            continue;
        }
        FPackageIndex ImportIndex = Export.ClassIndex;
        FName MissingPackage;
        for (int32 RemainingImports = Linker->ImportMap.Num(); RemainingImports > 0 && ImportIndex.IsImport(); --RemainingImports)
        {
            if (!Linker->ImportMap.IsValidIndex(ImportIndex.ToImport()))
            {
                break;
            }
            const FObjectImport& Import = Linker->ImportMap[ImportIndex.ToImport()];
            if (Import.XObject)
            {
                break;
            }
            if (Import.OuterIndex.IsNull() && Import.ClassName == NAME_Package)
            {
                const FString PackagePath = Import.ObjectName.ToString();
                if (PackagePath.StartsWith(TEXT("/Game/")) && !FPackageName::DoesPackageExist(PackagePath) &&
                    !FindPackage(nullptr, *PackagePath))
                {
                    MissingPackage = Import.ObjectName;
                }
                break;
            }
            ImportIndex = Import.OuterIndex;
        }
        if (!MissingPackage.IsNone())
        {
            Repair.MissingPackages.AddUnique(MissingPackage);
            Repair.Diagnostics.Add(FString::Format(
                TEXT("{0}: component template {1} failed to load from missing class package {2}. TryFix can compile and resave the Blueprint to discard stale imports; it cannot recover the missing component's behavior."),
                {Blueprint->GetPathName(), Export.ObjectName.ToString(), MissingPackage.ToString()}));
        }
    }
    return Repair;
}

void FBlueprintComponentRepair::TryRemoveBrokenNodes(UBlueprint* Blueprint, TArray<FString>& Messages) const
{
    USimpleConstructionScript* ConstructionScript = Blueprint->SimpleConstructionScript;
    for (USCS_Node* Node : BrokenNodes)
    {
        if (ConstructionScript->GetAllNodes().Contains(Node) && !Node->ComponentClass && !Node->ComponentTemplate)
        {
            ConstructionScript->Modify();
            const FString VariableName = Node->GetVariableName().ToString();
            ConstructionScript->RemoveNodeAndPromoteChildren(Node);
            Messages.Add(FString::Format(TEXT("Removed broken SCS node {0} from {1}; children were promoted."),
                {VariableName, Blueprint->GetPathName()}));
        }
    }
}
