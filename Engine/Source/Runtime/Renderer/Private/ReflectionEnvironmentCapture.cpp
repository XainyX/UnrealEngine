// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	Functionality for capturing the scene into reflection capture cubemaps, and prefiltering
=============================================================================*/

#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneFilterRendering.h"
#include "PostProcessing.h"
#include "UniformBuffer.h"
#include "ShaderParameters.h"
#include "ScreenRendering.h"
#include "ReflectionEnvironment.h"
#include "ReflectionEnvironmentCapture.h"
#include "SceneUtils.h"

/** Near plane to use when capturing the scene. */
float GReflectionCaptureNearPlane = 5;

int32 GSupersampleCaptureFactor = 1;

/** 
 * Mip map used by a Roughness of 0, counting down from the lowest resolution mip (MipCount - 1).  
 * This has been tweaked along with ReflectionCaptureRoughnessMipScale to make good use of the resolution in each mip, especially the highest resolution mips.
 * This value is duplicated in ReflectionEnvironmentShared.usf!
 */
float ReflectionCaptureRoughestMip = 1;

/** 
 * Scales the log2 of Roughness when computing which mip to use for a given roughness.
 * Larger values make the higher resolution mips sharper.
 * This has been tweaked along with ReflectionCaptureRoughnessMipScale to make good use of the resolution in each mip, especially the highest resolution mips.
 * This value is duplicated in ReflectionEnvironmentShared.usf!
 */
float ReflectionCaptureRoughnessMipScale = 1.2f;

int32 GDiffuseIrradianceCubemapSize = 32;

void OnUpdateReflectionCaptures( UWorld* InWorld )
{
	InWorld->UpdateAllReflectionCaptures();
}

FAutoConsoleCommandWithWorld CaptureConsoleCommand(
	TEXT("r.ReflectionCapture"),
	TEXT("Updates all reflection captures"),
	FConsoleCommandWithWorldDelegate::CreateStatic(OnUpdateReflectionCaptures)
	);


/** Encapsulates render target picking logic for cubemap mip generation. */
FSceneRenderTargetItem& GetEffectiveRenderTarget(bool bDownsamplePass, int32 TargetMipIndex)
{
	int32 ScratchTextureIndex = TargetMipIndex % 2;

	if (!bDownsamplePass)
	{
		ScratchTextureIndex = 1 - ScratchTextureIndex;
	}

	return GSceneRenderTargets.ReflectionColorScratchCubemap[ScratchTextureIndex]->GetRenderTargetItem();
}

/** Encapsulates source texture picking logic for cubemap mip generation. */
FSceneRenderTargetItem& GetEffectiveSourceTexture(bool bDownsamplePass, int32 TargetMipIndex)
{
	int32 ScratchTextureIndex = TargetMipIndex % 2;

	if (bDownsamplePass)
	{
		ScratchTextureIndex = 1 - ScratchTextureIndex;
	}

	return GSceneRenderTargets.ReflectionColorScratchCubemap[ScratchTextureIndex]->GetRenderTargetItem();
}

class FDownsamplePS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FDownsamplePS,Global);
public:

	static bool ShouldCache(EShaderPlatform Platform)
	{
		return true;
	}

	FDownsamplePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FGlobalShader(Initializer)
	{
		CubeFace.Bind(Initializer.ParameterMap,TEXT("CubeFace"));
		SourceMipIndex.Bind(Initializer.ParameterMap,TEXT("SourceMipIndex"));
		SourceTexture.Bind(Initializer.ParameterMap,TEXT("SourceTexture"));
		SourceTextureSampler.Bind(Initializer.ParameterMap,TEXT("SourceTextureSampler"));
	}
	FDownsamplePS() {}

	void SetParameters(FRHICommandList& RHICmdList, int32 CubeFaceValue, int32 SourceMipIndexValue, FSceneRenderTargetItem& SourceTextureValue)
	{
		SetShaderValue(RHICmdList, GetPixelShader(), CubeFace, CubeFaceValue);
		SetShaderValue(RHICmdList, GetPixelShader(), SourceMipIndex, SourceMipIndexValue);

		SetTextureParameter(
			RHICmdList, 
			GetPixelShader(), 
			SourceTexture, 
			SourceTextureSampler, 
			TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), 
			SourceTextureValue.ShaderResourceTexture);
	}

	virtual bool Serialize(FArchive& Ar)
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << CubeFace;
		Ar << SourceMipIndex;
		Ar << SourceTexture;
		Ar << SourceTextureSampler;
		return bShaderHasOutdatedParameters;
	}

private:
	FShaderParameter CubeFace;
	FShaderParameter SourceMipIndex;
	FShaderResourceParameter SourceTexture;
	FShaderResourceParameter SourceTextureSampler;
};

IMPLEMENT_SHADER_TYPE(,FDownsamplePS,TEXT("ReflectionEnvironmentShaders"),TEXT("DownsamplePS"),SF_Pixel);


/** Pixel shader used for filtering a mip. */
class FCubeFilterPS : public FDownsamplePS
{
	DECLARE_SHADER_TYPE(FCubeFilterPS,Global);
public:

	static bool ShouldCache(EShaderPlatform Platform)
	{
		return true;
	}

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment)
	{
		FDownsamplePS::ModifyCompilationEnvironment(Platform, OutEnvironment);
	}

	FCubeFilterPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FDownsamplePS(Initializer)
	{
		AverageBrightnessTexture.Bind(Initializer.ParameterMap,TEXT("AverageBrightnessTexture"));
		AverageBrightnessSampler.Bind(Initializer.ParameterMap,TEXT("AverageBrightnessSampler"));
	}
	FCubeFilterPS() {}

	void SetParameters(FRHICommandList& RHICmdList, int32 CubeFaceValue, int32 SourceMipIndexValue, FSceneRenderTargetItem& SourceTextureValue)
	{
		FDownsamplePS::SetParameters(RHICmdList, CubeFaceValue, SourceMipIndexValue, SourceTextureValue);

		SetTextureParameter(
			RHICmdList,
			GetPixelShader(), 
			AverageBrightnessTexture, 
			AverageBrightnessSampler, 
			TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), 
			GSceneRenderTargets.ReflectionBrightness->GetRenderTargetItem().ShaderResourceTexture);
	}

	virtual bool Serialize(FArchive& Ar)
	{
		bool bShaderHasOutdatedParameters = FDownsamplePS::Serialize(Ar);
		Ar << AverageBrightnessTexture;
		Ar << AverageBrightnessSampler;
		return bShaderHasOutdatedParameters;
	}

private:
	FShaderResourceParameter AverageBrightnessTexture;
	FShaderResourceParameter AverageBrightnessSampler;
};

template< uint32 bNormalize >
class TCubeFilterPS : public FCubeFilterPS
{
	DECLARE_SHADER_TYPE(TCubeFilterPS,Global);

public:
	static void ModifyCompilationEnvironment(EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment)
	{
		FCubeFilterPS::ModifyCompilationEnvironment(Platform, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("NORMALIZE"), bNormalize);
	}

	TCubeFilterPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	: FCubeFilterPS(Initializer)
	{}

	TCubeFilterPS() {}
};

IMPLEMENT_SHADER_TYPE(template<>,TCubeFilterPS<0>,TEXT("ReflectionEnvironmentShaders"),TEXT("FilterPS"),SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>,TCubeFilterPS<1>,TEXT("ReflectionEnvironmentShaders"),TEXT("FilterPS"),SF_Pixel);

static FGlobalBoundShaderState DownsampleBoundShaderState;

