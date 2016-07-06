// Fill out your copyright notice in the Description page of Project Settings.

#include "RoCoG.h"
#include "RCGUtils.h"
#include "RCGGraspPiano.h" //TODO remove this?
#include "RCGCharacter.h"


// Sets default values
ARCGCharacter::ARCGCharacter(const FObjectInitializer& ObjectInitializer)
{
	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	// Visualize motion controller debug arrows
	bVisTargetArrows = true;

	// Bone names
	LeftControlBoneName = FName("palm_l");
	RightControlBoneName = FName("palm_r");

	// TODO check that bones exist (in begin play)
	// Left hands finger collision (sensor) names
	LeftCollisionBoneNames.Add(FName("palm_l"));
	LeftCollisionBoneNames.Add(FName("index_03_l"));
	LeftCollisionBoneNames.Add(FName("middle_03_l"));
	LeftCollisionBoneNames.Add(FName("ring_03_l"));
	LeftCollisionBoneNames.Add(FName("pinky_03_l"));
	LeftCollisionBoneNames.Add(FName("thumb_03_l"));
	// Right hands finger collision (sensor) names
	RightCollisionBoneNames.Add(FName("palm_r"));
	RightCollisionBoneNames.Add(FName("index_03_r"));
	RightCollisionBoneNames.Add(FName("middle_03_r"));
	RightCollisionBoneNames.Add(FName("ring_03_r"));
	RightCollisionBoneNames.Add(FName("pinky_03_r"));
	RightCollisionBoneNames.Add(FName("thumb_03_r"));

	// PID params
	PGain = 140.0f;
	IGain = 0.0f;
	DGain = 20.0f;
	PIDMaxOutput = 1500.0f;
	PIDMinOutput = -1500.0f;

	// Rotation control param
	RotOutStrength = 1000.0f;

	// Joint control params
	Spring = 59950000.0f;
	Damping = 59950000.0f;
	ForceLimit = 0.0f;
	Velocity = 0.1f;

	// Finger types
	FingerTypes.Add(ERCGHandLimb::Index);
	FingerTypes.Add(ERCGHandLimb::Middle);
	FingerTypes.Add(ERCGHandLimb::Ring);
	FingerTypes.Add(ERCGHandLimb::Pinky);
	FingerTypes.Add(ERCGHandLimb::Thumb);

	// Make the capsule thin
	GetCapsuleComponent()->SetCapsuleRadius(5);

	// Set this pawn to be controlled by the lowest-numbered player
	AutoPossessPlayer = EAutoReceiveInput::Player0;

	// Create a CameraComponent, attach to capsule
	CharCamera = ObjectInitializer.CreateDefaultSubobject<UCameraComponent>(this, TEXT("CharacterCamera"));
	CharCamera->SetupAttachment(GetCapsuleComponent());
	// Position the camera
	CharCamera->RelativeLocation = FVector(0.0f, 0.0f, BaseEyeHeight);
	// Allow the pawn to control the camera rotation
	CharCamera->bUsePawnControlRotation = true;

	// Get root component
	RootComponent = GetRootComponent();
	// Create the motion controller offset (hands in front of the character)
	MCOffset = CreateDefaultSubobject<USceneComponent>(TEXT("MCOffsetComponent"));
	// Attach Offset to root
	MCOffset->SetupAttachment(RootComponent);
	// Position of the offset
	MCOffset->RelativeLocation = FVector(80.0f, 0.0f, 0.0f);

	// Create left/right motion controller
	LeftMC = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("LeftMotionController"));
	RightMC = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("RightMotionController"));
	// Attach controllers to root component
	LeftMC->SetupAttachment(MCOffset);
	RightMC->SetupAttachment(MCOffset);
	// Set the mapped hand (from the Motion Controller)
	LeftMC->Hand = EControllerHand::Left;
	RightMC->Hand = EControllerHand::Right;

	// Create left/right target vis arrows
	LeftTargetArrow = CreateDefaultSubobject<UArrowComponent>(TEXT("LeftVisArrow"));
	RightTargetArrow = CreateDefaultSubobject<UArrowComponent>(TEXT("RightVisArrow"));
	// Set arrow sizes
	LeftTargetArrow->ArrowSize = 0.1;
	RightTargetArrow->ArrowSize = 0.1;
	// Attach vis arrow to motion controllers
	LeftTargetArrow->SetupAttachment(LeftMC);
	RightTargetArrow->SetupAttachment(RightMC);

	// Default type of the hands
	LeftHand = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("LeftSkeletalMesh"));
	RightHand = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("RightSkeletalMesh"));
	// Get default skeletal mesh of the right / left hands
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> LeftSkelMesh(TEXT(
		"SkeletalMesh'/Game/Models/Hands/LeftHand/LeftHand.LeftHand'"));
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> RightSkelMesh(TEXT(
		"SkeletalMesh'/Game/Models/Hands/RightHand/RightHand.RightHand'"));
	// Set default skeletal mesh
	LeftHand->SetSkeletalMesh(LeftSkelMesh.Object);
	RightHand->SetSkeletalMesh(RightSkelMesh.Object);
	// Simulate physics
	LeftHand->SetSimulatePhysics(true);
	RightHand->SetSimulatePhysics(true);
	// Disable gravity
	LeftHand->SetEnableGravity(false);
	RightHand->SetEnableGravity(false);
	// Collision default
	LeftHand->SetCollisionProfileName(TEXT("BLockAll"));
	RightHand->SetCollisionProfileName(TEXT("BLockAll"));
	// Attach hands to the motion controller offset parent
	LeftHand->SetupAttachment(MCOffset);
	RightHand->SetupAttachment(MCOffset);

	// Make sure collision hit events are enabled on the hands
	LeftHand->SetNotifyRigidBodyCollision(true);
	RightHand->SetNotifyRigidBodyCollision(true);

	// Initialize grasp type
	LeftGrasp = new FRCGGrasp(LFingerTypeToConstrs);
	RightGrasp = new FRCGGrasp(RFingerTypeToConstrs);
}

