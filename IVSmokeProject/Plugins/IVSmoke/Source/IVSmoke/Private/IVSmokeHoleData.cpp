// Copyright SDB. All Rights Reserved.

#include "IVSmokeHoleData.h"
#include "IVSmokeHoleGeneratorComponent.h"

void FIVSmokeHoleData::PostReplicatedAdd(const FIVSmokeHoleArray& InArray)
{
	if (InArray.OwnerComponent)
	{
		InArray.OwnerComponent->MarkHoleTextureDirty();
	}
}

void FIVSmokeHoleData::PostReplicatedChange(const FIVSmokeHoleArray& InArray)
{
	if (InArray.OwnerComponent)
	{
		InArray.OwnerComponent->MarkHoleTextureDirty();
	}
}

void FIVSmokeHoleData::PreReplicatedRemove(const FIVSmokeHoleArray& InArray)
{
	if (InArray.OwnerComponent)
	{
		InArray.OwnerComponent->MarkHoleTextureDirty();
	}
}
