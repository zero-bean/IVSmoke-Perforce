// Copyright Epic Games, Inc. All Rights Reserved.

#include "IVSmokeShadowCaptureComponent.h"
#include "IVSmokeSettings.h"
#include "Engine/TextureRenderTarget2D.h"

DEFINE_LOG_CATEGORY_STATIC(LogIVSmokeShadowCapture, Log, All);

UIVSmokeShadowCaptureComponent::UIVSmokeShadowCaptureComponent()
{
	// Orthographic projection for directional light shadow
	ProjectionType = ECameraProjectionMode::Orthographic;

	// Capture scene depth for shadow mapping
	// SCS_SceneDepth outputs linear depth in centimeters
	CaptureSource = ESceneCaptureSource::SCS_SceneDepth;

	// Enable every frame capture - engine handles timing automatically
	bCaptureEveryFrame = true;
	// IMPORTANT: Must be false when bCaptureEveryFrame is true to avoid redundant capture requests
	bCaptureOnMovement = false;

	// Manually control clip planes for consistency (auto-calculate can cause flickering)
	bAutoCalculateOrthoPlanes = false;

	// Configure capture settings in constructor (BeginPlay may not be called for dynamically created components)
	ConfigureCaptureSettings();
}

void UIVSmokeShadowCaptureComponent::BeginPlay()
{
	Super::BeginPlay();

	ConfigureCaptureSettings();
}

void UIVSmokeShadowCaptureComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Clean up render target
	if (ShadowDepthRT)
	{
		ShadowDepthRT->RemoveFromRoot();
		ShadowDepthRT = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

void UIVSmokeShadowCaptureComponent::InitializeRenderTarget(int32 Resolution)
{
	// Skip if resolution unchanged and RT exists
	if (ShadowDepthRT && CurrentResolution == Resolution)
	{
		return;
	}

	// Clean up old render target
	if (ShadowDepthRT)
	{
		ShadowDepthRT->RemoveFromRoot();
		ShadowDepthRT = nullptr;
	}

	// Create new render target
	ShadowDepthRT = NewObject<UTextureRenderTarget2D>(this);
	ShadowDepthRT->AddToRoot(); // Prevent GC

	// R32 Float format for depth storage (high precision)
	ShadowDepthRT->RenderTargetFormat = ETextureRenderTargetFormat::RTF_R32f;
	ShadowDepthRT->InitAutoFormat(Resolution, Resolution);

	// Clamp address mode (no wrap for shadow maps)
	ShadowDepthRT->AddressX = TextureAddress::TA_Clamp;
	ShadowDepthRT->AddressY = TextureAddress::TA_Clamp;

	// Set clear color
	ShadowDepthRT->ClearColor = FLinearColor::Black;

	// Create GPU resource immediately
	ShadowDepthRT->UpdateResourceImmediate(true);

	// Assign to scene capture
	TextureTarget = ShadowDepthRT;

	CurrentResolution = Resolution;

	UE_LOG(LogIVSmokeShadowCapture, Log, TEXT("[UIVSmokeShadowCaptureComponent::InitializeRenderTarget] Created shadow RT: %dx%d"), Resolution, Resolution);
}

void UIVSmokeShadowCaptureComponent::UpdateCapture(const FVector& LightDirection, const FBox& CombinedAABB)
{
	if (!ShadowDepthRT || !CombinedAABB.IsValid)
	{
		return;
	}

	// Normalize light direction
	FVector NormalizedLightDir = LightDirection.GetSafeNormal();
	if (NormalizedLightDir.IsNearlyZero())
	{
		NormalizedLightDir = FVector(0.0f, 0.0f, -1.0f); // Default: straight down
	}

	// IMPORTANT: The ViewProjection matrix must match the CAPTURED texture, not the CURRENT frame.
	// Since bCaptureEveryFrame captures at the END of the frame, the texture we're reading
	// was captured with the PREVIOUS frame's camera settings.
	//
	// Flow:
	// - Frame N: UpdateCapture sets camera to position A, calculates matrix for CACHED values
	// - Frame N: Capture happens at position A
	// - Frame N+1: Shader uses matrix from CACHED (matches texture from Frame N)
	// Double-buffer pattern:
	// - "Active" values match the CURRENT texture (returned by getters, used in shader)
	// - "Pending" values are for NEXT frame's capture
	//
	// Flow:
	// Frame N: Active = (from Frame N-2), Pending = (from Frame N-1), capture happens with Pending
	// Frame N+1: Active = old Pending, Pending = new values

	// STEP 1: Promote Pending -> Active (now Active matches the just-captured texture)
	ActiveCameraForward = PendingCameraForward;
	ActiveCameraPosition = PendingCameraPosition;
	ActiveOrthoWidth = PendingOrthoWidth;
	ActiveNearClipPlane = PendingNearClipPlane;
	ActiveFarClipPlane = PendingFarClipPlane;

	// STEP 2: Calculate ViewProjection matrix using Active values (matches current texture)
	// On first few frames, Active values might not be valid yet - use current values instead
	bool bActiveValid = !ActiveCameraPosition.IsZero() && ActiveOrthoWidth > 0.0f;
	if (bActiveValid)
	{
		OrthoWidth = ActiveOrthoWidth;
		CalculateLightViewProjection(ActiveCameraForward, PendingAABB.IsValid ? PendingAABB : CombinedAABB);
	}
	// else: Skip matrix calculation on first frame, will be valid next frame

	// STEP 3: Calculate new Pending values for NEXT frame's capture
	// Add padding to AABB to reduce sensitivity to small changes
	const float AABBPadding = 50.0f; // 50cm padding
	FBox PaddedAABB = CombinedAABB.ExpandBy(AABBPadding);

	FVector AABBCenter = PaddedAABB.GetCenter();
	FVector AABBExtent = PaddedAABB.GetExtent();
	float AABBDiagonal = AABBExtent.Size() * 2.0f;
	float CaptureDistance = AABBDiagonal;

	// Snap values to grid to eliminate floating-point jitter
	const float SnapGrid = 10.0f; // 10cm grid
	AABBCenter.X = FMath::RoundToFloat(AABBCenter.X / SnapGrid) * SnapGrid;
	AABBCenter.Y = FMath::RoundToFloat(AABBCenter.Y / SnapGrid) * SnapGrid;
	AABBCenter.Z = FMath::RoundToFloat(AABBCenter.Z / SnapGrid) * SnapGrid;
	CaptureDistance = FMath::RoundToFloat(CaptureDistance / SnapGrid) * SnapGrid;

	FVector NewCaptureLocation = AABBCenter + NormalizedLightDir * CaptureDistance;
	FRotator NewCaptureRotation = (-NormalizedLightDir).Rotation();

	FVector LightRight = FVector::CrossProduct(NormalizedLightDir, FVector::UpVector);
	if (LightRight.IsNearlyZero())
	{
		LightRight = FVector::CrossProduct(NormalizedLightDir, FVector::ForwardVector);
	}
	LightRight.Normalize();
	FVector LightUp = FVector::CrossProduct(LightRight, NormalizedLightDir).GetSafeNormal();

	float MinX = FLT_MAX, MaxX = -FLT_MAX;
	float MinY = FLT_MAX, MaxY = -FLT_MAX;
	const FVector Corners[8] = {
		CombinedAABB.Min,
		FVector(CombinedAABB.Max.X, CombinedAABB.Min.Y, CombinedAABB.Min.Z),
		FVector(CombinedAABB.Min.X, CombinedAABB.Max.Y, CombinedAABB.Min.Z),
		FVector(CombinedAABB.Max.X, CombinedAABB.Max.Y, CombinedAABB.Min.Z),
		FVector(CombinedAABB.Min.X, CombinedAABB.Min.Y, CombinedAABB.Max.Z),
		FVector(CombinedAABB.Max.X, CombinedAABB.Min.Y, CombinedAABB.Max.Z),
		FVector(CombinedAABB.Min.X, CombinedAABB.Max.Y, CombinedAABB.Max.Z),
		CombinedAABB.Max
	};
	for (const FVector& Corner : Corners)
	{
		FVector RelativePos = Corner - AABBCenter;
		float X = FVector::DotProduct(RelativePos, LightRight);
		float Y = FVector::DotProduct(RelativePos, LightUp);
		MinX = FMath::Min(MinX, X);
		MaxX = FMath::Max(MaxX, X);
		MinY = FMath::Min(MinY, Y);
		MaxY = FMath::Max(MaxY, Y);
	}
	float NewOrthoWidth = FMath::Max(MaxX - MinX, MaxY - MinY) * 1.1f; // 10% margin
	// Snap OrthoWidth to grid
	NewOrthoWidth = FMath::RoundToFloat(NewOrthoWidth / SnapGrid) * SnapGrid;
	NewOrthoWidth = FMath::Max(NewOrthoWidth, 100.0f); // Minimum 100cm

	float NewNearClip = -CaptureDistance * 0.5f;
	float NewFarClip = CaptureDistance * 2.0f;

	// STEP 4: Update camera transform for NEXT frame's capture
	SetWorldLocationAndRotation(NewCaptureLocation, NewCaptureRotation);
	OrthoWidth = NewOrthoWidth;

	// STEP 5: Store Pending values for next iteration
	PendingCameraForward = -NormalizedLightDir;
	PendingCameraPosition = NewCaptureLocation;
	PendingAABB = PaddedAABB; // Use padded AABB for consistency
	PendingOrthoWidth = NewOrthoWidth;
	PendingNearClipPlane = NewNearClip;
	PendingFarClipPlane = NewFarClip;

	UE_LOG(LogIVSmokeShadowCapture, Verbose, TEXT("[UIVSmokeShadowCaptureComponent::UpdateCapture] Updated: Pos=%s, OrthoWidth=%.1f"),
		*NewCaptureLocation.ToString(), OrthoWidth);
}

FTextureRHIRef UIVSmokeShadowCaptureComponent::GetShadowDepthTexture() const
{
	if (ShadowDepthRT && ShadowDepthRT->GetResource())
	{
		FTextureRenderTargetResource* RTResource = ShadowDepthRT->GameThread_GetRenderTargetResource();
		if (RTResource)
		{
			return RTResource->TextureRHI;
		}
	}

	return nullptr;
}

void UIVSmokeShadowCaptureComponent::CalculateLightViewProjection(const FVector& LightDir, const FBox& InBounds)
{
	// Build view matrix (camera looking in light direction)
	// Use ActiveCameraPosition instead of GetComponentLocation() for consistency with texture
	FVector CameraLocation = ActiveCameraPosition;
	FVector CameraForward = LightDir;
	FVector CameraUp = FVector::UpVector;

	// Handle case where light is pointing straight up/down
	if (FMath::Abs(FVector::DotProduct(CameraForward, CameraUp)) > 0.99f)
	{
		CameraUp = FVector::ForwardVector;
	}

	FVector CameraRight = FVector::CrossProduct(CameraUp, CameraForward).GetSafeNormal();
	CameraUp = FVector::CrossProduct(CameraForward, CameraRight).GetSafeNormal();

	// View matrix (world to camera space)
	FMatrix ViewMatrix = FLookFromMatrix(CameraLocation, CameraForward, CameraUp);

	// Orthographic projection matrix
	// OrthoWidth is the full width, so half-width = OrthoWidth / 2
	float HalfWidth = OrthoWidth * 0.5f;
	float HalfHeight = HalfWidth; // Square projection for simplicity
	float NearZ = ActiveNearClipPlane;
	float FarZ = ActiveFarClipPlane;

	// Build orthographic projection matrix (matches UE convention)
	// Note: UE uses reversed-Z for better depth precision
	FMatrix ProjectionMatrix = FReversedZOrthoMatrix(HalfWidth, HalfHeight, 1.0f / (FarZ - NearZ), 0.0f);

	// Combined view-projection matrix
	LightViewProjectionMatrix = ViewMatrix * ProjectionMatrix;
}

void UIVSmokeShadowCaptureComponent::ConfigureCaptureSettings()
{
	// For SCS_SceneDepth, disable all visual effects - only need geometry depth
	ShowFlags.SetLighting(false);
	ShowFlags.SetPostProcessing(false);
	ShowFlags.SetAtmosphere(false);
	ShowFlags.SetFog(false);
	ShowFlags.SetVolumetricFog(false);
	ShowFlags.SetBloom(false);
	ShowFlags.SetMotionBlur(false);
	ShowFlags.SetToneCurve(false);
	ShowFlags.SetEyeAdaptation(false);
	ShowFlags.SetColorGrading(false);
	ShowFlags.SetCameraImperfections(false);
	ShowFlags.SetDepthOfField(false);
	ShowFlags.SetVignette(false);
	ShowFlags.SetGrain(false);
	ShowFlags.SetScreenSpaceReflections(false);
	ShowFlags.SetAmbientOcclusion(false);
	ShowFlags.SetDynamicShadows(false);
	ShowFlags.SetContactShadows(false);

	// Disable particles and unnecessary elements
	ShowFlags.SetParticles(false);
	ShowFlags.SetTranslucency(false);

	// Disable elements that don't contribute meaningful shadows
	ShowFlags.SetDecals(false);
	ShowFlags.SetInstancedGrass(false);
	ShowFlags.SetBillboardSprites(false);
	ShowFlags.SetPaper2DSprites(false);

	// Skeletal meshes (characters) - configurable due to potential flickering
	const UIVSmokeSettings* Settings = UIVSmokeSettings::Get();
	bool bShowSkeletalMeshes = Settings ? Settings->bCaptureSkeletalMeshes : false;
	ShowFlags.SetSkeletalMeshes(bShowSkeletalMeshes);

	// Use scene primitives render mode (default behavior)
	PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;

	// Ensure high-quality capture
	bAlwaysPersistRenderingState = true;
}