// Called when the game starts or when spawned
void ARCGCharacter::BeginPlay()
{
	Super::BeginPlay();
	
	if (bVisTargetArrows)
	{
		// Set arrows to visible
		LeftTargetArrow->SetHiddenInGame(false);
		RightTargetArrow->SetHiddenInGame(false);
	}

	// Linear movement PIDs
	LeftPID3D = FRCGPid3d(PGain, IGain, DGain, PIDMaxOutput, PIDMinOutput);
	RightPID3D = FRCGPid3d(PGain, IGain, DGain, PIDMaxOutput, PIDMinOutput);

	// Get the bodies to apply forces on for pose control
	LeftControlBody = LeftHand->GetBodyInstance(LeftControlBoneName);
	RightControlBody = RightHand->GetBodyInstance(RightControlBoneName);
	// Check if the bodies not set
	if (!LeftControlBody || !RightControlBody)
	{
		UE_LOG(LogTemp, Error,
			TEXT("LeftControlBody or RightControlBody not set, check if [%s] or [%s] exists!"),
			*LeftControlBoneName.ToString(), *RightControlBoneName.ToString());
	}
	
	// Hand joint velocity drive
	LeftHand->SetAllMotorsAngularPositionDrive(true, true);
	RightHand->SetAllMotorsAngularPositionDrive(true, true);

	// Set drive parameters
	LeftHand->SetAllMotorsAngularDriveParams(Spring, Damping, ForceLimit);
	RightHand->SetAllMotorsAngularDriveParams(Spring, Damping, ForceLimit);
	
	// Lambda to map the hand fingers constraints to its type (index, middle etc.)
	auto GetFingerToConstrLambda = [&](TArray<FConstraintInstance*>& Constraints)
	{
		// Finger type to contraint map
		TMultiMap<ERCGHandLimb, FConstraintInstance*> FingersMap;
		// Iterate constraints
		for (FConstraintInstance* Constr : Constraints)
		{
			// Current constraint joint name
			const FString ConstrName = Constr->JointName.ToString();
			// Iterate hand limbs type
			for (const ERCGHandLimb Type : FingerTypes)
			{
				// Get the enum type as string
				const FString TypeName = FRCGUtils::GetEnumValueToString<ERCGHandLimb>("ERCGHandLimb", Type);
				// Add to finger map if the constraint name matches the finger type (name)
				if (ConstrName.Contains(TypeName))
				{
					FingersMap.Add(Type, Constr);
				}
			}
		}
		return FingersMap;
	};

	// Get the left and right hand finger constraints
	LFingerTypeToConstrs = GetFingerToConstrLambda(LeftHand->Constraints);
	RFingerTypeToConstrs = GetFingerToConstrLambda(RightHand->Constraints);
	
	// Get the hands finger collisions map, make sure hit events are set
	auto GetFingerCollisionBonesLambda = [](USkeletalMeshComponent* Hand, TArray<FName>& CollisionBoneNames)
	{
		// Hand finger bone name to its body Map
		TMap<FName, FBodyInstance*> FingerBoneNameToBodyMap;
		// Iterate the bone names
		for (auto BoneName : CollisionBoneNames)
		{
			// Get the body instance
			FBodyInstance* BoneBody = Hand->GetBodyInstance(BoneName);
			// Make sure the body returns hit events
			BoneBody->bNotifyRigidBodyCollision = true;
			// Add to map
			FingerBoneNameToBodyMap.Add(BoneName, BoneBody);
		}
		// Return the map
		return FingerBoneNameToBodyMap;
	};

	// Get the left / right hand finger sensor collision map
	LFingerBoneNameToBody = GetFingerCollisionBonesLambda(LeftHand, LeftCollisionBoneNames);
	RFingerBoneNameToBody = GetFingerCollisionBonesLambda(RightHand, RightCollisionBoneNames);
	
	// Add bone names to limb type Map
	auto AddBoneNamesToLimbsLambda = [&](TArray<FName> BoneNames, TMap<FName, ERCGHandLimb>& NameToLimbMap)
	{
		// Iterate the bone names
		for (auto Name : BoneNames)
		{
			// Get bone name as string
			const FString NameStr = Name.ToString();

			// Iterate hand limbs type
			for (const ERCGHandLimb Type : FingerTypes)
			{
				// Get the enum type as string
				const FString TypeName = FRCGUtils::GetEnumValueToString<ERCGHandLimb>("ERCGHandLimb", Type);
				// Add to finger map if the constraint name matches the finger type (name)
				if (NameStr.Contains(TypeName))
				{
					NameToLimbMap.Add(Name, Type);
				}
			}
		}
	};

	// Create finger bone names to hand limb type Map
	AddBoneNamesToLimbsLambda(LeftCollisionBoneNames, BoneNameToLimbMap);
	AddBoneNamesToLimbsLambda(RightCollisionBoneNames, BoneNameToLimbMap);

	// Set init location of the control body
	LeftCurrLoc = LeftHand->GetBoneLocation(LeftControlBoneName);
	LeftCurrQuat = LeftHand->GetBoneQuaternion(LeftControlBoneName);
	RightCurrLoc = RightHand->GetBoneLocation(RightControlBoneName);
	RightCurrQuat = RightHand->GetBoneQuaternion(RightControlBoneName);

	// Collision callbacks for the hands
	LeftHand->OnComponentHit.AddDynamic(this, &ARCGCharacter::OnHitLeft);
	RightHand->OnComponentHit.AddDynamic(this, &ARCGCharacter::OnHitRight);
}

