// Copyright (c) 2026, Team SDB. All rights reserved.
#pragma once
#include "CoreMinimal.h"
#include "Materials/MaterialExpression.h"
#include "MaterialExpressionIVSmoke_TextureSample.generated.h"

/**
 * Alpha processing type in composite pass.
 */
UENUM(BlueprintType)
enum class EIVSmokeTextureType : uint8
{
	//PostProcessInput0
	SmokeColor,
	//PostProcessInput1
	SmokeLocalPos,
	//PostProcessInput4
	SmokeWorldPosLinearDepth
};

/**
 *
 */
UCLASS(collapsecategories, hidecategories = Object)
class IVSMOKE_API UMaterialExpressionIVSmoke_TextureSample : public UMaterialExpression
{
	GENERATED_BODY()
public:
	UMaterialExpressionIVSmoke_TextureSample();
	// 머티리얼 그래프 → HLSL 코드 생성
	virtual int32 Compile(FMaterialCompiler* Compiler, int32 OutputIndex) override;
	// 노드 상단 이름 표시
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
#if WITH_EDITOR
	// 각 출력 핀 타입 정의 (float2, float3 등)
	virtual uint32 GetOutputType(int32 OutputIndex) override;
#endif
public:
	// inputs
	UPROPERTY()
	FExpressionInput UVs;
	// options
	UPROPERTY(EditAnywhere, Category = "Source")
	EIVSmokeTextureType TextureType;

	// Channel Mask - 노드에 체크박스로 표시
	UPROPERTY(EditAnywhere, Category = "MaterialExpression", meta = (ShowAsComponentMask))
	uint32 R : 1;

	UPROPERTY(EditAnywhere, Category = "MaterialExpression", meta = (ShowAsComponentMask))
	uint32 G : 1;

	UPROPERTY(EditAnywhere, Category = "MaterialExpression", meta = (ShowAsComponentMask))
	uint32 B : 1;

	UPROPERTY(EditAnywhere, Category = "MaterialExpression", meta = (ShowAsComponentMask))
	uint32 A : 1;
};
