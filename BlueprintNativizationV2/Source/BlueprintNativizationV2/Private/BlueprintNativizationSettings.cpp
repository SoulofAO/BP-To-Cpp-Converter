/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "BlueprintNativizationSettings.h"
#include "TranslatorObjects/BranchTranslatorObject.h"
#include "TranslatorObjects/CallFunctionTranslatorObject.h"
#include "TranslatorObjects/CallParentTranslatorObject.h"
#include "TranslatorObjects/Latens/DelayTranslatorObject.h"
#include "TranslatorObjects/Latens/DelayNextTickTranslatorObject.h"
#include "TranslatorObjects/Latens/RetriggerableDelayTranslatorObject.h"
#include "TranslatorObjects/Cycles/ForEachLoopTranslatorObject.h"
#include "TranslatorObjects/Cycles/ForLoopTranslatorObject.h"
#include "TranslatorObjects/MoveComponentToTranslatorObject.h"
#include "TranslatorObjects/SequenceTranslatorObject.h"
#include "TranslatorObjects/TimelineTranslatorObject.h"
#include "TranslatorObjects/VariableSetTranslatorObject.h"
#include "TranslatorObjects/Cycles/WhileLoopTranslatorObject.h"
#include "TranslatorObjects/VariableGetTranslatorObject.h"
#include "TranslatorObjects/TemporaryVariableTranslatorObject.h"
#include "TranslatorObjects/MakeArrayTranslatorObject.h"
#include "TranslatorObjects/PromotabaleOperatorObject.h"
#include "TranslatorObjects/SelfTranslatorObject.h"
#include "TranslatorObjects/Delegates/CallDelegateTranslatorObject.h"
#include "TranslatorObjects/Delegates/AddDelegateTranslatorObject.h"
#include "TranslatorObjects/Delegates/RemoveDelegateTranslatorObject.h"
#include "TranslatorObjects/Delegates/RemoveAllDelegateTranslatorObject.h"
#include "TranslatorObjects/Delegates/CreateDelegateTranslatorObject.h"
#include "TranslatorObjects/DynamicCastTranslatorObject.h"

#include "TranslatorObjects/MakeStructTranslatorObject.h"
#include "TranslatorObjects/BreakStructTranslatorObject.h"
#include "TranslatorObjects/MakeSetTranslatorObject.h"
#include "TranslatorObjects/MakeMapTranslatorObject.h"
#include "TranslatorObjects/SetMemberStructTranslatorObject.h"
#include "TranslatorObjects/SelectTranslatorObject.h"
#include "TranslatorObjects/SwitchTranslatorObject.h"
#include "TranslatorObjects/Array/GetArrayItemTranslatorObject.h"

#include "TranslatorObjects/CreateTranslators/AddComponentTranslatorObject.h"
#include "TranslatorObjects/CreateTranslators/ConstructObjectTranslatorObject.h"
#include "TranslatorObjects/CreateTranslators/CreateWidgetTranslatorObject.h"
#include "TranslatorObjects/CreateTranslators/SpawnActorTranslatorObject.h"

#include "TranslatorObjects/GetSubsystemTranslatorObject.h"
#include "TranslatorObjects/EnhancedInputTranslatorObject.h"

#include "TranslatorObjects/ReturnTranslatorObject.h"

#include "Blueprint/UserWidget.h"
#include "ControlRig.h"

#include "BlueprintNativizationLibrary.h"