// Called every frame
void ARCGCharacter::Tick( float DeltaTime )
{
	Super::Tick( DeltaTime );

	//// Compute location
	// Get the target location (motion controller loc)
	const FVector LeftTargetLoc = LeftMC->GetComponentLocation();
	const FVector RightTargetLoc = RightMC->GetComponentLocation();

	// Get the loc errors
	const FVector LeftError = LeftTargetLoc - LeftCurrLoc;
	const FVector RightError = RightTargetLoc - RightCurrLoc;

	// Compute the pos output
	const FVector LeftLocOutput = LeftPID3D.Update(LeftError, DeltaTime);
	const FVector RightLocOutput = RightPID3D.Update(RightError, DeltaTime);

	// Apply force to the hands control body 
	LeftControlBody->AddForce(FVector(LeftLocOutput));
	RightControlBody->AddForce(FVector(RightLocOutput));

	// Set the current location of the control bodies
	LeftCurrLoc = LeftHand->GetBoneLocation(LeftControlBoneName);
	RightCurrLoc = RightHand->GetBoneLocation(RightControlBoneName);

	//// Compute rotation
	// Get the target rotation (motion controller loc)
	const FQuat LeftTargetQuat = LeftMC->GetComponentQuat();
	const FQuat RightTargetQuat = RightMC->GetComponentQuat();

	// Lambda to compute the rotational output velocity
	auto RotOutputLambda = [&](const FQuat& TargetQuat, FQuat CurrQuat)
	{
		// Dot product to get costheta
		const float CosTheta = TargetQuat | CurrQuat;
		// Avoid taking the long path around the sphere
		if (CosTheta < 0)
		{
			CurrQuat = CurrQuat * (-1.0f);
		}
		// Use the xyz part of the quat as the rotation velocity
		const FQuat OutputAsQuat = TargetQuat * CurrQuat.Inverse();
		return FVector(OutputAsQuat.X, OutputAsQuat.Y, OutputAsQuat.Z) * RotOutStrength;
	};

	// Compute rotation output
	const FVector LeftOuputRot = RotOutputLambda(LeftTargetQuat, LeftCurrQuat);
	const FVector RightOuputRot = RotOutputLambda(RightTargetQuat, RightCurrQuat);

	// Apply torque/angularvel to the hands control body 
	LeftControlBody->SetAngularVelocity(LeftOuputRot, false);
	RightControlBody->SetAngularVelocity(RightOuputRot, false);

	// Set the current rotation of the control bodies
	LeftCurrQuat = LeftHand->GetBoneQuaternion(LeftControlBoneName);
	RightCurrQuat = RightHand->GetBoneQuaternion(RightControlBoneName);
}

