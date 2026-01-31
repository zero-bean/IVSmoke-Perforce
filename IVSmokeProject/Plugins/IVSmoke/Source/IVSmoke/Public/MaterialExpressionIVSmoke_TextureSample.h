// Copyright (c) 2026, Team SDB. All rights reserved.
#pragma once
#include "CoreMinimal.h"
#include "Materials/MaterialExpression.h"
#include "MaterialExpressionIVSmoke_TextureSample.generated.h"

UENUM(BlueprintType)
enum class EIVSmokeTextureType : uint8
{
	SmokeColor,
	SmokeLocalPos,
	SmokeWorldPosLinearDepth
};

UCLASS(collapsecategories, hidecategories = Object)
class IVSMOKE_API UMaterialExpressionIVSmoke_TextureSample : public UMaterialExpression
{
	GENERATED_BODY()
public:
	UMaterialExpressionIVSmoke_TextureSample();

	// UE 5.7 API 변경사항 반영
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;

#if WITH_EDITOR
	virtual uint32 GetOutputType(int32 OutputIndex) override;
#endif

public:
	UPROPERTY()
	FExpressionInput UVs;

	UPROPERTY(EditAnywhere, Category = "Source")
	EIVSmokeTextureType TextureType;

	UPROPERTY(EditAnywhere, Category = "MaterialExpression", meta = (ShowAsComponentMask))
	uint32 R : 1;
	UPROPERTY(EditAnywhere, Category = "MaterialExpression", meta = (ShowAsComponentMask))
	uint32 G : 1;
	UPROPERTY(EditAnywhere, Category = "MaterialExpression", meta = (ShowAsComponentMask))
	uint32 B : 1;
	UPROPERTY(EditAnywhere, Category = "MaterialExpression", meta = (ShowAsComponentMask))
	uint32 A : 1;
};