/** Computes the average brightness of a 1x1 mip of a cubemap. */
class FComputeBrightnessPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FComputeBrightnessPS,Global)
public:

	static bool ShouldCache(EShaderPlatform Platform)
	{
		return true;
	}

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Platform, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("COMPUTEBRIGHTNESS_PIXELSHADER"), 1);
		OutEnvironment.SetRenderTargetOutputFormat(0, PF_R32_FLOAT);
	}

	FComputeBrightnessPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		ReflectionEnvironmentColorTexture.Bind(Initializer.ParameterMap,TEXT("ReflectionEnvironmentColorTexture"));
		ReflectionEnvironmentColorSampler.Bind(Initializer.ParameterMap,TEXT("ReflectionEnvironmentColorSampler"));
		NumCaptureArrayMips.Bind(Initializer.ParameterMap, TEXT("NumCaptureArrayMips"));
	}

	FComputeBrightnessPS()
	{
	}

	void SetParameters(FRHICommandList& RHICmdList)
	{
		const int32 EffectiveTopMipSize = GReflectionCaptureSize;
		const int32 NumMips = FMath::CeilLogTwo(EffectiveTopMipSize) + 1;
		// Read from the smallest mip that was downsampled to
		FSceneRenderTargetItem& Cubemap = GetEffectiveRenderTarget(true, NumMips - 1);

		if (Cubemap.IsValid())
		{
			SetTextureParameter(
				RHICmdList,
				GetPixelShader(), 
				ReflectionEnvironmentColorTexture, 
				ReflectionEnvironmentColorSampler, 
				TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), 
				Cubemap.ShaderResourceTexture);
		}

		SetShaderValue(RHICmdList, GetPixelShader(), NumCaptureArrayMips, FMath::CeilLogTwo(GReflectionCaptureSize) + 1);
	}

	virtual bool Serialize(FArchive& Ar)
	{		
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << ReflectionEnvironmentColorTexture;
		Ar << ReflectionEnvironmentColorSampler;
		Ar << NumCaptureArrayMips;
		return bShaderHasOutdatedParameters;
	}

private:

	FShaderResourceParameter ReflectionEnvironmentColorTexture;
	FShaderResourceParameter ReflectionEnvironmentColorSampler;
	FShaderParameter NumCaptureArrayMips;
};

IMPLEMENT_SHADER_TYPE(,FComputeBrightnessPS,TEXT("ReflectionEnvironmentShaders"),TEXT("ComputeBrightnessMain"),SF_Pixel);

/** Computes the average brightness of the given reflection capture and stores it in the scene. */
void ComputeAverageBrightness(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel)
{
	SetRenderTarget(RHICmdList, GSceneRenderTargets.ReflectionBrightness->GetRenderTargetItem().TargetableTexture, NULL);
	RHICmdList.SetRasterizerState(TStaticRasterizerState<FM_Solid, CM_None>::GetRHI());
	RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
	RHICmdList.SetBlendState(TStaticBlendState<>::GetRHI());

	auto ShaderMap = GetGlobalShaderMap(FeatureLevel);
	TShaderMapRef<FPostProcessVS> VertexShader(ShaderMap);
	TShaderMapRef<FComputeBrightnessPS> PixelShader(ShaderMap);

	static FGlobalBoundShaderState BoundShaderState;
	
	SetGlobalBoundShaderState(RHICmdList, FeatureLevel, BoundShaderState, GFilterVertexDeclaration.VertexDeclarationRHI, *VertexShader, *PixelShader);

	PixelShader->SetParameters(RHICmdList);

	DrawRectangle( 
		RHICmdList,
		0, 0, 
		1, 1,
		0, 0, 
		1, 1,
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		*VertexShader);
}

/** Generates mips for glossiness and filters the cubemap for a given reflection. */
void FilterReflectionEnvironment(FRHICommandListImmediate& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, FSHVectorRGB3* OutIrradianceEnvironmentMap, bool bNormalize)
{
	const int32 EffectiveTopMipSize = GReflectionCaptureSize;
	const int32 NumMips = FMath::CeilLogTwo(EffectiveTopMipSize) + 1;

	int32 DiffuseConvolutionSourceMip = INDEX_NONE;
	FSceneRenderTargetItem* DiffuseConvolutionSource = NULL;

	static const auto CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DiffuseFromCaptures"));
	bNormalize = bNormalize && CVar->GetInt() == 0;

	auto ShaderMap = GetGlobalShaderMap(FeatureLevel);

	{	
		SCOPED_DRAW_EVENT(RHICmdList, DownsampleCubeMips, DEC_SCENE_ITEMS);
		// Downsample all the mips, each one reads from the mip above it
		for (int32 MipIndex = 1; MipIndex < NumMips; MipIndex++)
		{
			SCOPED_DRAW_EVENT(RHICmdList, DownsampleCubeMip, DEC_SCENE_ITEMS);
			const int32 SourceMipIndex = FMath::Max(MipIndex - 1, 0);
			const int32 MipSize = 1 << (NumMips - MipIndex - 1);

			FSceneRenderTargetItem& EffectiveRT = GetEffectiveRenderTarget(true, MipIndex);
			FSceneRenderTargetItem& EffectiveSource = GetEffectiveSourceTexture(true, MipIndex);
			check(EffectiveRT.TargetableTexture != EffectiveSource.ShaderResourceTexture);
			
			for (int32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
			{
				SetRenderTarget(RHICmdList, EffectiveRT.TargetableTexture, MipIndex, CubeFace, NULL);

				const FIntRect ViewRect(0, 0, MipSize, MipSize);
				RHICmdList.SetViewport(0, 0, 0.0f, MipSize, MipSize, 1.0f);
				RHICmdList.SetRasterizerState(TStaticRasterizerState<FM_Solid, CM_None>::GetRHI());
				RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
				RHICmdList.SetBlendState(TStaticBlendState<>::GetRHI());

				TShaderMapRef<FScreenVS> VertexShader(GetGlobalShaderMap(FeatureLevel));
				TShaderMapRef<FDownsamplePS> PixelShader(GetGlobalShaderMap(FeatureLevel));

				SetGlobalBoundShaderState(RHICmdList, FeatureLevel, DownsampleBoundShaderState, GFilterVertexDeclaration.VertexDeclarationRHI, *VertexShader, *PixelShader);

				PixelShader->SetParameters(RHICmdList, CubeFace, SourceMipIndex, EffectiveSource);


				RHICmdList.CopyToResolveTarget(EffectiveRT.TargetableTexture, EffectiveRT.ShaderResourceTexture, true, FResolveParams(FResolveRect(), (ECubeFace)CubeFace, MipIndex));
			}

			if (MipSize == GDiffuseIrradianceCubemapSize)
			{
				DiffuseConvolutionSourceMip = MipIndex;
				DiffuseConvolutionSource = &EffectiveRT;
			}
		}
	}

	if (OutIrradianceEnvironmentMap)
	{
		SCOPED_DRAW_EVENT(RHICmdList, ComputeDiffuseIrradiance, DEC_SCENE_ITEMS);
		check(DiffuseConvolutionSource != NULL);
		ComputeDiffuseIrradiance(RHICmdList, FeatureLevel, DiffuseConvolutionSource->ShaderResourceTexture, DiffuseConvolutionSourceMip, OutIrradianceEnvironmentMap);
	}
	
	ComputeAverageBrightness(RHICmdList, FeatureLevel);

	{	
		SCOPED_DRAW_EVENT(RHICmdList, FilterCubeMap, DEC_SCENE_ITEMS);
		// Filter all the mips, each one reads from whichever scratch render target holds the downsampled contents, and writes to the destination cubemap
		for (int32 MipIndex = 0; MipIndex < NumMips; MipIndex++)
		{
			SCOPED_DRAW_EVENT(RHICmdList, FilterCubeMip, DEC_SCENE_ITEMS);
			FSceneRenderTargetItem& EffectiveRT = GetEffectiveRenderTarget(false, MipIndex);
			FSceneRenderTargetItem& EffectiveSource = GetEffectiveSourceTexture(false, MipIndex);
			check(EffectiveRT.TargetableTexture != EffectiveSource.ShaderResourceTexture);
			const int32 MipSize = 1 << (NumMips - MipIndex - 1);

			for (int32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
			{
				SetRenderTarget(RHICmdList, EffectiveRT.TargetableTexture, MipIndex, CubeFace, NULL);

				const FIntRect ViewRect(0, 0, MipSize, MipSize);
				RHICmdList.SetViewport(0, 0, 0.0f, MipSize, MipSize, 1.0f);
				RHICmdList.SetRasterizerState(TStaticRasterizerState<FM_Solid, CM_None>::GetRHI());
				RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
				RHICmdList.SetBlendState(TStaticBlendState<>::GetRHI());

				TShaderMapRef<FScreenVS> VertexShader(GetGlobalShaderMap(FeatureLevel));
				TShaderMapRef< TCubeFilterPS<1> > CaptureCubemapArrayPixelShader(GetGlobalShaderMap(FeatureLevel));

				FCubeFilterPS* PixelShader;
				if( bNormalize )
				{
					PixelShader = *TShaderMapRef< TCubeFilterPS<1> >(ShaderMap);
					static FGlobalBoundShaderState BoundShaderState;
					
					SetGlobalBoundShaderState(RHICmdList, FeatureLevel, BoundShaderState, GFilterVertexDeclaration.VertexDeclarationRHI, *VertexShader, PixelShader);
				}
				else
				{
					PixelShader = *TShaderMapRef< TCubeFilterPS<0> >(ShaderMap);
					static FGlobalBoundShaderState BoundShaderState;
					SetGlobalBoundShaderState(RHICmdList, FeatureLevel, BoundShaderState, GFilterVertexDeclaration.VertexDeclarationRHI, *VertexShader, PixelShader);
						
					SetGlobalBoundShaderState(RHICmdList, FeatureLevel, BoundShaderState, GFilterVertexDeclaration.VertexDeclarationRHI, *VertexShader, PixelShader);
				}

				PixelShader->SetParameters(RHICmdList, CubeFace, MipIndex, EffectiveSource);

				DrawRectangle( 
					RHICmdList,
					ViewRect.Min.X, ViewRect.Min.Y, 
					ViewRect.Width(), ViewRect.Height(),
					ViewRect.Min.X, ViewRect.Min.Y, 
					ViewRect.Width(), ViewRect.Height(),
					FIntPoint(ViewRect.Width(), ViewRect.Height()),
					FIntPoint(MipSize, MipSize),
					*VertexShader);

				RHICmdList.CopyToResolveTarget(EffectiveRT.TargetableTexture, EffectiveRT.ShaderResourceTexture, true, FResolveParams(FResolveRect(), (ECubeFace)CubeFace, MipIndex));
			}
		}
	}
}

/** Vertex shader used when writing to a cubemap. */
class FCopyToCubeFaceVS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FCopyToCubeFaceVS,Global);
public:

	static bool ShouldCache(EShaderPlatform Platform)
	{
		return true;
	}

	FCopyToCubeFaceVS()	{}
	FCopyToCubeFaceVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FGlobalShader(Initializer)
	{
	}

	void SetParameters(FRHICommandList& RHICmdList, const FViewInfo& View)
	{
		FGlobalShader::SetParameters(RHICmdList, GetVertexShader(),View);
	}

	virtual bool Serialize(FArchive& Ar)
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		return bShaderHasOutdatedParameters;
	}
};