// Called to bind functionality to input
void ARCGCharacter::SetupPlayerInputComponent(class UInputComponent* InputComponent)
{
	Super::SetupPlayerInputComponent(InputComponent);

	// Set up gameplay key bindings
	InputComponent->BindAxis("MoveForward", this, &ARCGCharacter::MoveForward);
	InputComponent->BindAxis("MoveRight", this, &ARCGCharacter::MoveRight);
	// Default Camera view bindings
	InputComponent->BindAxis("CameraPitch", this, &ARCGCharacter::AddControllerPitchInput);
	InputComponent->BindAxis("CameraYaw", this, &ARCGCharacter::AddControllerYawInput);
	// Set up hand bindings
	InputComponent->BindAxis("OpenHandLeft", this, &ARCGCharacter::OpenHandLeft);
	InputComponent->BindAxis("CloseHandLeft", this, &ARCGCharacter::CloseHandLeft);
	InputComponent->BindAxis("OpenHandRight", this, &ARCGCharacter::OpenHandRight);
	InputComponent->BindAxis("CloseHandRight", this, &ARCGCharacter::CloseHandRight);
	// TODO TEST
	// Set up grasp switch
	InputComponent->BindAction("SwitchGrasp", IE_Pressed, this, &ARCGCharacter::OnSwitchGrasp);
}

// Handles moving forward/backward
void ARCGCharacter::MoveForward(const float Value)
{
	if ((Controller != nullptr) && (Value != 0.0f))
	{
		// find out which way is forward
		FRotator Rotation = Controller->GetControlRotation();
		// Limit pitch when walking or falling
		if (GetCharacterMovement()->IsMovingOnGround() || GetCharacterMovement()->IsFalling())
		{
			Rotation.Pitch = 0.0f;
		}
		// add movement in that direction
		const FVector Direction = FRotationMatrix(Rotation).GetScaledAxis(EAxis::X);
		AddMovementInput(Direction, Value * 0.1);
	}
}

// Handles moving right/left
void ARCGCharacter::MoveRight(const float Value)
{
	if ((Controller != nullptr) && (Value != 0.0f))
	{
		// find out which way is right
		const FRotator Rotation = Controller->GetControlRotation();
		const FVector Direction = FRotationMatrix(Rotation).GetScaledAxis(EAxis::Y);
		// add movement in that direction
		AddMovementInput(Direction, Value * 0.1);
	}
}

