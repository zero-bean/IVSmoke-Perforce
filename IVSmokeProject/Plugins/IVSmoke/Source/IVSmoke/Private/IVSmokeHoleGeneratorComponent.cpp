// Copyright SDB. All Rights Reserved.

#include "IVSmokeHoleGeneratorComponent.h"
#include "IVSmokeDebugSceneViewExtension.h"
#include "IVSmokeVolumeDebugRenderer.h"
#include "IVSmokeHoleCarveCS.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "GlobalShader.h"
#include "IVSmoke.h"

#if ENABLE_DRAW_DEBUG
#include "DrawDebugHelpers.h"
#endif

UIVSmokeHoleGeneratorComponent::UIVSmokeHoleGeneratorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetGenerateOverlapEvents(true);
	InitBoxExtent(FVector(200.0, 200.0, 200.0));
}

UIVSmokeHoleGeneratorComponent::~UIVSmokeHoleGeneratorComponent()
{
	ReleaseHoleTexture();
}

void UIVSmokeHoleGeneratorComponent::BeginPlay()
{
	Super::BeginPlay();

	ActiveHoles.Reserve(MaxHoles);
	InitializeHoleTexture();

	if (bAutoDetectProjectiles)
	{
		OnComponentBeginOverlap.AddDynamic(this, &UIVSmokeHoleGeneratorComponent::OnVolumeBeginOverlap);
	}

	FIVSmokeVolumeDebugRenderer::Get().Register(this);
	FIVSmokeDebugSceneViewExtension::Initialize();
}

void UIVSmokeHoleGeneratorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	FIVSmokeVolumeDebugRenderer::Get().Unregister(this);
	Super::EndPlay(EndPlayReason);
}

void UIVSmokeHoleGeneratorComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const double CurrentTime = GetWorld()->GetTimeSeconds();

	CleanupExpiredHoles(CurrentTime);
	DispatchHoleCarveCS(CurrentTime);

	if (bShowVolumeDebug)
	{
		FIVSmokeVolumeDebugRenderer::Get().UpdateRenderData(this);

#if ENABLE_DRAW_DEBUG
		DrawDebugBox(GetWorld(), GetComponentLocation(), GetScaledBoxExtent(), FColor::Cyan, false, -1.0f, 0, 2.0f);
#endif
	}
}

void UIVSmokeHoleGeneratorComponent::CreateHole(const FVector& Position, const FVector& Direction, const double Radius)
{
	if (ActiveHoles.Num() >= MaxHoles)
	{
		ActiveHoles.RemoveAt(0);
	}

	ActiveHoles.Emplace(Position, Direction, Radius, GetWorld()->GetTimeSeconds());
}

void UIVSmokeHoleGeneratorComponent::OnVolumeBeginOverlap(UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	if (!bAutoDetectProjectiles || !OtherActor)
	{
		return;
	}

	if (const FVector Velocity = OtherActor->GetVelocity(); Velocity.SizeSquared() > KINDA_SMALL_NUMBER)
	{
		const FVector HitPosition = bFromSweep ? FVector(SweepResult.Location) : OtherActor->GetActorLocation();
		CreateHole(HitPosition, Velocity.GetSafeNormal(), DefaultHoleRadius);
	}
}

// ============================================================================
// Hole Texture Management
// ============================================================================