IMPLEMENT_SHADER_TYPE(,FCopyToCubeFaceVS,TEXT("ReflectionEnvironmentShaders"),TEXT("CopyToCubeFaceVS"),SF_Vertex);

/** Pixel shader used when copying scene color from a scene render into a face of a reflection capture cubemap. */
class FCopySceneColorToCubeFacePS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FCopySceneColorToCubeFacePS,Global);
public:

	static bool ShouldCache(EShaderPlatform Platform)
	{
		return true;
	}

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Platform,OutEnvironment);
	}

	FCopySceneColorToCubeFacePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FGlobalShader(Initializer)
	{
		DeferredParameters.Bind(Initializer.ParameterMap);
		InTexture.Bind(Initializer.ParameterMap,TEXT("InTexture"));
		InTextureSampler.Bind(Initializer.ParameterMap,TEXT("InTextureSampler"));
		SkyLightCaptureParameters.Bind(Initializer.ParameterMap,TEXT("SkyLightCaptureParameters"));
	}
	FCopySceneColorToCubeFacePS() {}

	void SetParameters(FRHICommandList& RHICmdList, const FViewInfo& View, bool bCapturingForSkyLight, bool bLowerHemisphereIsBlack)
	{
		const FPixelShaderRHIParamRef ShaderRHI = GetPixelShader();

		FGlobalShader::SetParameters(RHICmdList, ShaderRHI, View);
		DeferredParameters.Set(RHICmdList, ShaderRHI, View);

		SetTextureParameter(
			RHICmdList, 
			ShaderRHI, 
			InTexture, 
			InTextureSampler, 
			TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), 
			GSceneRenderTargets.GetSceneColor()->GetRenderTargetItem().ShaderResourceTexture);		

		FVector SkyLightParametersValue = FVector::ZeroVector;
		FScene* Scene = (FScene*)View.Family->Scene;

		if (bCapturingForSkyLight)
		{
			// When capturing reflection captures, support forcing all low hemisphere lighting to be black
			SkyLightParametersValue = FVector(0, 0, bLowerHemisphereIsBlack ? 1.0f : 0.0f);
		}
		else if (Scene->SkyLight && !Scene->SkyLight->bHasStaticLighting)
		{
			// When capturing reflection captures and there's a stationary sky light, mask out any pixels whose depth classify it as part of the sky
			// This will allow changing the stationary sky light at runtime
			SkyLightParametersValue = FVector(1, Scene->SkyLight->SkyDistanceThreshold, 0);
		}
		else
		{
			// When capturing reflection captures and there's no sky light, or only a static sky light, capture all depth ranges
			SkyLightParametersValue = FVector(2, 0, 0);
		}

		SetShaderValue(RHICmdList, ShaderRHI, SkyLightCaptureParameters, SkyLightParametersValue);
	}

	virtual bool Serialize(FArchive& Ar)
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << DeferredParameters;
		Ar << InTexture;
		Ar << InTextureSampler;
		Ar << SkyLightCaptureParameters;
		return bShaderHasOutdatedParameters;
	}

private:
	FDeferredPixelShaderParameters DeferredParameters;	
	FShaderResourceParameter InTexture;
	FShaderResourceParameter InTextureSampler;
	FShaderParameter SkyLightCaptureParameters;
};

IMPLEMENT_SHADER_TYPE(,FCopySceneColorToCubeFacePS,TEXT("ReflectionEnvironmentShaders"),TEXT("CopySceneColorToCubeFaceColorPS"),SF_Pixel);

FGlobalBoundShaderState CopyColorCubemapBoundShaderState;

