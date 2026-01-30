// Copyright (c) 2026, Team SDB. All rights reserved.

#include "MaterialExpressionIVSmoke_TextureSample.h"
#include "MaterialCompiler.h"

#define LOCTEXT_NAMESPACE "IVSmokeMaterialExpressions"

UMaterialExpressionIVSmoke_TextureSample::UMaterialExpressionIVSmoke_TextureSample()
{
	// 기본값 설정
	TextureType = EIVSmokeTextureType::SmokeColor;

	// 기본적으로 RGBA 모두 활성화
	R = true;
	G = true;
	B = true;
	A = true;

#if WITH_EDITORONLY_DATA
	// 노드 메뉴 카테고리
	MenuCategories.Add(LOCTEXT("IVSmokeCategory", "IVSmoke"));
#endif

	// 출력 핀 설정
	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT("Color")));
}

int32 UMaterialExpressionIVSmoke_TextureSample::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (!Compiler)
	{
		return INDEX_NONE;
	}

	// SceneTexture ID 매핑
	uint32 SceneTextureId = 0;

	switch (TextureType)
	{
	case EIVSmokeTextureType::SmokeColor:
		SceneTextureId = PPI_PostProcessInput0; // PPI_PostProcessInput0
		break;
	case EIVSmokeTextureType::SmokeLocalPos:
		SceneTextureId = PPI_PostProcessInput1; // PPI_PostProcessInput1
		break;
	case EIVSmokeTextureType::SmokeWorldPosLinearDepth:
		SceneTextureId = PPI_PostProcessInput4; // PPI_PostProcessInput4
		break;
	default:
		return Compiler->Errorf(TEXT("Invalid texture type"));
	}

	int32 UVsInput = INDEX_NONE;

	// UV 입력 처리 (SceneTexture 노드와 동일한 방식)
	if (UVs.GetTracedInput().Expression)
	{
		UVsInput = UVs.Compile(Compiler);
	}
	else
	{
		// UV가 연결되지 않은 경우 기본 ViewportUV 사용
		UVsInput = Compiler->GetViewportUV();
	}

	if (UVsInput == INDEX_NONE)
	{
		return Compiler->Errorf(TEXT("Failed to compile UV input"));
	}

	// SceneTexture 샘플링
	int32 SceneTextureLookup = Compiler->SceneTextureLookup(
		UVsInput,
		SceneTextureId,
		false, // bFiltered
		false, // bClamped
		false  // bUnused
	);

	if (SceneTextureLookup == INDEX_NONE)
	{
		return Compiler->Errorf(TEXT("Failed to sample scene texture"));
	}

	// 최소 하나의 채널은 선택되어야 함
	if (!R && !G && !B && !A)
	{
		return Compiler->Errorf(TEXT("At least one channel must be selected"));
	}

	// 모든 채널이 선택된 경우
	if (R && G && B && A)
	{
		return SceneTextureLookup;
	}

	// 일부 채널만 선택된 경우 ComponentMask 적용
	return Compiler->ComponentMask(SceneTextureLookup, R, G, B, A);
}

void UMaterialExpressionIVSmoke_TextureSample::GetCaption(TArray<FString>& OutCaptions) const
{
	FString TypeName;

	switch (TextureType)
	{
	case EIVSmokeTextureType::SmokeColor:
		TypeName = TEXT("SmokeColor");
		break;
	case EIVSmokeTextureType::SmokeLocalPos:
		TypeName = TEXT("SmokeLocalPos");
		break;
	case EIVSmokeTextureType::SmokeWorldPosLinearDepth:
		TypeName = TEXT("SmokeWorldPosLinearDepth");
		break;
	default:
		TypeName = TEXT("Unknown");
		break;
	}

	OutCaptions.Add(FString::Printf(TEXT("IVSmoke Sample [%s]"), *TypeName));
}

#if WITH_EDITOR
uint32 UMaterialExpressionIVSmoke_TextureSample::GetOutputType(int32 OutputIndex)
{
	// 채널 마스크에 따른 출력 타입 결정
	int32 NumChannels = 0;

	if (R) NumChannels++;
	if (G) NumChannels++;
	if (B) NumChannels++;
	if (A) NumChannels++;

	switch (NumChannels)
	{
	case 1:
		return MCT_Float1;
	case 2:
		return MCT_Float2;
	case 3:
		return MCT_Float3;
	case 4:
	default:
		return MCT_Float4;
	}
}

#endif

#undef LOCTEXT_NAMESPACE