void UIVSmokeHoleGeneratorComponent::InitializeHoleTexture()
{
	if (VoxelResolution.X <= 0 || VoxelResolution.Y <= 0 || VoxelResolution.Z <= 0)
	{
		return;
	}

	const int32 TotalVoxels = VoxelResolution.X * VoxelResolution.Y * VoxelResolution.Z;
	TArray<float> InitialData;
	InitialData.SetNumUninitialized(TotalVoxels * 4);
	for (int32 i = 0; i < TotalVoxels; ++i)
	{
		InitialData[i * 4 + 0] = 1.0f;  // R: Density (1.0 = full smoke)
		InitialData[i * 4 + 1] = 0.0f;  // G: CreationTime
		InitialData[i * 4 + 2] = 0.0f;  // B: Reserved
		InitialData[i * 4 + 3] = 1.0f;  // A: Reserved
	}

	FTextureRHIRef* TexturePtr = &HoleTexture;
	FIntVector Resolution = VoxelResolution;

	ENQUEUE_RENDER_COMMAND(CreateHoleTexture)(
		[TexturePtr, Resolution, InitialData = MoveTemp(InitialData)](FRHICommandListImmediate& RHICmdList)
		{
			const FRHITextureCreateDesc Desc =
				FRHITextureCreateDesc::Create3D(TEXT("IVSmokeHoleTexture"), Resolution.X, Resolution.Y, Resolution.Z, PF_A32B32G32R32F)
				.SetFlags(ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV)
				.SetInitialState(ERHIAccess::CopyDest);

			*TexturePtr = RHICreateTexture(Desc);

			if (TexturePtr->IsValid())
			{
				FUpdateTextureRegion3D Region(0, 0, 0, 0, 0, 0, Resolution.X, Resolution.Y, Resolution.Z);
				const uint32 SourceRowPitch = Resolution.X * 4 * sizeof(float);
				const uint32 SourceDepthPitch = Resolution.X * Resolution.Y * 4 * sizeof(float);

				RHIUpdateTexture3D(*TexturePtr, 0, Region, SourceRowPitch, SourceDepthPitch,
					reinterpret_cast<const uint8*>(InitialData.GetData()));
				RHICmdList.Transition(FRHITransitionInfo(*TexturePtr, ERHIAccess::CopyDest, ERHIAccess::SRVGraphics));
			}
		}
	);

	FlushRenderingCommands();
}

void UIVSmokeHoleGeneratorComponent::ReleaseHoleTexture()
{
	if (HoleTexture.IsValid())
	{
		FTextureRHIRef TextureToRelease = HoleTexture;
		ENQUEUE_RENDER_COMMAND(ReleaseHoleTexture)(
			[TextureToRelease](FRHICommandListImmediate& RHICmdList)
			{
				// TextureToRelease released when lambda exits
			}
		);
		HoleTexture = nullptr;
	}
}

void UIVSmokeHoleGeneratorComponent::CleanupExpiredHoles(double CurrentTime)
{
	ActiveHoles.RemoveAll([this, CurrentTime](const FIVSmokeHoleData& Hole)
	{
		return (CurrentTime - Hole.CreationTime) > HoleLifeTime;
	});
}

// ============================================================================
// GPU Compute Shader Dispatch
// ============================================================================