UBlueprintNativizationV2EditorSettings::UBlueprintNativizationV2EditorSettings()
{
	TranslatorBPToCPPObjects =
	{
		// Highest Priority
		UBranchTranslatorObject::StaticClass(),
		UDelayTranslatorObject::StaticClass(),
		UDelayNextTickTranslatorObject::StaticClass(),
		URetriggarableDelayTranslatorObject::StaticClass(),
		UForEachLoopTranslatorObject::StaticClass(),
		UForLoopTranslatorObject::StaticClass(),
		UMoveComponentToTranslatorObject::StaticClass(),
		USequenceTranslatorObject::StaticClass(),
		UTimelineTranslatorObject::StaticClass(),
		UVariableSetTranslatorObject::StaticClass(),
		UWhileTranslatorObject::StaticClass(),
		UVariableGetTranslatorObject::StaticClass(),
		UTemporaryVariableTranslatorObject::StaticClass(),
		UMakeArrayTranslatorObject::StaticClass(),
		UPromotabaleOperatorTranslatorObject::StaticClass(),
		USelfTranslatorObject::StaticClass(),
		UAddDelegateTranslatorObject::StaticClass(),
		UCallDelegateTranslatorObject::StaticClass(),
		URemoveDelegateTranslatorObject::StaticClass(),
		URemoveAllDelegateTranslatorObject::StaticClass(),
		UCreateDelegateTranslatorObject::StaticClass(),
		UDynamicCastTranslatorObject::StaticClass(),

		USetMemberStructTranslatorObject::StaticClass(),
		UGetSubsystemTranslatorObject::StaticClass(),
		UMakeStructTranslatorObject::StaticClass(),
		UBreakStructTranslatorObject::StaticClass(),
		UMakeSetTranslatorObject::StaticClass(),
		UMakeMapTranslatorObject::StaticClass(),
		UGetArrayItemTranslatorObject::StaticClass(),
		UEnhancedInputTranslatorObject::StaticClass(),

		USelectTranslatorObject::StaticClass(),
		USwitchTranslatorObject::StaticClass(),

		UAddComponentTranslatorObject::StaticClass(),
		UCreateWidgetTranslatorObject::StaticClass(),
		USpawnActorTranslatorObject::StaticClass(),
		UCallParentTranslatorObject::StaticClass(),


		// Lowers Priority
		UConstructObjectTranslatorObject::StaticClass(),
		UCallFunctionTranslatorObject::StaticClass(),
		UReturnTranslatorObject::StaticClass(),

	};

	GlobalVariableNames =
	{
		"GEngine",
		"GWorld",
		"GEditor",
		"GIsEditor",
		"GIsServer",
		"GIsClient",
		"GIsRunning",
		"GFrameCounter",
		"GDeltaTime",
		"GLog",
		"GWarn",
		"GConfig",
		"GEngineLoop",
		"GUObjectArray",
	};

	SetupActionObject = TSoftClassPtr<USetupActionObject>(
		FSoftClassPath(TEXT("/BlueprintNativizationV2/BP_MainSetupActionObject.BP_MainSetupActionObject_C"))
	);



	FunctionRedirects.Add({
	FFunctionDescriptor(TEXT("AActor"), TEXT("ReceiveTick"), { TEXT("float DeltaSeconds") }),
	FFunctionDescriptor(TEXT("AActor"), TEXT("Tick"),        { TEXT("float DeltaSeconds") })
		});

	FunctionRedirects.Add({
		FFunctionDescriptor(TEXT("AActor"), TEXT("ReceiveBeginPlay"), {}),
		FFunctionDescriptor(TEXT("AActor"), TEXT("BeginPlay"),        {})
		});

	FunctionRedirects.Add({
		FFunctionDescriptor(TEXT("AActor"), TEXT("UserConstructionScript"), {}),
		FFunctionDescriptor(TEXT("AActor"), TEXT("OnConstruction"), {"const FTransform& Transform"})
		});

	FunctionRedirects.Add({
		FFunctionDescriptor(TEXT("AActor"), TEXT("ReceiveEndPlay"), { TEXT("EEndPlayReason::Type EndPlayReason") }),
		FFunctionDescriptor(TEXT("AActor"), TEXT("EndPlay"),        { TEXT("EEndPlayReason::Type EndPlayReason") })
		});

	FunctionRedirects.Add({
		FFunctionDescriptor(TEXT("AActor"), TEXT("ReceiveHit"),
			{
				TEXT("UPrimitiveComponent* MyComp"),
				TEXT("AActor* Other"),
				TEXT("UPrimitiveComponent* OtherComp"),
				TEXT("bool bSelfMoved"),
				TEXT("FVector HitLocation"),
				TEXT("FVector HitNormal"),
				TEXT("FVector NormalImpulse"),
				TEXT("const FHitResult& Hit")
			}),
		FFunctionDescriptor(TEXT("AActor"), TEXT("NotifyHit"),
			{
				TEXT("UPrimitiveComponent* MyComp"),
				TEXT("AActor* Other"),
				TEXT("UPrimitiveComponent* OtherComp"),
				TEXT("bool bSelfMoved"),
				TEXT("FVector HitLocation"),
				TEXT("FVector HitNormal"),
				TEXT("FVector NormalImpulse"),
				TEXT("const FHitResult& Hit")
			})
		});

	FunctionRedirects.Add({
		FFunctionDescriptor(TEXT("AActor"), TEXT("ReceiveAnyDamage"),
			{
				TEXT("float Damage"),
				TEXT("const UDamageType* DamageType"),
				TEXT("AController* InstigatedBy"),
				TEXT("AActor* DamageCauser")
			}),
		FFunctionDescriptor(TEXT("AActor"), TEXT("TakeDamage"),
			{
				TEXT("float Damage"),
				TEXT("FDamageEvent const& DamageEvent"),
				TEXT("AController* EventInstigator"),
				TEXT("AActor* DamageCauser")
			})
		});

	FunctionRedirects.Add({
	FFunctionDescriptor(TEXT("UActorComponent"), TEXT("ReceiveBeginPlay"), {}),
	FFunctionDescriptor(TEXT("UActorComponent"), TEXT("BeginPlay"), {})
		});

	FunctionRedirects.Add({
		FFunctionDescriptor(TEXT("UActorComponent"), TEXT("ReceiveEndPlay"), { TEXT("EEndPlayReason::Type EndPlayReason") }),
		FFunctionDescriptor(TEXT("UActorComponent"), TEXT("EndPlay"),        { TEXT("EEndPlayReason::Type EndPlayReason") })
		});

	FunctionRedirects.Add({
		FFunctionDescriptor(TEXT("UActorComponent"), TEXT("ReceiveTick"), { TEXT("float DeltaSeconds") }),
		FFunctionDescriptor(TEXT("UActorComponent"), TEXT("TickComponent"),
			{
				TEXT("float DeltaTime"),
				TEXT("ELevelTick TickType"),
				TEXT("FActorComponentTickFunction* ThisTickFunction")
			})
		});

	FunctionRedirects.Add({
		FFunctionDescriptor(TEXT("UPrimitiveComponent"), TEXT("ReceiveHit"),
			{
				TEXT("MyComp"),
				TEXT("AActor* Other"),
				TEXT("UPrimitiveComponent* OtherComp"),
				TEXT("bool bSelfMoved"),
				TEXT("FVector HitLocation"),
				TEXT("FVector HitNormal"),
				TEXT("FVector NormalImpulse"),
				TEXT("const FHitResult& Hit")
			}),
		FFunctionDescriptor(TEXT("UPrimitiveComponent"), TEXT("NotifyHit"),
			{
				TEXT("UPrimitiveComponent* MyComp"),
				TEXT("AActor* Other"),
				TEXT("UPrimitiveComponent* OtherComp"),
				TEXT("bool bSelfMoved"),
				TEXT("FVector HitLocation"),
				TEXT("FVector HitNormal"),
				TEXT("FVector NormalImpulse"),
				TEXT("const FHitResult& Hit")
			})
		});

	FunctionRedirects.Add({
		FFunctionDescriptor(TEXT("UPrimitiveComponent"), TEXT("ReceiveComponentBeginOverlap"),
			{
				TEXT("UPrimitiveComponent* OverlappedComponent"),
				TEXT("AActor* OtherActor"),
				TEXT("UPrimitiveComponent* OtherComp"),
				TEXT("int32 OtherBodyIndex"),
				TEXT("bool bFromSweep"),
				TEXT("const FHitResult& SweepResult")
			}),
		FFunctionDescriptor(TEXT("UPrimitiveComponent"), TEXT("OnComponentBeginOverlap"),
			{
				TEXT("UPrimitiveComponent* OverlappedComponent"),
				TEXT("AActor* OtherActor"),
				TEXT("UPrimitiveComponent* OtherComp"),
				TEXT("int32 OtherBodyIndex"),
				TEXT("bool bFromSweep"),
				TEXT("const FHitResult& SweepResult")
			})
		});

	FunctionRedirects.Add({
		FFunctionDescriptor(TEXT("UPrimitiveComponent"), TEXT("ReceiveComponentEndOverlap"),
			{
				TEXT("UPrimitiveComponent* OverlappedComponent"),
				TEXT("AActor* OtherActor"),
				TEXT("UPrimitiveComponent* OtherComp"),
				TEXT("int32 OtherBodyIndex")
			}),
		FFunctionDescriptor(TEXT("UPrimitiveComponent"), TEXT("OnComponentEndOverlap"),
			{
				TEXT("UPrimitiveComponent* OverlappedComponent"),
				TEXT("AActor* OtherActor"),
				TEXT("UPrimitiveComponent* OtherComp"),
				TEXT("int32 OtherBodyIndex")
			})
		});
	FunctionRedirects.Add({
		FFunctionDescriptor(TEXT("AActor"), TEXT("ReceiveActorBeginOverlap"), { TEXT("AActor* OtherActor") }),
		FFunctionDescriptor(TEXT("AActor"), TEXT("NotifyActorBeginOverlap"), { TEXT("AActor* OtherActor") })
		});

	FunctionRedirects.Add({
		FFunctionDescriptor(TEXT("AActor"), TEXT("ReceiveActorEndOverlap"), { TEXT("AActor* OtherActor") }),
		FFunctionDescriptor(TEXT("AActor"), TEXT("NotifyActorEndOverlap"), { TEXT("AActor* OtherActor") })
		});

	FunctionRedirects.Add({
		FFunctionDescriptor(TEXT("AActor"), TEXT("ReceiveActorOnClicked"), { TEXT("FKey ButtonPressed") }),
		FFunctionDescriptor(TEXT("AActor"), TEXT("NotifyActorOnClicked"), { TEXT("FKey ButtonPressed") })
		});

	FunctionRedirects.Add({
		FFunctionDescriptor(TEXT("AActor"), TEXT("ReceiveActorOnReleased"), { TEXT("FKey ButtonReleased") }),
		FFunctionDescriptor(TEXT("AActor"), TEXT("NotifyActorOnReleased"), { TEXT("FKey ButtonReleased") })
		});

	FunctionRedirects.Add({
		FFunctionDescriptor(TEXT("AActor"), TEXT("ReceiveActorBeginCursorOver"), {}),
		FFunctionDescriptor(TEXT("AActor"), TEXT("NotifyActorBeginCursorOver"), {})
		});

	FunctionRedirects.Add({
		FFunctionDescriptor(TEXT("AActor"), TEXT("ReceiveActorEndCursorOver"), {}),
		FFunctionDescriptor(TEXT("AActor"), TEXT("NotifyActorEndCursorOver"), {})
		});

	FunctionRedirects.Add({
		FFunctionDescriptor(TEXT("AActor"), TEXT("ReceiveActorOnInputTouchBegin"), { TEXT("const ETouchIndex::Type FingerIndex") }),
		FFunctionDescriptor(TEXT("AActor"), TEXT("NotifyActorOnInputTouchBegin"), { TEXT("const ETouchIndex::Type FingerIndex") })
		});

	FunctionRedirects.Add({
		FFunctionDescriptor(TEXT("AActor"), TEXT("ReceiveActorOnInputTouchEnd"), { TEXT("const ETouchIndex::Type FingerIndex") }),
		FFunctionDescriptor(TEXT("AActor"), TEXT("NotifyActorOnInputTouchEnd"), { TEXT("const ETouchIndex::Type FingerIndex") })
		});

	FunctionRedirects.Add({
		FFunctionDescriptor(TEXT("AActor"), TEXT("ReceiveActorOnInputTouchEnter"), { TEXT("const ETouchIndex::Type FingerIndex") }),
		FFunctionDescriptor(TEXT("AActor"), TEXT("NotifyActorOnInputTouchEnter"), { TEXT("const ETouchIndex::Type FingerIndex") })
		});

	FunctionRedirects.Add({
		FFunctionDescriptor(TEXT("AActor"), TEXT("ReceiveActorOnInputTouchLeave"), { TEXT("const ETouchIndex::Type FingerIndex") }),
		FFunctionDescriptor(TEXT("AActor"), TEXT("NotifyActorOnInputTouchLeave"), { TEXT("const ETouchIndex::Type FingerIndex") })
		});

	FunctionRedirects.Add({
		FFunctionDescriptor(TEXT("AActor"), TEXT("ReceiveDestroyed"), {}),
		FFunctionDescriptor(TEXT("AActor"), TEXT("Destroyed"), {})
		});

	FunctionRedirects.Add({
	FFunctionDescriptor(TEXT("AActor"), TEXT("ReceivePointDamage"),
		{
		TEXT("float Damage"),
		TEXT("const UDamageType* DamageType"),
		TEXT("FVector HitLocation"),
		TEXT("FVector HitNormal"),
		TEXT("UPrimitiveComponent* HitComponent"),
		TEXT("FName BoneName"),
		TEXT("FVector ShotFromDirection"),
		TEXT("AController* InstigatedBy"),
		TEXT("AActor* DamageCauser"),
		TEXT("const FHitResult& HitInfo")
	}),
	FFunctionDescriptor(TEXT("AActor"), TEXT("TakeDamage"),
		{
		TEXT("float Damage"),
		TEXT("FDamageEvent const& DamageEvent"),
		TEXT("AController* EventInstigator"),
		TEXT("AActor* DamageCauser")
		})
	});

	FunctionRedirects.Add({
	FFunctionDescriptor(TEXT("AActor"), TEXT("ReceiveRadialDamage"),
	{
	TEXT("float DamageReceived"),
	TEXT("const UDamageType* DamageType"),
	TEXT("FVector Origin"),
	TEXT("const FHitResult& HitInfo"),
	TEXT("AController* InstigatedBy"),
	TEXT("AActor* DamageCauser")
	}),
	FFunctionDescriptor(TEXT("AActor"), TEXT("TakeDamage"),
	{
	TEXT("float Damage"),
	TEXT("FDamageEvent const& DamageEvent"),
	TEXT("AController* EventInstigator"),
	TEXT("AActor* DamageCauser")
	})
		});

	// Pawn-specific
	FunctionRedirects.Add({
		FFunctionDescriptor(TEXT("APawn"), TEXT("ReceivePossessed"), { TEXT("AController* NewController") }),
		FFunctionDescriptor(TEXT("APawn"), TEXT("PossessedBy"),      { TEXT("AController* NewController") })
		});

	FunctionRedirects.Add({
		FFunctionDescriptor(TEXT("APawn"), TEXT("ReceiveUnPossessed"), { }),
		FFunctionDescriptor(TEXT("APawn"), TEXT("UnPossessed"),        { })
		});


	// �������������� Charac
	FunctionRedirects.Add({
	FFunctionDescriptor(TEXT("UPrimitiveComponent"), TEXT("ReceiveComponentBeginCursorOver"), { TEXT("UPrimitiveComponent* TouchedComponent") }),
	FFunctionDescriptor(TEXT("UPrimitiveComponent"), TEXT("OnBeginCursorOver"), { TEXT("UPrimitiveComponent* TouchedComponent") })
		});

	FunctionRedirects.Add({
	FFunctionDescriptor(TEXT("UPrimitiveComponent"), TEXT("ReceiveComponentEndCursorOver"), { TEXT("UPrimitiveComponent* TouchedComponent") }),
	FFunctionDescriptor(TEXT("UPrimitiveComponent"), TEXT("OnEndCursorOver"), { TEXT("UPrimitiveComponent* TouchedComponent") })
		});

	FunctionRedirects.Add({
	FFunctionDescriptor(TEXT("UPrimitiveComponent"), TEXT("ReceiveComponentOnClicked"), { TEXT("UPrimitiveComponent* TouchedComponent"), TEXT("FKey ButtonPressed") }),
	FFunctionDescriptor(TEXT("UPrimitiveComponent"), TEXT("OnClicked"), { TEXT("UPrimitiveComponent* TouchedComponent"), TEXT("FKey ButtonPressed") })
		});

	FunctionRedirects.Add({
	FFunctionDescriptor(TEXT("UPrimitiveComponent"), TEXT("ReceiveComponentOnReleased"), { TEXT("UPrimitiveComponent* TouchedComponent"), TEXT("FKey ButtonReleased") }),
	FFunctionDescriptor(TEXT("UPrimitiveComponent"), TEXT("OnReleased"), { TEXT("UPrimitiveComponent* TouchedComponent"), TEXT("FKey ButtonReleased") })
		});

	FunctionRedirects.Add({
	FFunctionDescriptor(TEXT("UPrimitiveComponent"), TEXT("ReceiveComponentOnInputTouchBegin"), { TEXT("UPrimitiveComponent* TouchedComponent"), TEXT("const ETouchIndex::Type FingerIndex") }),
	FFunctionDescriptor(TEXT("UPrimitiveComponent"), TEXT("OnInputTouchBegin"), { TEXT("UPrimitiveComponent* TouchedComponent"), TEXT("const ETouchIndex::Type FingerIndex") })
		});

	FunctionRedirects.Add({
	FFunctionDescriptor(TEXT("UPrimitiveComponent"), TEXT("ReceiveComponentOnInputTouchEnd"), { TEXT("UPrimitiveComponent* TouchedComponent"), TEXT("const ETouchIndex::Type FingerIndex") }),
	FFunctionDescriptor(TEXT("UPrimitiveComponent"), TEXT("OnInputTouchEnd"), { TEXT("UPrimitiveComponent* TouchedComponent"), TEXT("const ETouchIndex::Type FingerIndex") })
		});

	FunctionRedirects.Add({
	FFunctionDescriptor(TEXT("UPrimitiveComponent"), TEXT("ReceiveComponentOnInputTouchEnter"), { TEXT("UPrimitiveComponent* TouchedComponent"), TEXT("const ETouchIndex::Type FingerIndex") }),
	FFunctionDescriptor(TEXT("UPrimitiveComponent"), TEXT("OnInputTouchEnter"), { TEXT("UPrimitiveComponent* TouchedComponent"), TEXT("const ETouchIndex::Type FingerIndex") })
		});

	FunctionRedirects.Add({
	FFunctionDescriptor(TEXT("UPrimitiveComponent"), TEXT("ReceiveComponentOnInputTouchLeave"), { TEXT("UPrimitiveComponent* TouchedComponent"), TEXT("const ETouchIndex::Type FingerIndex") }),
	FFunctionDescriptor(TEXT("UPrimitiveComponent"), TEXT("OnInputTouchLeave"), { TEXT("UPrimitiveComponent* TouchedComponent"), TEXT("const ETouchIndex::Type FingerIndex") })
		});




	ConstructorDescriptors =
	{
		FConstructorDescriptor(
			TEXT("FVector"),
			{
				FConstructorPropertyDescriptor(TEXT("X"), 0, TEXT("0.0")),
				FConstructorPropertyDescriptor(TEXT("Y"), 1, TEXT("0.0")),
				FConstructorPropertyDescriptor(TEXT("Z"), 2, TEXT("0.0"))
			}
		),
		FConstructorDescriptor(
			TEXT("FRotator"),
			{
				FConstructorPropertyDescriptor(TEXT("Pitch"), 0, TEXT("0.0")),
				FConstructorPropertyDescriptor(TEXT("Yaw"), 1, TEXT("0.0")),
				FConstructorPropertyDescriptor(TEXT("Roll"), 2, TEXT("0.0"))
			}
		),
		FConstructorDescriptor(
			TEXT("FTransform"),
			{
				FConstructorPropertyDescriptor(TEXT("Rotation"), 0, TEXT("FQuat::Identity")),
				FConstructorPropertyDescriptor(TEXT("Translation"), 1, TEXT("FVector::ZeroVector")),
				FConstructorPropertyDescriptor(TEXT("Scale3D"), 2, TEXT("FVector(1.0f, 1.0f, 1.0f)"))
			}
		),
		FConstructorDescriptor(
			TEXT("FLinearColor"),
			{
				FConstructorPropertyDescriptor(TEXT("R"), 0, TEXT("0.0")),
				FConstructorPropertyDescriptor(TEXT("G"), 1, TEXT("0.0")),
				FConstructorPropertyDescriptor(TEXT("B"), 2, TEXT("0.0")),
				FConstructorPropertyDescriptor(TEXT("A"), 3, TEXT("1.0"))
			}
		),
		FConstructorDescriptor(
			TEXT("FColor"),
			{
				FConstructorPropertyDescriptor(TEXT("R"), 0, TEXT("0")),
				FConstructorPropertyDescriptor(TEXT("G"), 1, TEXT("0")),
				FConstructorPropertyDescriptor(TEXT("B"), 2, TEXT("0")),
				FConstructorPropertyDescriptor(TEXT("A"), 3, TEXT("255"))
			}
		),
		FConstructorDescriptor(
			TEXT("FIntPoint"),
			{
				FConstructorPropertyDescriptor(TEXT("X"), 0, TEXT("0")),
				FConstructorPropertyDescriptor(TEXT("Y"), 1, TEXT("0"))
			}
		),
		FConstructorDescriptor(
			TEXT("FIntVector"),
			{
				FConstructorPropertyDescriptor(TEXT("X"), 0, TEXT("0")),
				FConstructorPropertyDescriptor(TEXT("Y"), 1, TEXT("0")),
				FConstructorPropertyDescriptor(TEXT("Z"), 2, TEXT("0"))
			}
		),
		FConstructorDescriptor(
			TEXT("FQuat"),
			{
				FConstructorPropertyDescriptor(TEXT("X"), 0, TEXT("0.0")),
				FConstructorPropertyDescriptor(TEXT("Y"), 1, TEXT("0.0")),
				FConstructorPropertyDescriptor(TEXT("Z"), 2, TEXT("0.0")),
				FConstructorPropertyDescriptor(TEXT("W"), 3, TEXT("1.0"))
			}
		),
		FConstructorDescriptor(
			TEXT("FBox"),
			{
				FConstructorPropertyDescriptor(TEXT("Min"), 0, TEXT("FVector::ZeroVector")),
				FConstructorPropertyDescriptor(TEXT("Max"), 1, TEXT("FVector::ZeroVector")),
				FConstructorPropertyDescriptor(TEXT("bIsValid"), 2, TEXT("false"))
			}
		),
		FConstructorDescriptor(
			TEXT("FBox2D"),
			{
				FConstructorPropertyDescriptor(TEXT("Min"), 0, TEXT("FVector2D::ZeroVector")),
				FConstructorPropertyDescriptor(TEXT("Max"), 1, TEXT("FVector2D::ZeroVector")),
				FConstructorPropertyDescriptor(TEXT("bIsValid"), 2, TEXT("false"))
			}
		),
		FConstructorDescriptor(
			TEXT("FVector2D"),
			{
				FConstructorPropertyDescriptor(TEXT("X"), 0, TEXT("0.0")),
				FConstructorPropertyDescriptor(TEXT("Y"), 1, TEXT("0.0"))
			}
		),
		FConstructorDescriptor(
			TEXT("FMatrix"),
			{
				FConstructorPropertyDescriptor(TEXT("M"), 0, TEXT("{}"))
			}
		)
	};

	//���� ������ ������ - ������  �� - ������������ ����������. � �������� ��� ������������ ��� ������� ��������.

	GetterAndSetterDescriptors = {
	{ TEXT("UStaticMeshComponent"), TEXT("StaticMesh"), TEXT("GetStaticMesh"), TEXT("SetStaticMesh") },
	{ TEXT("UStaticMeshComponent"), TEXT("Material"), TEXT("GetMaterial"), TEXT("SetMaterial") },
	{ TEXT("UStaticMeshComponent"), TEXT("Visibility"), TEXT("GetVisibleFlag"), TEXT("SetVisibility") },


	{ TEXT("USkeletalMeshComponent"), TEXT("SkeletalMesh"), TEXT("GetSkeletalMeshAsset"), TEXT("SetSkeletalMesh") },
	{ TEXT("USkeletalMeshComponent"), TEXT("AnimClass"), TEXT("GetAnimInstanceClass"), TEXT("SetAnimInstanceClass") },
	{ TEXT("USkeletalMeshComponent"), TEXT("Material"), TEXT("GetMaterial"), TEXT("SetMaterial") },
	{ TEXT("USkeletalMeshComponent"), TEXT("Visibility"), TEXT("GetVisibleFlag"), TEXT("SetVisibility") },
	{ TEXT("USkeletalMeshComponent"), TEXT("PhysicsAsset"), TEXT("GetPhysicsAsset"), TEXT("SetPhysicsAsset") },


	{ TEXT("USceneComponent"), TEXT("RelativeLocation"), TEXT("GetRelativeLocation"), TEXT("SetRelativeLocation") },
	{ TEXT("USceneComponent"), TEXT("RelativeRotation"), TEXT("GetRelativeRotation"), TEXT("SetRelativeRotation") },
	{ TEXT("USceneComponent"), TEXT("RelativeScale3D"), TEXT("GetRelativeScale3D"), TEXT("SetRelativeScale3D") },
	{ TEXT("USceneComponent"), TEXT("Visibility"), TEXT("GetVisibleFlag"), TEXT("SetVisibility") },


	{ TEXT("AActor"), TEXT("ActorLocation"), TEXT("GetActorLocation"), TEXT("SetActorLocation") },
	{ TEXT("AActor"), TEXT("ActorRotation"), TEXT("GetActorRotation"), TEXT("SetActorRotation") },
	{ TEXT("AActor"), TEXT("ActorScale3D"), TEXT("GetActorScale3D"), TEXT("SetActorScale3D") },
	{ TEXT("AActor"), TEXT("ActorHiddenInGame"), TEXT("IsHidden"), TEXT("SetActorHiddenInGame") },
	{ TEXT("AActor"), TEXT("Mobility"), TEXT("GetRootComponentMobility"), TEXT("SetMobility") },


	{ TEXT("ULightComponent"), TEXT("Intensity"), TEXT("GetIntensity"), TEXT("SetIntensity") },
	{ TEXT("ULightComponent"), TEXT("LightColor"), TEXT("GetLightColor"), TEXT("SetLightColor") },


	{ TEXT("UCameraComponent"), TEXT("FieldOfView"), TEXT("GetFieldOfView"), TEXT("SetFieldOfView") },
	{ TEXT("UCameraComponent"), TEXT("AspectRatio"), TEXT("GetAspectRatio"), TEXT("SetAspectRatio") },
	{ TEXT("UCameraComponent"), TEXT("OrthoWidth"), TEXT("GetOrthoWidth"), TEXT("SetOrthoWidth") },


	{ TEXT("UAudioComponent"), TEXT("Sound"), TEXT("GetSound"), TEXT("SetSound") },
	{ TEXT("UAudioComponent"), TEXT("VolumeMultiplier"), TEXT("GetVolumeMultiplier"), TEXT("SetVolumeMultiplier") },


	{ TEXT("UWidgetComponent"), TEXT("WidgetClass"), TEXT("GetWidgetClass"), TEXT("SetWidgetClass") },
	{ TEXT("UWidgetComponent"), TEXT("DrawSize"), TEXT("GetDrawSize"), TEXT("SetDrawSize") },


	{ TEXT("UCapsuleComponent"), TEXT("CapsuleHalfHeight"), TEXT("GetUnscaledCapsuleHalfHeight"), TEXT("SetCapsuleHalfHeight") },
	{ TEXT("UCapsuleComponent"), TEXT("CapsuleRadius"), TEXT("GetUnscaledCapsuleRadius"), TEXT("SetCapsuleRadius") },
	{ TEXT("UCapsuleComponent"), TEXT("RelativeLocation"), TEXT("GetRelativeLocation"), TEXT("SetRelativeLocation") },
	{ TEXT("UCapsuleComponent"), TEXT("RelativeRotation"), TEXT("GetRelativeRotation"), TEXT("SetRelativeRotation") },
	{ TEXT("UCapsuleComponent"), TEXT("CollisionEnabled"), TEXT("GetCollisionEnabled"), TEXT("SetCollisionEnabled") },
	{ TEXT("UCapsuleComponent"), TEXT("CollisionProfileName"), TEXT("GetCollisionProfileName"), TEXT("SetCollisionProfileName") },
	{ TEXT("UCapsuleComponent"), TEXT("AreaClassOverride"), TEXT("GetAreaClassOverride"), TEXT("SetAreaClassOverride") },
	{ TEXT("UCapsuleComponent"), TEXT("bUseSystemDefaultObstacleAreaClass"), TEXT(""), TEXT("") },


	{ TEXT("ACharacter"), TEXT("Mesh"), TEXT("GetMesh"), TEXT("SetSkeletalMesh") },
	{ TEXT("ACharacter"), TEXT("CapsuleComponent"), TEXT("GetCapsuleComponent"), TEXT("SetCapsuleComponent") },
	{ TEXT("ACharacter"), TEXT("ActorHiddenInGame"), TEXT("IsHidden"), TEXT("SetActorHiddenInGame") },
	{ TEXT("ACharacter"), TEXT("AutoPossessPlayer"), TEXT("GetAutoPossessPlayer"), TEXT("SetAutoPossessPlayer") },
	{ TEXT("ACharacter"), TEXT("UseControllerRotationYaw"), TEXT("IsUsingControllerRotationYaw"), TEXT("SetUseControllerRotationYaw") },
	{ TEXT("ACharacter"), TEXT("CharacterMovement"), TEXT("GetCharacterMovement"), TEXT("SetCharacterMovement") },
	{ TEXT("ACharacter"), TEXT("CharacterMovement"), TEXT("GetArrowComponent"), TEXT("SetArrowComponent") },
	{ TEXT("ACharacter"), TEXT("AnimClass"), TEXT("GetMeshAnimClass"), TEXT("SetMeshAnimClass") },


	{ TEXT("UCharacterMovementComponent"), TEXT("WalkableFloorAngle"), TEXT("GetWalkableFloorAngle"), TEXT("SetWalkableFloorAngle") },


	{ TEXT("UAnimInstance"), TEXT("RootMotionMode"), TEXT("GetRootMotionMode"), TEXT("SetRootMotionMode") },
	{ TEXT("UAnimInstance"), TEXT("NativeUpdateRate"), TEXT("GetNativeUpdateRate"), TEXT("SetNativeUpdateRate") },


	{ TEXT("UPrimitiveComponent"), TEXT("Mobility"), TEXT("GetComponentMobility"), TEXT("SetMobility") },
	{ TEXT("UPrimitiveComponent"), TEXT("bVisible"), TEXT("IsVisible"), TEXT("SetVisibility") },
	{ TEXT("UPrimitiveComponent"), TEXT("bCastShadow"), TEXT("GetCastShadow"), TEXT("SetCastShadow") },


	{ TEXT("USceneComponent"), TEXT("ComponentTransform"), TEXT("GetComponentTransform"), TEXT("SetWorldTransform") },

	{ TEXT("AActor"), TEXT("Tags"), TEXT("GetTags"), TEXT("SetTags") },
	};

	ColorEditableTextGroups =
	{
		FColorEditableKeywordGroup
		{
			FLinearColor(0.0f, 0.5f, 1.0f),  // ������ ����� ��� �������� ����
			{
				TEXT("if"),
				TEXT("else"),
				TEXT("while"),
				TEXT("for"),
				TEXT("return"),
				TEXT("switch"),
				TEXT("break"),
				TEXT("continue"),
				TEXT("class"),
				TEXT("public"),
				TEXT("static"),
				TEXT("private"),
				TEXT("protected"),
				TEXT("this"),
				TEXT("super"),
				TEXT("function"),
				TEXT("new"),
				TEXT("const"),
				TEXT("true"),
				TEXT("false"),
				TEXT("null"),
				TEXT("NULL"), 
				TEXT("virtual"),
				TEXT("override")
			}
		},
		FColorEditableKeywordGroup
		{
			FLinearColor(0.0f, 0.8f, 0.0f),  // ������ ����� ��� ����������� �����
			{
				TEXT("int"),
				TEXT("float"),
				TEXT("void"),
				TEXT("bool"),
				TEXT("char"),
				TEXT("unsigned"),
				TEXT("long"),
				TEXT("short"),
				TEXT("double"),
				TEXT("FString"),
				TEXT("TArray"),
				TEXT("TMap"),
				TEXT("TSet"),
				TEXT("UObject"),
				TEXT("AActor"),
				TEXT("UClass"),
				TEXT("enum"),
				TEXT("struct"),
			}
		},
		FColorEditableKeywordGroup
		{
			FLinearColor(1.0f, 0.0f, 1.0f),
			{
				TEXT("UPROPERTY"),
				TEXT("UFUNCTION"),
				TEXT("UCLASS"),
				TEXT("GENERATED_BODY()"),
				TEXT("USTRUCT"),
				TEXT("UENUM"),
				TEXT("UINTERFACE"),
				TEXT("UDELEGATE"),
				TEXT("UPARAM")
			}
		}
	};

	ColorEditableTextOperators  =
	{
		TEXT("/*"),
		TEXT("*/"),
		TEXT("//"),
		TEXT("\""),
		TEXT("\'"),
		TEXT("::"),
		TEXT(":"),
		TEXT("+="),
		TEXT("++"),
		TEXT("+"),
		TEXT("--"),
		TEXT("-="),
		TEXT("-"),
		TEXT("("),
		TEXT(")"),
		TEXT("["),
		TEXT("]"),
		TEXT("."),
		TEXT("->"),
		TEXT("!="),
		TEXT("!"),
		TEXT("&="),
		TEXT("~"),
		TEXT("&"),
		TEXT("*="),
		TEXT("*"),
		TEXT("->"),
		TEXT("/="),
		TEXT("/"),
		TEXT("%="),
		TEXT("%"),
		TEXT("<<="),
		TEXT("<<"),
		TEXT("<="),
		TEXT("<"),
		TEXT(">>="),
		TEXT(">>"),
		TEXT(">="),
		TEXT(">"),
		TEXT("=="),
		TEXT("&&"),
		TEXT("&"),
		TEXT("^="),
		TEXT("^"),
		TEXT("|="),
		TEXT("||"),
		TEXT("|"),
		TEXT("?"),
		TEXT("="),

		// chuck operators
		TEXT("++"),
		TEXT("--"),
		TEXT(":"),
		TEXT("+"),
		TEXT("-"),
		TEXT("*"),
		TEXT("/"),
		TEXT("%"),
		TEXT("::"),
		TEXT("=="),
		TEXT("!="),
		TEXT("<"),
		TEXT(">"),
		TEXT("<="),
		TEXT(">="),
		TEXT("&&"),
		TEXT("||"),
		TEXT("&"),
		TEXT("|"),
		TEXT("^"),
		TEXT(">>"),
		TEXT("<<"),
		TEXT("="),
		TEXT("?"),
		TEXT("!"),
		TEXT("~"),
		TEXT("<<<"),
		TEXT(">>>"),
		TEXT("=>"),
		TEXT("!=>"),
		TEXT("=^"),
		TEXT("=v"),
		TEXT("@=>"),
		TEXT("+=>"),
		TEXT("-=>"),
		TEXT("*=>"),
		TEXT("/=>"),
		TEXT("&=>"),
		TEXT("|=>"),
		TEXT("^=>"),
		TEXT(">>=>"),
		TEXT("<<=>"),
		TEXT("%=>"),
		TEXT("@"),
		TEXT("@@"),
		TEXT("->"),
		TEXT("<-"),
		TEXT(".")
	};




	IgnoreClassToRefGenerate = {UUserWidget::StaticClass(), UAnimInstance::StaticClass(), UControlRig::StaticClass() };
}