// Called to bind functionality to input
void ARCGCharacter::CloseHandLeft(const float AxisValue)
{
	if (AxisValue == 0)
	{
		return;
	}

	// Update grasping
	LeftGrasp->Update(LFingerTypeToConstrs, 0.5f);
}

// Called to bind functionality to input
void ARCGCharacter::OpenHandLeft(const float AxisValue)
{
	if (AxisValue == 0)
	{
		// Grasp action inactive
		return;
	}

	// Update grasping
	LeftGrasp->Update(LFingerTypeToConstrs, -0.5f);
}

// Called to bind functionality to input
void ARCGCharacter::CloseHandRight(const float AxisValue)
{
	if (AxisValue == 0)
	{
		return;
	}

	// Update grasping
	RightGrasp->Update(RFingerTypeToConstrs, 0.5f);
}

// Called to bind functionality to input
void ARCGCharacter::OpenHandRight(const float AxisValue)
{
	if (AxisValue == 0)
	{
		// Grasp action inactive
		return;
	}

	// Update grasping
	RightGrasp->Update(RFingerTypeToConstrs, -0.5f);
}


// Collision callback
void ARCGCharacter::OnHitLeft(UPrimitiveComponent* SelfComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	//UE_LOG(LogTemp, Warning, TEXT("**LEFT** SelfComp: %s, OtherActor: %s, OtherComp: %s"),
	//	*SelfComp->GetName(), *OtherActor->GetName(), *OtherComp->GetName());

	//UE_LOG(LogTemp, Warning, TEXT("BoneName: %s, ts: %f"), *Hit.BoneName.ToString(), GetWorld()->GetDeltaSeconds());

	ERCGHandLimb* Limb = BoneNameToLimbMap.Find(Hit.BoneName);

	if (Limb != nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("Blocked %s, Limb: %s"), *Hit.BoneName.ToString(), *FRCGUtils::GetEnumValueToString<ERCGHandLimb>("ERCGHandLimb", *Limb));
		
		LeftGrasp->BlockFinger(*Limb);
	}


	//UE_LOG(LogTemp, Warning, TEXT("HIT %s"), *Hit.BoneName.ToString());

	////ERCGHandLimb Limb = BoneNameToLimbMap.Find(Hit.BoneName);

	//ERCGHandLimb* Limb2 = BoneNameToLimbMap.Find(Hit.BoneName);

	//if (BoneNameToLimbMap.Contains(Hit.BoneName))
	//{
	//	ERCGHandLimb Limb = BoneNameToLimbMap[Hit.BoneName];
	//	UE_LOG(LogTemp, Warning, TEXT("CONTAINS"));

	//	UE_LOG(LogTemp, Warning, TEXT("Blocked %s, Limb: %s"), *Hit.BoneName.ToString(), *FUtils::GetEnumValueToString<ERCGHandLimb>("ERCGHandLimb", Limb));

	//}
}

// Collision callback
void ARCGCharacter::OnHitRight(UPrimitiveComponent* SelfComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	//UE_LOG(LogTemp, Warning, TEXT("**RIGHT** SelfComp: %s, OtherActor: %s, OtherComp: %s"),
	//	*SelfComp->GetName(), *OtherActor->GetName(), *OtherComp->GetName());

	//	*SelfComp->GetName(), *OtherActor->GetName(), *OtherComp->GetName(), *Hit.ToString());
	//UE_LOG(LogTemp, Warning, TEXT("OtherActor: %s, OtherComp: %s, Hit: %s"),
	//	*OtherActor->GetName(), *OtherComp->GetName(), *Hit.ToString());

	//UE_LOG(LogTemp, Warning, TEXT("BoneName: %s, ts: %f"), *Hit.BoneName.ToString(), GetWorld()->GetDeltaSeconds());
}

// Swith between grasping styles
void ARCGCharacter::OnSwitchGrasp()
{
	UE_LOG(LogTemp, Warning, TEXT("Switch!"));

	LeftGrasp = dynamic_cast<FRCGGrasp*>(new FRCGGraspPiano(LFingerTypeToConstrs));
	RightGrasp = dynamic_cast<FRCGGrasp*>(new FRCGGraspPiano(RFingerTypeToConstrs));
}