void UIVSmokeHoleGeneratorComponent::DispatchHoleCarveCS(double CurrentTime)
{
	if (!HoleTexture.IsValid())
	{
		return;
	}

	if (ActiveHoles.Num() == 0 && !bNeedsClear)
	{
		return;
	}

	ActiveHoles.Num() > 0 ? bNeedsClear = true : bNeedsClear = false;

	TArray<FIVSmokeHoleTrajectoryGPU> Trajectories;
	TArray<FIVSmokeControlPointGPU> ControlPoints;
	Trajectories.Reserve(FMath::Max(ActiveHoles.Num(), 1));
	ControlPoints.Reserve(FMath::Max(ActiveHoles.Num() * 2, 1));

	const FTransform ComponentTransform = GetComponentTransform();
	const FVector VolumeExtent = GetUnscaledBoxExtent();
	const float MaxTraceDistance = VolumeExtent.Size() * 2.0f; // roughly set root 2

	for (const FIVSmokeHoleData& Hole : ActiveHoles)
	{
		const FVector StartLocalPos = ComponentTransform.InverseTransformPosition(Hole.Position);
		const FVector LocalDirection = ComponentTransform.InverseTransformVectorNoScale(Hole.Direction.GetSafeNormal());
		const FVector EndLocalPos = StartLocalPos + LocalDirection * MaxTraceDistance;

		const float StartRadius = static_cast<float>(Hole.Radius);
		const float EndRadius = StartRadius * EndRadiusRatio;
		const int32 ControlPointStartIndex = ControlPoints.Num();

		ControlPoints.Add({ FVector3f(StartLocalPos), StartRadius });
		ControlPoints.Add({ FVector3f(EndLocalPos), EndRadius });

		FIVSmokeHoleTrajectoryGPU Trajectory;
		Trajectory.ControlPointStartIndex = ControlPointStartIndex;
		Trajectory.NumControlPoints = 2;
		Trajectory.StartRadius = StartRadius;
		Trajectory.EndRadius = EndRadius;
		Trajectory.CreationTime = static_cast<float>(Hole.CreationTime);
		Trajectory.ShapeType = static_cast<int32>(EIVSmokeHoleShape::Sphere);
		Trajectory.EdgeSoftness = 0.3f;
		Trajectory.DensityMultiplier = 1.0f;
		Trajectories.Add(Trajectory);
	}

	if (Trajectories.Num() == 0)
	{
		Trajectories.AddDefaulted(1);
		ControlPoints.AddDefaulted(1);
	}

	const int32 NumTrajectories = ActiveHoles.Num();
	const int32 TrajectoryBufferCount = Trajectories.Num();
	const int32 ControlPointBufferCount = ControlPoints.Num();
	const FVector3f VolumeMin = FVector3f(-VolumeExtent);
	const FVector3f VolumeMax = FVector3f(VolumeExtent);
	const FIntVector Resolution = VoxelResolution;
	const int32 SpheresPerTrajectory = NumSpheresPerTrajectory;
	const float Time = static_cast<float>(CurrentTime);
	const float Lifetime = static_cast<float>(HoleLifeTime);
	FTextureRHIRef Texture = HoleTexture;

	ENQUEUE_RENDER_COMMAND(IVSmokeHoleCarveCS)(
		[Texture, Trajectories = MoveTemp(Trajectories), ControlPoints = MoveTemp(ControlPoints),
		 NumTrajectories, TrajectoryBufferCount, ControlPointBufferCount,
		 VolumeMin, VolumeMax, Resolution, SpheresPerTrajectory, Time, Lifetime]
		(FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);

			FRDGTextureRef RDGTexture = GraphBuilder.RegisterExternalTexture(
				CreateRenderTarget(Texture, TEXT("IVSmokeHoleTexture"))
			);

			FRDGBufferRef TrajectoryBuffer = CreateStructuredBuffer(
				GraphBuilder,
				TEXT("IVSmokeTrajectoryBuffer"),
				sizeof(FIVSmokeHoleTrajectoryGPU),
				TrajectoryBufferCount,
				Trajectories.GetData(),
				sizeof(FIVSmokeHoleTrajectoryGPU) * TrajectoryBufferCount
			);

			FRDGBufferRef ControlPointBuffer = CreateStructuredBuffer(
				GraphBuilder,
				TEXT("IVSmokeControlPointBuffer"),
				sizeof(FIVSmokeControlPointGPU),
				ControlPointBufferCount,
				ControlPoints.GetData(),
				sizeof(FIVSmokeControlPointGPU) * ControlPointBufferCount
			);

			FIVSmokeHoleCarveCS::FParameters* Parameters = GraphBuilder.AllocParameters<FIVSmokeHoleCarveCS::FParameters>();
			Parameters->VolumeTexture = GraphBuilder.CreateUAV(RDGTexture);
			Parameters->TrajectoryBuffer = GraphBuilder.CreateSRV(TrajectoryBuffer);
			Parameters->ControlPointBuffer = GraphBuilder.CreateSRV(ControlPointBuffer);
			Parameters->VolumeMin = VolumeMin;
			Parameters->VolumeMax = VolumeMax;
			Parameters->Resolution = Resolution;
			Parameters->NumTrajectories = NumTrajectories;
			Parameters->NumSpheresPerTrajectory = SpheresPerTrajectory;
			Parameters->CurrentTime = Time;
			Parameters->HoleLifetime = Lifetime;

			TShaderMapRef<FIVSmokeHoleCarveCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

			const uint32 ThreadGroupSize = FIVSmokeHoleCarveCS::ThreadGroupSize;
			FIntVector GroupCount(
				FMath::DivideAndRoundUp(Resolution.X, static_cast<int32>(ThreadGroupSize)),
				FMath::DivideAndRoundUp(Resolution.Y, static_cast<int32>(ThreadGroupSize)),
				FMath::DivideAndRoundUp(Resolution.Z, static_cast<int32>(ThreadGroupSize))
			);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("IVSmokeHoleCarveCS"),
				ComputeShader,
				Parameters,
				GroupCount
			);

			GraphBuilder.Execute();
		}
	);
}