/** Pixel shader used when copying a cubemap into a face of a reflection capture cubemap. */
class FCopyCubemapToCubeFacePS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FCopyCubemapToCubeFacePS,Global);
public:

	static bool ShouldCache(EShaderPlatform Platform)
	{
		return true;
	}

	FCopyCubemapToCubeFacePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FGlobalShader(Initializer)
	{
		CubeFace.Bind(Initializer.ParameterMap,TEXT("CubeFace"));
		SourceTexture.Bind(Initializer.ParameterMap,TEXT("SourceTexture"));
		SourceTextureSampler.Bind(Initializer.ParameterMap,TEXT("SourceTextureSampler"));
		SkyLightCaptureParameters.Bind(Initializer.ParameterMap,TEXT("SkyLightCaptureParameters"));
	}
	FCopyCubemapToCubeFacePS() {}

	void SetParameters(FRHICommandList& RHICmdList, const FTexture* SourceCubemap, uint32 CubeFaceValue, bool bIsSkyLight, bool bLowerHemisphereIsBlack)
	{
		const FPixelShaderRHIParamRef ShaderRHI = GetPixelShader();

		SetShaderValue(RHICmdList, ShaderRHI, CubeFace, CubeFaceValue);

		SetTextureParameter(
			RHICmdList, 
			ShaderRHI, 
			SourceTexture, 
			SourceTextureSampler, 
			SourceCubemap);

		SetShaderValue(RHICmdList, ShaderRHI, SkyLightCaptureParameters, FVector(bIsSkyLight ? 1.0f : 0.0f, 0.0f, bLowerHemisphereIsBlack ? 1.0f : 0.0f));
	}

	virtual bool Serialize(FArchive& Ar)
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << CubeFace;
		Ar << SourceTexture;
		Ar << SourceTextureSampler;
		Ar << SkyLightCaptureParameters;
		return bShaderHasOutdatedParameters;
	}

private:
	FShaderParameter CubeFace;
	FShaderResourceParameter SourceTexture;
	FShaderResourceParameter SourceTextureSampler;
	FShaderParameter SkyLightCaptureParameters;
};

IMPLEMENT_SHADER_TYPE(,FCopyCubemapToCubeFacePS,TEXT("ReflectionEnvironmentShaders"),TEXT("CopyCubemapToCubeFaceColorPS"),SF_Pixel);

FGlobalBoundShaderState CopyFromCubemapToCubemapBoundShaderState;

int32 FindOrAllocateCubemapIndex(FScene* Scene, const UReflectionCaptureComponent* Component)
{
	int32 CaptureIndex = -1;

	// Try to find an existing capture index for this component
	FCaptureComponentSceneState* CaptureSceneStatePtr = Scene->ReflectionSceneData.AllocatedReflectionCaptureState.Find(Component);

	if (CaptureSceneStatePtr)
	{
		CaptureIndex = CaptureSceneStatePtr->CaptureIndex;
	}
	else
	{
		// Reuse a freed index if possible
		for (int32 PotentialIndex = 0; PotentialIndex < Scene->ReflectionSceneData.AllocatedReflectionCaptureState.Num(); PotentialIndex++)
		{
			if (!Scene->ReflectionSceneData.AllocatedReflectionCaptureState.FindKey(FCaptureComponentSceneState(PotentialIndex)))
			{
				CaptureIndex = PotentialIndex;
				break;
			}
		}

		// Allocate a new index if needed
		if (CaptureIndex == -1)
		{
			CaptureIndex = Scene->ReflectionSceneData.AllocatedReflectionCaptureState.Num();
		}

		Scene->ReflectionSceneData.AllocatedReflectionCaptureState.Add(Component, FCaptureComponentSceneState(CaptureIndex));

		check(CaptureIndex < GMaxNumReflectionCaptures);
	}

	check(CaptureIndex >= 0);
	return CaptureIndex;
}

void ClearScratchCubemaps(FRHICommandList& RHICmdList)
{
	// Clear scratch render targets to a consistent but noticeable value
	// This makes debugging capture issues much easier, otherwise the random contents from previous captures is shown

	const int32 NumMips = FMath::CeilLogTwo(GReflectionCaptureSize) + 1;
	FSceneRenderTargetItem& RT0 = GSceneRenderTargets.ReflectionColorScratchCubemap[0]->GetRenderTargetItem();

	for (int32 MipIndex = 0; MipIndex < NumMips; MipIndex++)
	{
		for (int32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
		{
			SetRenderTarget(RHICmdList, RT0.TargetableTexture, MipIndex, CubeFace, NULL);
			RHICmdList.Clear(true, FLinearColor(0, 10000, 0, 0), false, 0, false, 0, FIntRect());
		}
	}

	FSceneRenderTargetItem& RT1 = GSceneRenderTargets.ReflectionColorScratchCubemap[1]->GetRenderTargetItem();

	for (int32 MipIndex = 0; MipIndex < NumMips; MipIndex++)
	{
		for (int32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
		{
			SetRenderTarget(RHICmdList, RT1.TargetableTexture, MipIndex, CubeFace, NULL);
			RHICmdList.Clear(true, FLinearColor(0, 10000, 0, 0), false, 0, false, 0, FIntRect());
		}
	}
}

/** Captures the scene for a reflection capture by rendering the scene multiple times and copying into a cubemap texture. */
void CaptureSceneToScratchCubemap(FRHICommandListImmediate& RHICmdList, FSceneRenderer* SceneRenderer, ECubeFace CubeFace, bool bCapturingForSkyLight, bool bLowerHemisphereIsBlack)
{
	FMemMark MemStackMark(FMemStack::Get());
	// update any resources that needed a deferred update
	FDeferredUpdateResource::UpdateResources(RHICmdList);

	const auto FeatureLevel = SceneRenderer->FeatureLevel;
	
	{
		SCOPED_DRAW_EVENT(RHICmdList, CubeMapCapture, DEC_SCENE_ITEMS);

		// Render the scene normally for one face of the cubemap
		SceneRenderer->Render(RHICmdList);

#if PLATFORM_PS4 // @todo ps4 - this should be done a different way
		// PS4 needs some code here to process the scene
		extern void TEMP_PostReflectionCaptureRender();
		check(&RHICmdList == &FRHICommandListExecutor::GetImmediateCommandList());
		check(IsInRenderingThread());
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_CaptureSceneToScratchCubemap_Flush);
			FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::FlushRHIThread);
		}
		TEMP_PostReflectionCaptureRender();
#endif

		auto ShaderMap = GetGlobalShaderMap(FeatureLevel);

		const int32 EffectiveSize = GReflectionCaptureSize;
		FSceneRenderTargetItem& EffectiveColorRT =  GSceneRenderTargets.ReflectionColorScratchCubemap[0]->GetRenderTargetItem();

		{
			SCOPED_DRAW_EVENT(RHICmdList, CubeMapCopyScene, DEC_SCENE_ITEMS);
			
			// Copy the captured scene into the cubemap face
			SetRenderTarget(RHICmdList, EffectiveColorRT.TargetableTexture, 0, CubeFace, NULL);

			const FIntRect ViewRect(0, 0, EffectiveSize, EffectiveSize);
			RHICmdList.SetViewport(0, 0, 0.0f, EffectiveSize, EffectiveSize, 1.0f);
			RHICmdList.SetRasterizerState(TStaticRasterizerState<FM_Solid, CM_None>::GetRHI());
			RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
			RHICmdList.SetBlendState(TStaticBlendState<>::GetRHI());

			TShaderMapRef<FCopyToCubeFaceVS> VertexShader(GetGlobalShaderMap(FeatureLevel));
			TShaderMapRef<FCopySceneColorToCubeFacePS> PixelShader(GetGlobalShaderMap(FeatureLevel));
			SetGlobalBoundShaderState(RHICmdList, FeatureLevel, CopyColorCubemapBoundShaderState, GFilterVertexDeclaration.VertexDeclarationRHI, *VertexShader, *PixelShader);

			SetGlobalBoundShaderState(RHICmdList, FeatureLevel, CopyColorCubemapBoundShaderState, GFilterVertexDeclaration.VertexDeclarationRHI, *VertexShader, *PixelShader);

			PixelShader->SetParameters(RHICmdList, SceneRenderer->Views[0], bCapturingForSkyLight, bLowerHemisphereIsBlack);
			VertexShader->SetParameters(RHICmdList, SceneRenderer->Views[0]);

				DrawRectangle( 
					RHICmdList,
					ViewRect.Min.X, ViewRect.Min.Y, 
					ViewRect.Width(), ViewRect.Height(),
					ViewRect.Min.X, ViewRect.Min.Y, 
					ViewRect.Width() * GSupersampleCaptureFactor, ViewRect.Height() * GSupersampleCaptureFactor,
					FIntPoint(ViewRect.Width(), ViewRect.Height()),
					GSceneRenderTargets.GetBufferSizeXY(),
				*VertexShader);

			RHICmdList.CopyToResolveTarget(EffectiveColorRT.TargetableTexture, EffectiveColorRT.ShaderResourceTexture, true, FResolveParams(FResolveRect(), CubeFace));			
		}
	}

	delete SceneRenderer;
}