bool UBlueprintNativizationSettingsLibrary::FindNativeByClassAndName(UClass* OwnerClass, FString OwnerName, FFunctionDescriptor& OutNativeDesc)
{
	if (!OwnerClass)
	{
		return false;
	}

	const UBlueprintNativizationV2EditorSettings* Settings = GetDefault<UBlueprintNativizationV2EditorSettings>();

	TArray<UClass*> ClassChain;
	for (UClass* CurrentClass = OwnerClass; CurrentClass != nullptr; CurrentClass = CurrentClass->GetSuperClass())
	{
		if (!CurrentClass->IsChildOf<UBlueprintGeneratedClass>())
		{
			ClassChain.Add(CurrentClass);
		}
	}


	for (const FFunctionBinding& Binding : Settings->FunctionRedirects)
	{
		for (UClass* ClassInChain : ClassChain)
		{
			if (UBlueprintNativizationLibrary::GetUniqueFieldName(ClassInChain) == Binding.BlueprintFunction.ClassName)
			{
				if (Binding.BlueprintFunction.FunctionName == OwnerName)
				{
					OutNativeDesc.ClassName = Binding.NativeFunction.ClassName;
					OutNativeDesc.FunctionName = Binding.NativeFunction.FunctionName;
					OutNativeDesc.ParameterSignature = Binding.NativeFunction.ParameterSignature;

					return true;
				}
			}
		}
	}

	return false;
}

