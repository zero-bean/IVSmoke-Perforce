// Copyright SDB. All Rights Reserved.

#include "IVSmokeVolumeTextureBaker.h"

#include "IVSmoke.h"
#include "IVSmokeVoxelVolumeCurator.h"
#include "RenderingThread.h"

void FIVSmokeVolumeTextureBaker::Initialize(UObject* Outer, const FIntVector& Resolution)
{
    if (!Outer || Resolution.X <= 0 || Resolution.Y <= 0 || Resolution.Z <= 0)
    {
        return;
    }

    TextureResolution = Resolution;

    if (HoleDataTexture = NewObject<UVolumeTexture>(Outer, TEXT("HoleDataVolumeTexture")); HoleDataTexture)
    {
        // Default Settings
        HoleDataTexture->SRGB = false;
        HoleDataTexture->Filter = TF_Bilinear;
        HoleDataTexture->AddressMode = TA_Clamp;
        HoleDataTexture->CompressionSettings = TC_HDR;
        HoleDataTexture->NeverStream = true;
        HoleDataTexture->LODGroup = TEXTUREGROUP_Pixels2D;
        HoleDataTexture->MipGenSettings = TMGS_NoMipmaps;
        HoleDataTexture->LODBias = 0;

        // Self Generate PlatformData
        FTexturePlatformData* PlatformData = new FTexturePlatformData();
        PlatformData->SizeX = Resolution.X;
        PlatformData->SizeY = Resolution.Y;
        PlatformData->SetNumSlices(Resolution.Z);
        PlatformData->PixelFormat = PF_A32B32G32R32F;

        // Generate Mip level
        FTexture2DMipMap* Mip = new FTexture2DMipMap();
        Mip->SizeX = Resolution.X;
        Mip->SizeY = Resolution.Y;
        Mip->SizeZ = Resolution.Z;

        // Calculate and Assign Data Size (TSF_RGBA32F Format)
        const int32 TotalVoxels = Resolution.X * Resolution.Y * Resolution.Z;
        const int32 DataSize = TotalVoxels * 4 * sizeof(float);

        Mip->BulkData.Lock(LOCK_READ_WRITE);
        float* MipData = reinterpret_cast<float*>(Mip->BulkData.Realloc(DataSize));

        // Initialize
        for (int32 i = 0; i < TotalVoxels; ++i)
        {
            const int32 BaseIndex = i * 4;
            MipData[BaseIndex + 0] = 1.0f;  // R: Density
            MipData[BaseIndex + 1] = 0.0f;  // G: CreationTime
            MipData[BaseIndex + 2] = 0.0f;  // B: Reserved
            MipData[BaseIndex + 3] = 1.0f;  // A: Reserved
        }

        Mip->BulkData.Unlock();
        PlatformData->Mips.Add(Mip);

        // Set PlatformData and Update Resource
        HoleDataTexture->SetPlatformData(PlatformData);
        HoleDataTexture->UpdateResource();
    }
}

void FIVSmokeVolumeTextureBaker::Bake(FIVSmokeVoxelVolumeCurator& Curator) const
{
    if (!IsInitialized() || !HoleDataTexture)
    {
        Curator.ClearTextureDirty();
        return;
    }

    const TArray<float>& SourceData = Curator.GetTextureData();
    if (SourceData.Num() == 0)
    {
        Curator.ClearTextureDirty();
        return;
    }

    FTextureResource* Resource = HoleDataTexture->GetResource();
    if (!Resource)
    {
        Curator.ClearTextureDirty();
        return;
    }

    // Copy Data
    TArray<uint8> TextureDataCopy;
    TextureDataCopy.SetNumUninitialized(SourceData.Num() * sizeof(float));
    FMemory::Memcpy(TextureDataCopy.GetData(), SourceData.GetData(), TextureDataCopy.Num());

    const FIntVector Resolution = TextureResolution;

    ENQUEUE_RENDER_COMMAND(UpdateHoleDataTexture)(
        [Resource, Resolution, Data = MoveTemp(TextureDataCopy)](FRHICommandListImmediate& RHICmdList)
        {
            FRHITexture* RHITexture = Resource->GetTextureRHI();
            if (!RHITexture)
            {
                return;
            }

            const int32 ActualSizeX = RHITexture->GetSizeX();
            const int32 ActualSizeY = RHITexture->GetSizeY();
            const int32 ActualSizeZ = RHITexture->GetSizeZ();

            if (ActualSizeX != Resolution.X || ActualSizeY != Resolution.Y || ActualSizeZ != Resolution.Z)
            {
                UE_LOG(LogIVSmoke, Fatal, TEXT("Texture size mismatch! Expected=%dx%dx%d, Actual=%dx%dx%d"),
                    Resolution.X, Resolution.Y, Resolution.Z, ActualSizeX, ActualSizeY, ActualSizeZ);
                return;
            }

            const FUpdateTextureRegion3D Region(0, 0, 0, 0, 0, 0,
            	Resolution.X, Resolution.Y, Resolution.Z);
            const uint32 SourceRowPitch = Resolution.X * 4 * sizeof(float);
            const uint32 SourceDepthPitch = Resolution.X * Resolution.Y * 4 * sizeof(float);

            RHIUpdateTexture3D(
                RHITexture,
                0,
                Region,
                SourceRowPitch,
                SourceDepthPitch,
                Data.GetData()
            );
        }
    );

    Curator.ClearTextureDirty();
}