void CopyCubemapToScratchCubemap(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, UTextureCube* SourceCubemap, bool bIsSkyLight, bool bLowerHemisphereIsBlack)
{
	check(SourceCubemap);
	
	ClearScratchCubemaps(RHICmdList);

	const int32 EffectiveSize = GReflectionCaptureSize;
	FSceneRenderTargetItem& EffectiveColorRT =  GSceneRenderTargets.ReflectionColorScratchCubemap[0]->GetRenderTargetItem();

	for (uint32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
	{
		// Copy the captured scene into the cubemap face
		SetRenderTarget(RHICmdList, EffectiveColorRT.TargetableTexture, 0, CubeFace, NULL);

		const FTexture* SourceCubemapResource = SourceCubemap->Resource;
		const FIntPoint SourceDimensions(SourceCubemapResource->GetSizeX(), SourceCubemapResource->GetSizeY());
		const FIntRect ViewRect(0, 0, EffectiveSize, EffectiveSize);
		RHICmdList.SetViewport(0, 0, 0.0f, EffectiveSize, EffectiveSize, 1.0f);
		RHICmdList.SetRasterizerState(TStaticRasterizerState<FM_Solid, CM_None>::GetRHI());
		RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
		RHICmdList.SetBlendState(TStaticBlendState<>::GetRHI());

		TShaderMapRef<FScreenVS> VertexShader(GetGlobalShaderMap(FeatureLevel));
		TShaderMapRef<FCopyCubemapToCubeFacePS> PixelShader(GetGlobalShaderMap(FeatureLevel));

		SetGlobalBoundShaderState(RHICmdList, FeatureLevel, CopyFromCubemapToCubemapBoundShaderState, GFilterVertexDeclaration.VertexDeclarationRHI, *VertexShader, *PixelShader);
		PixelShader->SetParameters(RHICmdList, SourceCubemapResource, CubeFace, bIsSkyLight, bLowerHemisphereIsBlack);

		DrawRectangle(
			RHICmdList,
			ViewRect.Min.X, ViewRect.Min.Y, 
			ViewRect.Width(), ViewRect.Height(),
			0, 0, 
			SourceDimensions.X, SourceDimensions.Y,
			FIntPoint(ViewRect.Width(), ViewRect.Height()),
			SourceDimensions,
			*VertexShader);

		RHICmdList.CopyToResolveTarget(EffectiveColorRT.TargetableTexture, EffectiveColorRT.ShaderResourceTexture, true, FResolveParams(FResolveRect(), (ECubeFace)CubeFace));
	}
}

/** 
 * Allocates reflection captures in the scene's reflection cubemap array and updates them by recapturing the scene.
 * Existing captures will only be updated.  Must be called from the game thread.
 */
void FScene::AllocateReflectionCaptures(const TArray<UReflectionCaptureComponent*>& NewCaptures)
{
	if (NewCaptures.Num() > 0)
	{
		if (GetFeatureLevel() >= ERHIFeatureLevel::SM5)
		{
			for (int32 CaptureIndex = 0; CaptureIndex < NewCaptures.Num(); CaptureIndex++)
			{
				bool bAlreadyExists = false;

				// Try to find an existing allocation
				for (TSparseArray<UReflectionCaptureComponent*>::TIterator It(ReflectionSceneData.AllocatedReflectionCapturesGameThread); It; ++It)
				{
					UReflectionCaptureComponent* OtherComponent = *It;

					if (OtherComponent == NewCaptures[CaptureIndex])
					{
						bAlreadyExists = true;
					}
				}

				// Add the capture to the allocated list
				if (!bAlreadyExists && ReflectionSceneData.AllocatedReflectionCapturesGameThread.Num() < GMaxNumReflectionCaptures)
				{
					ReflectionSceneData.AllocatedReflectionCapturesGameThread.Add(NewCaptures[CaptureIndex]);
				}
			}

			// Request the exact amount needed by default
			int32 DesiredMaxCubemaps = ReflectionSceneData.AllocatedReflectionCapturesGameThread.Num();
			const float MaxCubemapsRoundUpBase = 1.5f;

			// If this is not the first time the scene has allocated the cubemap array, include slack to reduce reallocations
			if (ReflectionSceneData.MaxAllocatedReflectionCubemapsGameThread > 0)
			{
				float Exponent = FMath::LogX(MaxCubemapsRoundUpBase, ReflectionSceneData.AllocatedReflectionCapturesGameThread.Num());

				// Round up to the next integer exponent to provide stability and reduce reallocations
				DesiredMaxCubemaps = FMath::Pow(MaxCubemapsRoundUpBase, FMath::TruncToInt(Exponent) + 1);
			}

			DesiredMaxCubemaps = FMath::Min(DesiredMaxCubemaps, GMaxNumReflectionCaptures);

			if (DesiredMaxCubemaps != ReflectionSceneData.MaxAllocatedReflectionCubemapsGameThread)
			{
				ReflectionSceneData.MaxAllocatedReflectionCubemapsGameThread = DesiredMaxCubemaps;

				ENQUEUE_UNIQUE_RENDER_COMMAND_TWOPARAMETER( 
					ResizeArrayCommand,
					FScene*, Scene, this,
					uint32, MaxSize, ReflectionSceneData.MaxAllocatedReflectionCubemapsGameThread,
				{
					// Update the scene's cubemap array, which will reallocate it, so we no longer have the contents of existing entries
					Scene->ReflectionSceneData.CubemapArray.UpdateMaxCubemaps(MaxSize);
				});

				// Recapture all reflection captures now that we have reallocated the cubemap array
				UpdateAllReflectionCaptures();
			}
			else
			{
				// No reallocation of the cubemap array was needed, just update the captures that were requested

				for (TSparseArray<UReflectionCaptureComponent*>::TIterator It(ReflectionSceneData.AllocatedReflectionCapturesGameThread); It; ++It)
				{
					UReflectionCaptureComponent* CurrentComponent = *It;

					if (NewCaptures.Contains(CurrentComponent))
					{
						UpdateReflectionCaptureContents(CurrentComponent);
					}
				}
			}
		}
		else if (GetFeatureLevel() == ERHIFeatureLevel::SM4)
		{
			for (int32 ComponentIndex = 0; ComponentIndex < NewCaptures.Num(); ComponentIndex++)
			{
				UReflectionCaptureComponent* CurrentComponent = NewCaptures[ComponentIndex];
				UpdateReflectionCaptureContents(CurrentComponent);
			}
		}

		for (int32 CaptureIndex = 0; CaptureIndex < NewCaptures.Num(); CaptureIndex++)
		{
			UReflectionCaptureComponent* Component = NewCaptures[CaptureIndex];

			Component->SetCaptureCompleted();

			if (Component->SceneProxy)
			{
				// Update the transform of the reflection capture
				// This is not done earlier by the reflection capture when it detects that it is dirty,
				// To ensure that the RT sees both the new transform and the new contents on the same frame.
				Component->SendRenderTransform_Concurrent();
			}
		}
	}
}

/** Updates the contents of all reflection captures in the scene.  Must be called from the game thread. */
void FScene::UpdateAllReflectionCaptures()
{
	if (IsReflectionEnvironmentAvailable(GetFeatureLevel()))
	{
		ENQUEUE_UNIQUE_RENDER_COMMAND_ONEPARAMETER( 
			CaptureCommand,
			FScene*, Scene, this,
		{
			Scene->ReflectionSceneData.AllocatedReflectionCaptureState.Empty();
		});

		const int32 UpdateDivisor = FMath::Max(ReflectionSceneData.AllocatedReflectionCapturesGameThread.Num() / 20, 1);
		const bool bDisplayStatus = ReflectionSceneData.AllocatedReflectionCapturesGameThread.Num() > 50;

		if (bDisplayStatus)
		{
			const FText Status = NSLOCTEXT("Engine", "BeginReflectionCapturesTask", "Updating Reflection Captures...");
			GWarn->BeginSlowTask( Status, true );
			GWarn->StatusUpdate(0, ReflectionSceneData.AllocatedReflectionCapturesGameThread.Num(), Status);
		}

		int32 CaptureIndex = 0;

		for (TSparseArray<UReflectionCaptureComponent*>::TIterator It(ReflectionSceneData.AllocatedReflectionCapturesGameThread); It; ++It)
		{
			// Update progress occasionally
			if (bDisplayStatus && CaptureIndex % UpdateDivisor == 0)
			{
				GWarn->UpdateProgress(CaptureIndex, ReflectionSceneData.AllocatedReflectionCapturesGameThread.Num());
			}

			CaptureIndex++;
			UReflectionCaptureComponent* CurrentComponent = *It;
			UpdateReflectionCaptureContents(CurrentComponent);
		}

		if (bDisplayStatus)
		{
			GWarn->EndSlowTask();
		}
	}
}

void GetReflectionCaptureData_RenderingThread(FRHICommandListImmediate& RHICmdList, FScene* Scene, const UReflectionCaptureComponent* Component, FReflectionCaptureFullHDRDerivedData* OutDerivedData)
{
	const FCaptureComponentSceneState* ComponentStatePtr = Scene->ReflectionSceneData.AllocatedReflectionCaptureState.Find(Component);

	if (ComponentStatePtr)
	{
		const int32 CaptureIndex = ComponentStatePtr->CaptureIndex;
		const int32 EffectiveTopMipSize = GReflectionCaptureSize;
		const int32 NumMips = FMath::CeilLogTwo(EffectiveTopMipSize) + 1;

		TArray<uint8> CaptureData;
		int32 CaptureDataSize = 0;

		for (int32 MipIndex = 0; MipIndex < NumMips; MipIndex++)
		{
			const int32 MipSize = 1 << (NumMips - MipIndex - 1);

			for (int32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
			{
				CaptureDataSize += MipSize * MipSize * sizeof(FFloat16Color);
			}
		}

		CaptureData.Empty(CaptureDataSize);
		CaptureData.AddZeroed(CaptureDataSize);
		int32 MipBaseIndex = 0;

		for (int32 MipIndex = 0; MipIndex < NumMips; MipIndex++)
		{
			FSceneRenderTargetItem& EffectiveDest = Scene->ReflectionSceneData.CubemapArray.GetRenderTarget();
			check(EffectiveDest.ShaderResourceTexture->GetFormat() == PF_FloatRGBA);
			const int32 MipSize = 1 << (NumMips - MipIndex - 1);
			const int32 CubeFaceBytes = MipSize * MipSize * sizeof(FFloat16Color);

			for (int32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
			{
				TArray<FFloat16Color> SurfaceData;
				// Read each mip face
				//@todo - do this without blocking the GPU so many times
				//@todo - pool the temporary textures in RHIReadSurfaceFloatData instead of always creating new ones
				RHICmdList.ReadSurfaceFloatData(EffectiveDest.ShaderResourceTexture, FIntRect(0, 0, MipSize, MipSize), SurfaceData, (ECubeFace)CubeFace, CaptureIndex, MipIndex);
				const int32 DestIndex = MipBaseIndex + CubeFace * CubeFaceBytes;
				uint8* FaceData = &CaptureData[DestIndex];
				check(SurfaceData.Num() * SurfaceData.GetTypeSize() == CubeFaceBytes);
				FMemory::Memcpy(FaceData, SurfaceData.GetData(), CubeFaceBytes);
			}

			MipBaseIndex += CubeFaceBytes * CubeFace_MAX;
		}

		OutDerivedData->InitializeFromUncompressedData(CaptureData);
	}
}

void FScene::GetReflectionCaptureData(UReflectionCaptureComponent* Component, FReflectionCaptureFullHDRDerivedData& OutDerivedData) 
{
	check(GetFeatureLevel() >= ERHIFeatureLevel::SM5);

	ENQUEUE_UNIQUE_RENDER_COMMAND_THREEPARAMETER(
		GetReflectionDataCommand,
		FScene*,Scene,this,
		const UReflectionCaptureComponent*,Component,Component,
		FReflectionCaptureFullHDRDerivedData*,OutDerivedData,&OutDerivedData,
	{
		GetReflectionCaptureData_RenderingThread(RHICmdList, Scene, Component, OutDerivedData);
	});

	// Necessary since the RT is writing to OutDerivedData directly
	FlushRenderingCommands();
}

void UploadReflectionCapture_RenderingThread(FScene* Scene, const FReflectionCaptureFullHDRDerivedData* DerivedData, const UReflectionCaptureComponent* CaptureComponent)
{
	const int32 EffectiveTopMipSize = GReflectionCaptureSize;
	const int32 NumMips = FMath::CeilLogTwo(EffectiveTopMipSize) + 1;

	const int32 CaptureIndex = FindOrAllocateCubemapIndex(Scene, CaptureComponent);
	FTextureCubeRHIRef& CubeMapArray = (FTextureCubeRHIRef&)Scene->ReflectionSceneData.CubemapArray.GetRenderTarget().ShaderResourceTexture;
	check(CubeMapArray->GetFormat() == PF_FloatRGBA);

	TArray<uint8> CubemapData;
	DerivedData->GetUncompressedData(CubemapData);
	int32 MipBaseIndex = 0;

	for (int32 MipIndex = 0; MipIndex < NumMips; MipIndex++)
	{
		const int32 MipSize = 1 << (NumMips - MipIndex - 1);
		const int32 CubeFaceBytes = MipSize * MipSize * sizeof(FFloat16Color);

		for (int32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
		{
			uint32 DestStride = 0;
			uint8* DestBuffer = (uint8*)RHILockTextureCubeFace(CubeMapArray, CubeFace, CaptureIndex, MipIndex, RLM_WriteOnly, DestStride, false);

			// Handle DestStride by copying each row
			for (int32 Y = 0; Y < MipSize; Y++)
			{
				FFloat16Color* DestPtr = (FFloat16Color*)((uint8*)DestBuffer + Y * DestStride);
				const int32 SourceIndex = MipBaseIndex + CubeFace * CubeFaceBytes + Y * MipSize * sizeof(FFloat16Color);
				const uint8* SourcePtr = &CubemapData[SourceIndex];
				FMemory::Memcpy(DestPtr, SourcePtr, MipSize * sizeof(FFloat16Color));
			}

			RHIUnlockTextureCubeFace(CubeMapArray, CubeFace, CaptureIndex, MipIndex, false);
		}

		MipBaseIndex += CubeFaceBytes * CubeFace_MAX;
	}
}

/** Creates a transformation for a cubemap face, following the D3D cubemap layout. */
FMatrix CalcCubeFaceViewMatrix(ECubeFace Face, FVector WorldLocation)
{
	FMatrix Result(FMatrix::Identity);

	static const FVector XAxis(1.f,0.f,0.f);
	static const FVector YAxis(0.f,1.f,0.f);
	static const FVector ZAxis(0.f,0.f,1.f);

	// vectors we will need for our basis
	FVector vUp(YAxis);
	FVector vDir;

	switch( Face )
	{
	case CubeFace_PosX:
		vDir = XAxis;
		break;
	case CubeFace_NegX:
		vDir = -XAxis;
		break;
	case CubeFace_PosY:
		vUp = -ZAxis;
		vDir = YAxis;
		break;
	case CubeFace_NegY:
		vUp = ZAxis;
		vDir = -YAxis;
		break;
	case CubeFace_PosZ:
		vDir = ZAxis;
		break;
	case CubeFace_NegZ:
		vDir = -ZAxis;
		break;
	}

	// derive right vector
	FVector vRight( vUp ^ vDir );
	// create matrix from the 3 axes
	Result = FBasisVectorMatrix( vRight, vUp, vDir, -WorldLocation );	

	return Result;
}

/** 
 * Render target class required for rendering the scene.
 * This doesn't actually allocate a render target as we read from scene color to get HDR results directly.
 */
class FCaptureRenderTarget : public FRenderResource, public FRenderTarget
{
public:

	FCaptureRenderTarget() :
		Size(GReflectionCaptureSize)
	{}

	virtual const FTexture2DRHIRef& GetRenderTargetTexture() const 
	{
		static FTexture2DRHIRef DummyTexture;
		return DummyTexture;
	}

	virtual FIntPoint GetSizeXY() const { return FIntPoint(Size, Size); }
	virtual float GetDisplayGamma() const { return 1.0f; }

private:

	int32 Size;
};

TGlobalResource<FCaptureRenderTarget> GReflectionCaptureRenderTarget;

void CaptureSceneIntoScratchCubemap(FScene* Scene, FVector CapturePosition, bool bCapturingForSkyLight, bool bStaticSceneOnly, float SkyLightNearPlane, bool bLowerHemisphereIsBlack, bool bCaptureEmissiveOnly)
{
	for (int32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
	{
		// Alert the RHI that we're rendering a new frame
		// Not really a new frame, but it will allow pooling mechanisms to update, like the uniform buffer pool
		ENQUEUE_UNIQUE_RENDER_COMMAND(
			BeginFrame,
		{
			GFrameNumberRenderThread++;
			RHICmdList.BeginFrame();
		})

		FSceneViewFamilyContext ViewFamily( FSceneViewFamily::ConstructionValues(
			&GReflectionCaptureRenderTarget,
			Scene,
			FEngineShowFlags(ESFIM_Game))
			.SetWorldTimes( 0.0f, 0.0f, 0.0f )				
			.SetResolveScene(false) );

		// Disable features that are not desired when capturing the scene
		ViewFamily.EngineShowFlags.PostProcessing = 0;
		ViewFamily.EngineShowFlags.MotionBlur = 0;
		ViewFamily.EngineShowFlags.SetOnScreenDebug(false);
		ViewFamily.EngineShowFlags.HMDDistortion = 0;
		// Exclude particles and light functions as they are usually dynamic, and can't be captured well
		ViewFamily.EngineShowFlags.Particles = 0;
		ViewFamily.EngineShowFlags.LightFunctions = 0;
		ViewFamily.EngineShowFlags.CompositeEditorPrimitives = 0;
		// These are highly dynamic and can't be captured effectively
		ViewFamily.EngineShowFlags.LightShafts = 0;

		// Don't apply sky lighting diffuse when capturing the sky light source, or we would have feedback
		ViewFamily.EngineShowFlags.SkyLighting = !bCapturingForSkyLight;

		FSceneViewInitOptions ViewInitOptions;
		ViewInitOptions.ViewFamily = &ViewFamily;
		ViewInitOptions.BackgroundColor = FLinearColor::Black;
		ViewInitOptions.OverlayColor = FLinearColor::Black;
		ViewInitOptions.SetViewRectangle(FIntRect(0, 0, GReflectionCaptureSize * GSupersampleCaptureFactor, GReflectionCaptureSize * GSupersampleCaptureFactor));

		const float NearPlane = bCapturingForSkyLight ? SkyLightNearPlane : GReflectionCaptureNearPlane;

		// Projection matrix based on the fov, near / far clip settings
		// Each face always uses a 90 degree field of view
		ViewInitOptions.ProjectionMatrix = FReversedZPerspectiveMatrix(
			90.0f * (float)PI / 360.0f,
			(float)GReflectionCaptureSize * GSupersampleCaptureFactor,
			(float)GReflectionCaptureSize * GSupersampleCaptureFactor,
			NearPlane
			);

		ViewInitOptions.ViewMatrix = CalcCubeFaceViewMatrix((ECubeFace)CubeFace, CapturePosition);

		FSceneView* View = new FSceneView(ViewInitOptions);

		// Force all surfaces diffuse
		View->RoughnessOverrideParameter = FVector2D( 1.0f, 0.0f );

		if (bCaptureEmissiveOnly)
		{
			View->DiffuseOverrideParameter = FVector4(0, 0, 0, 0);
			View->SpecularOverrideParameter = FVector4(0, 0, 0, 0);
		}

		View->bIsReflectionCapture = true;
		View->bStaticSceneOnly = bStaticSceneOnly;
		View->StartFinalPostprocessSettings(CapturePosition);
		View->EndFinalPostprocessSettings();

		ViewFamily.Views.Add(View);

		FSceneRenderer* SceneRenderer = FSceneRenderer::CreateSceneRenderer(&ViewFamily, NULL);

		ENQUEUE_UNIQUE_RENDER_COMMAND_FOURPARAMETER( 
			CaptureCommand,
			FSceneRenderer*, SceneRenderer, SceneRenderer,
			ECubeFace, CubeFace, (ECubeFace)CubeFace,
			bool, bCapturingForSkyLight, bCapturingForSkyLight,
			bool, bLowerHemisphereIsBlack, bLowerHemisphereIsBlack,
		{
			
			CaptureSceneToScratchCubemap(RHICmdList, SceneRenderer, CubeFace, bCapturingForSkyLight, bLowerHemisphereIsBlack);

			RHICmdList.EndFrame();
		});
	}
}

void CopyToSceneArray(FRHICommandListImmediate& RHICmdList, FScene* Scene, FReflectionCaptureProxy* ReflectionProxy)
{
	const int32 EffectiveTopMipSize = GReflectionCaptureSize;
	const int32 NumMips = FMath::CeilLogTwo(EffectiveTopMipSize) + 1;

	const int32 CaptureIndex = FindOrAllocateCubemapIndex(Scene, ReflectionProxy->Component);

	// GPU copy back to the scene's texture array, which is not a render target
	for (int32 MipIndex = 0; MipIndex < NumMips; MipIndex++)
	{
		// The source for this copy is the dest from the filtering pass
		FSceneRenderTargetItem& EffectiveSource = GetEffectiveRenderTarget(false, MipIndex);
		FSceneRenderTargetItem& EffectiveDest = Scene->ReflectionSceneData.CubemapArray.GetRenderTarget();

		for (int32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
		{
			RHICmdList.CopyToResolveTarget(EffectiveSource.ShaderResourceTexture, EffectiveDest.ShaderResourceTexture, true, FResolveParams(FResolveRect(), (ECubeFace)CubeFace, MipIndex, 0, CaptureIndex));
		}
	}
}

void CopyToComponentTexture(FRHICommandList& RHICmdList, FScene* Scene, FReflectionCaptureProxy* ReflectionProxy)
{
	check(ReflectionProxy->SM4FullHDRCubemap);
	const int32 EffectiveTopMipSize = GReflectionCaptureSize;
	const int32 NumMips = FMath::CeilLogTwo(EffectiveTopMipSize) + 1;

	// GPU copy back to the component's cubemap texture, which is not a render target
	for (int32 MipIndex = 0; MipIndex < NumMips; MipIndex++)
	{
		// The source for this copy is the dest from the filtering pass
		FSceneRenderTargetItem& EffectiveSource = GetEffectiveRenderTarget(false, MipIndex);

		for (int32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
		{
			RHICmdList.CopyToResolveTarget(EffectiveSource.ShaderResourceTexture, ReflectionProxy->SM4FullHDRCubemap->TextureRHI, true, FResolveParams(FResolveRect(), (ECubeFace)CubeFace, MipIndex, 0, 0));
		}
	}
}

/** 
 * Updates the contents of the given reflection capture by rendering the scene. 
 * This must be called on the game thread.
 */
void FScene::UpdateReflectionCaptureContents(UReflectionCaptureComponent* CaptureComponent)
{
	if (IsReflectionEnvironmentAvailable(GetFeatureLevel()))
	{
		const FReflectionCaptureFullHDRDerivedData* DerivedData = CaptureComponent->GetCachedFullHDRDerivedData();

		// Upload existing derived data if it exists, instead of capturing
		if (DerivedData && DerivedData->CompressedCapturedData.Num() > 0)
		{
			// For other feature levels the reflection textures are stored on the component instead of in a scene-wide texture array
			if (GetFeatureLevel() >= ERHIFeatureLevel::SM5)
			{
				ENQUEUE_UNIQUE_RENDER_COMMAND_THREEPARAMETER( 
					UploadCaptureCommand,
					FScene*, Scene, this,
					const FReflectionCaptureFullHDRDerivedData*, DerivedData, DerivedData,
					const UReflectionCaptureComponent*, CaptureComponent, CaptureComponent,
				{
					UploadReflectionCapture_RenderingThread(Scene, DerivedData, CaptureComponent);
				});
			}
		}
		else
		{
			CaptureSceneIntoScratchCubemap(this, CaptureComponent->GetComponentLocation(), false, true, 0, false, false);

			ENQUEUE_UNIQUE_RENDER_COMMAND_ONEPARAMETER( 
				FilterCommand,
				ERHIFeatureLevel::Type, FeatureLevel, GetFeatureLevel(),
			{
				bool bNormalize = true;
				FilterReflectionEnvironment(RHICmdList, FeatureLevel, NULL, bNormalize);
			});

			// Create a proxy to represent the reflection capture to the rendering thread
			// The rendering thread will be responsible for deleting this when done with the filtering operation
			// We can't use the component's SceneProxy here because the component may not be registered with the scene
			FReflectionCaptureProxy* ReflectionProxy = new FReflectionCaptureProxy(CaptureComponent);

			ENQUEUE_UNIQUE_RENDER_COMMAND_THREEPARAMETER( 
				CopyCommand,
				FScene*, Scene, this,
				FReflectionCaptureProxy*, ReflectionProxy, ReflectionProxy,
				ERHIFeatureLevel::Type, FeatureLevel, GetFeatureLevel(),
			{
				if (FeatureLevel == ERHIFeatureLevel::SM5)
				{
					CopyToSceneArray(RHICmdList, Scene, ReflectionProxy);
				}
				else if (FeatureLevel == ERHIFeatureLevel::SM4)
				{
					CopyToComponentTexture(RHICmdList, Scene, ReflectionProxy);
				}

				// Clean up the proxy now that the rendering thread is done with it
				delete ReflectionProxy;
			});
		}
	}
}

void CopyToSkyTexture(FRHICommandList& RHICmdList, FScene* Scene, FTexture* ProcessedTexture)
{
	const int32 EffectiveTopMipSize = ProcessedTexture->GetSizeX();
	const int32 NumMips = FMath::CeilLogTwo(EffectiveTopMipSize) + 1;

	// GPU copy back to the skylight's texture, which is not a render target
	for (int32 MipIndex = 0; MipIndex < NumMips; MipIndex++)
	{
		// The source for this copy is the dest from the filtering pass
		FSceneRenderTargetItem& EffectiveSource = GetEffectiveRenderTarget(false, MipIndex);

		for (int32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
		{
			RHICmdList.CopyToResolveTarget(EffectiveSource.ShaderResourceTexture, ProcessedTexture->TextureRHI, true, FResolveParams(FResolveRect(), (ECubeFace)CubeFace, MipIndex, 0, 0));
		}
	}
}

void FScene::UpdateSkyCaptureContents(const USkyLightComponent* CaptureComponent, bool bCaptureEmissiveOnly, FTexture* OutProcessedTexture, FSHVectorRGB3& OutIrradianceEnvironmentMap)
{	
	//todo maybe use ShouldRenderSkylight here, but currently the Skylight hasn't been loaded in time.
	if (GSupportsRenderTargetFormat_PF_FloatRGBA)
	{
		ENQUEUE_UNIQUE_RENDER_COMMAND( 
			ClearCommand,
		{
			//@todo realtime skylight updates - skip this
			ClearScratchCubemaps(RHICmdList);
		});

		if (CaptureComponent->SourceType == SLS_CapturedScene)
		{
			bool bStaticSceneOnly = CaptureComponent->Mobility != EComponentMobility::Movable;
			CaptureSceneIntoScratchCubemap(this, CaptureComponent->GetComponentLocation(), true, bStaticSceneOnly, CaptureComponent->SkyDistanceThreshold, CaptureComponent->bLowerHemisphereIsBlack, bCaptureEmissiveOnly);
		}
		else if (CaptureComponent->SourceType == SLS_SpecifiedCubemap)
		{
			ENQUEUE_UNIQUE_RENDER_COMMAND_THREEPARAMETER( 
				CopyCubemapCommand,
				UTextureCube*, SourceTexture, CaptureComponent->Cubemap,
				bool, bLowerHemisphereIsBlack, CaptureComponent->bLowerHemisphereIsBlack,
				ERHIFeatureLevel::Type, FeatureLevel, GetFeatureLevel(),
			{
				
				CopyCubemapToScratchCubemap(RHICmdList, FeatureLevel, SourceTexture, true, bLowerHemisphereIsBlack);
			});
		}
		else
		{
			check(0);
		}

		ENQUEUE_UNIQUE_RENDER_COMMAND_TWOPARAMETER( 
			FilterCommand,
			FSHVectorRGB3*, IrradianceEnvironmentMap, &OutIrradianceEnvironmentMap,
			ERHIFeatureLevel::Type, FeatureLevel, GetFeatureLevel(),
		{
			// Skylight is normalized manually in the shader
			bool bNormalize = false;
			FilterReflectionEnvironment(RHICmdList, FeatureLevel, IrradianceEnvironmentMap, bNormalize);
		});

		// Wait until the SH coefficients have been written out by the RT before returning
		//@todo realtime skylight updates - remove the need for this
		FlushRenderingCommands();

		// Optionally copy the filtered mip chain to the output texture
		if (OutProcessedTexture)
		{
		ENQUEUE_UNIQUE_RENDER_COMMAND_TWOPARAMETER( 
			CopyCommand,
			FScene*, Scene, this,
				FTexture*, ProcessedTexture, OutProcessedTexture,
		{
			CopyToSkyTexture(RHICmdList, Scene, ProcessedTexture);
		});
		}
	}
}