bool UBlueprintNativizationSettingsLibrary::FindNativeByBlueprint(const FFunctionDescriptor BlueprintDesc, FFunctionDescriptor& OutNativeDesc)
{
	const UBlueprintNativizationV2EditorSettings* Settings = GetDefault<UBlueprintNativizationV2EditorSettings>();

	for (const FFunctionBinding& Binding : Settings->FunctionRedirects)
	{
		if (Binding.BlueprintFunction == BlueprintDesc)
		{
			OutNativeDesc = Binding.NativeFunction;
			return true;
		}
	}

	return false;
}

bool UBlueprintNativizationSettingsLibrary::FindConstructorDescriptorByStruct(UStruct* OwnerStruct, FConstructorDescriptor& OutNativeDesc)
{
	const UBlueprintNativizationV2EditorSettings* Settings = GetDefault<UBlueprintNativizationV2EditorSettings>();
	if (OwnerStruct->IsNative())
	{
		FString Name = UBlueprintNativizationLibrary::GetUniqueFieldName(OwnerStruct);

		for (FConstructorDescriptor ConstructorDescriptor : Settings->ConstructorDescriptors)
		{
			if (ConstructorDescriptor.OriginalStructName == Name)
			{
				OutNativeDesc = ConstructorDescriptor;
				return true;
			}
		}
	}
	return false;
}

