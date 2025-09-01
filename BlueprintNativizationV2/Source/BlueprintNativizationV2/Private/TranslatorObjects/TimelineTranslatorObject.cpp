/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/TimelineTranslatorObject.h"
#include "K2Node_Timeline.h"
#include "Components/TimelineComponent.h"
#include "Engine/TimelineTemplate.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"


FString UTimelineTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	FString Content;

	if (UK2Node_Timeline* Timeline = Cast<UK2Node_Timeline>(Node))
	{
		UTimelineTemplate* TimelineTemplate = Node->GetBlueprint()->FindTimelineTemplateByVariableName(Timeline->TimelineName);

		TSet<UCurveBase*> InOutCurves;
		TimelineTemplate->GetAllCurves(InOutCurves);

		for (UCurveBase* InOutCurve : InOutCurves)
		{

#if WITH_EDITOR
			FString PackageName = FString::Format(TEXT("/Game/SavedCurves/{0}"), {*Timeline->TimelineName.ToString() });
			UPackage* Package = CreatePackage(*PackageName);
			UCurveBase* Duplicate = DuplicateObject(InOutCurve, Package, FName("MyCurve"));

			FAssetRegistryModule::AssetCreated(Duplicate);
			Duplicate->MarkPackageDirty();

			FString PackageFileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
			FSavePackageArgs Args;
			UPackage::SavePackage(Package, Duplicate, *PackageFileName, Args);
#endif
		}

		Content += "SpawnComponent<>()";
	}
	return Content;
}

FString UTimelineTranslatorObject::GenerateGlobalVariables(UK2Node* InputNode, FString Prefix, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	return FString();
}