bool UBlueprintNativizationSettingsLibrary::FindGetterAndSetterDescriptorDescriptorByPropertyName(FProperty* Property, FGetterAndSetterPropertyDescriptor& OutGetterAndSetterDescriptorDesc)
{
	const UBlueprintNativizationV2EditorSettings* Settings = GetDefault<UBlueprintNativizationV2EditorSettings>();

	for (FGetterAndSetterPropertyDescriptor GetterAndSetterDescriptor : Settings->GetterAndSetterDescriptors)
	{
		if (GetterAndSetterDescriptor.OriginalPropertyName == Property->GetNameCPP())
		{
			OutGetterAndSetterDescriptorDesc = GetterAndSetterDescriptor;
			return true;
		}
	}
	return false;
}

bool UBlueprintNativizationSettingsLibrary::FindColorByNameInColorTextGroup(FString Name, FLinearColor& LinearColor)
{
	const UBlueprintNativizationV2EditorSettings* Settings = GetDefault<UBlueprintNativizationV2EditorSettings>();

	for (FColorEditableKeywordGroup ColorEditableKeywordGroup : Settings->ColorEditableTextGroups)
	{
		if (ColorEditableKeywordGroup.Names.Contains(Name))
		{
			LinearColor = ColorEditableKeywordGroup.Color;
			return true;
		}
	}
	return false;
}
