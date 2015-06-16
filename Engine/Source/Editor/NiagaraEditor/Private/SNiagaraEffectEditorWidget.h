// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Visibility.h"
#include "SlateBasics.h"
#include "Editor/ContentBrowser/Public/ContentBrowserModule.h"
#include "WorkflowOrientedApp/SContentReference.h"
#include "SCheckBox.h"
#include "SNameComboBox.h"
#include "SCurveEditor.h"
#include "AssetThumbnail.h"
#include "SNiagaraEffectEditorViewport.h"
#include "NiagaraComponent.h"
#include "NiagaraEffect.h"
#include "NiagaraSimulation.h"
#include "Engine/NiagaraEffectRenderer.h"
#include "ComponentReregisterContext.h"
#include "SNumericEntryBox.h"
#include "Curves/CurveVector.h"

#define NGED_SECTION_BORDER SNew(SBorder).BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder")).Padding(2.0f).HAlign(HAlign_Left)
#define NGED_SECTION_LIGHTBORDER SNew(SBorder).BorderImage(FEditorStyle::GetBrush("ToolPanel.LightGroupBorder")).Padding(2.0f).HAlign(HAlign_Left)
#define NGED_SECTION_DARKBORDER SNew(SBorder).BorderImage(FEditorStyle::GetBrush("ToolPanel.DarkGroupBorder")).Padding(2.0f).HAlign(HAlign_Left)

const FLinearColor RedCol(0.5, 0.1, 0.1);
const FLinearColor GreenCol(0.1, 0.5, 0.1);
const FLinearColor BlueCol(0.1, 0.1, 0.5);
const FLinearColor GrayCol(0.3, 0.3, 0.3);

class SVectorConstantWidget : public SCompoundWidget, public FNotifyHook
{
private:	
	FName ConstantName;
	TSharedPtr<FNiagaraSimulation> Emitter;
public:
	SLATE_BEGIN_ARGS(SVectorConstantWidget) :
	_Emitter(nullptr),
	_ConstantName("Undefined")
	{
	}
	SLATE_ARGUMENT(TSharedPtr<FNiagaraSimulation>, Emitter)
	SLATE_ARGUMENT(FName, ConstantName)
	SLATE_END_ARGS()

	TOptional<float> GetConstX() const	
	{
		const FVector4 *ConstPtr = Emitter->GetProperties()->ExternalConstants.FindVector(ConstantName);
		if (ConstPtr)
		{
			return ConstPtr->X;
		}
		return 0.0f;
	}

	TOptional<float> GetConstY() const
	{
		const FVector4 *ConstPtr = Emitter->GetProperties()->ExternalConstants.FindVector(ConstantName);
		if (ConstPtr)
		{
			return ConstPtr->Y;
		}
		return 0.0f;
	}

	TOptional<float> GetConstZ() const
	{
		const FVector4 *ConstPtr = Emitter->GetProperties()->ExternalConstants.FindVector(ConstantName);
		if (ConstPtr)
		{
			return ConstPtr->Z;
		}
		return 0.0f;
	}

	TOptional<float> GetConstW() const
	{
		const FVector4 *ConstPtr = Emitter->GetProperties()->ExternalConstants.FindVector(ConstantName);
		if (ConstPtr)
		{
			return ConstPtr->W;
		}
		return 0.0f;
	}

	void OnConstantChangedX(float InVal)	{ OnConstantChangedX(InVal, ETextCommit::Default); }
	void OnConstantChangedY(float InVal)	{ OnConstantChangedY(InVal, ETextCommit::Default); }
	void OnConstantChangedZ(float InVal)	{ OnConstantChangedZ(InVal, ETextCommit::Default); }
	void OnConstantChangedW(float InVal)	{ OnConstantChangedW(InVal, ETextCommit::Default); }

	void OnConstantChangedX(float InVal, ETextCommit::Type Type)
	{
		FVector4 Constant(InVal, GetConstY().GetValue(), GetConstZ().GetValue(), GetConstW().GetValue());
		Emitter->GetProperties()->ExternalConstants.SetOrAdd(ConstantName, Constant);
	}

	void OnConstantChangedY(float InVal, ETextCommit::Type Type)
	{
		FVector4 Constant(GetConstX().GetValue(), InVal, GetConstZ().GetValue(), GetConstW().GetValue());
		Emitter->GetProperties()->ExternalConstants.SetOrAdd(ConstantName, Constant);
	}

	void OnConstantChangedZ(float InVal, ETextCommit::Type Type)
	{
		FVector4 Constant(GetConstX().GetValue(), GetConstY().GetValue(), InVal, GetConstW().GetValue());
		Emitter->GetProperties()->ExternalConstants.SetOrAdd(ConstantName, Constant);
	}

	void OnConstantChangedW(float InVal, ETextCommit::Type Type)
	{
		FVector4 Constant(GetConstX().GetValue(), GetConstY().GetValue(), GetConstZ().GetValue(), InVal);
		Emitter->GetProperties()->ExternalConstants.SetOrAdd(ConstantName, Constant);
	}

	void Construct(const FArguments& InArgs)
	{
		Emitter = InArgs._Emitter;
		ConstantName = InArgs._ConstantName;

		const FVector4 *ConstPtr = Emitter->GetProperties()->ExternalConstants.FindVector(ConstantName);
		FVector4 ConstVal;

		if (ConstPtr)
		{
			ConstVal = *ConstPtr;
		}

		this->ChildSlot
		[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().VAlign(VAlign_Center).Padding(4)[SNew(STextBlock).Text(FText::FromName(ConstantName))]
		+ SHorizontalBox::Slot().VAlign(VAlign_Center).Padding(4)[SNew(SNumericEntryBox<float>).Value(this, &SVectorConstantWidget::GetConstX)
			.OnValueCommitted(this, &SVectorConstantWidget::OnConstantChangedX).OnValueChanged(this, &SVectorConstantWidget::OnConstantChangedX)
			.Label()
			[
				SNumericEntryBox<float>::BuildLabel(FText::FromString("X"), FLinearColor::White, RedCol)
			]
		]

			+ SHorizontalBox::Slot().VAlign(VAlign_Center).Padding(4)[SNew(SNumericEntryBox<float>).Value(this, &SVectorConstantWidget::GetConstY)
			.OnValueCommitted(this, &SVectorConstantWidget::OnConstantChangedY).OnValueChanged(this, &SVectorConstantWidget::OnConstantChangedY)
			.Label()
			[
				SNumericEntryBox<float>::BuildLabel(FText::FromString("Y"), FLinearColor::White, GreenCol)
			]
		]

			+ SHorizontalBox::Slot().VAlign(VAlign_Center).Padding(4)[SNew(SNumericEntryBox<float>).Value(this, &SVectorConstantWidget::GetConstZ)
			.OnValueCommitted(this, &SVectorConstantWidget::OnConstantChangedZ).OnValueChanged(this, &SVectorConstantWidget::OnConstantChangedZ)
			.Label()
			[
				SNumericEntryBox<float>::BuildLabel(FText::FromString("Z"), FLinearColor::White, BlueCol)
			]
		]

			+ SHorizontalBox::Slot().VAlign(VAlign_Center).Padding(4)[SNew(SNumericEntryBox<float>).Value(this, &SVectorConstantWidget::GetConstW)
			.OnValueCommitted(this, &SVectorConstantWidget::OnConstantChangedW).OnValueChanged(this, &SVectorConstantWidget::OnConstantChangedW)
			.Label()
			[
				SNumericEntryBox<float>::BuildLabel(FText::FromString("W"), FLinearColor::White, GrayCol)
			]
		]
		];
		
	}
};




class SCurveConstantWidget : public SCompoundWidget, public FNotifyHook
{
private:
	FName ConstantName;
	TSharedPtr<FNiagaraSimulation> Emitter;

	UCurveVector *CurCurve;

public:
	SLATE_BEGIN_ARGS(SCurveConstantWidget) :
		_Emitter(nullptr),
		_ConstantName("Undefined")
	{
	}
	SLATE_ARGUMENT(TSharedPtr<FNiagaraSimulation>, Emitter)
		SLATE_ARGUMENT(FName, ConstantName)
		SLATE_END_ARGS()

	void OnCurveSelected(UObject *Asset)
	{
		CurCurve = Cast<UCurveVector>(Asset);
		UNiagaraDataObject *DataObj = Emitter->GetProperties()->ExternalConstants.FindDataObj(ConstantName);
		if (!DataObj)
		{
			DataObj = NewObject<UNiagaraCurveDataObject>(Emitter->GetParentEffect());
			Emitter->GetProperties()->ExternalConstants.SetOrAdd(ConstantName, DataObj);
		}

		UNiagaraCurveDataObject *CurveData = Cast<UNiagaraCurveDataObject>(DataObj);
		CurveData->SetCurveObject(CurCurve);
	}

	UObject * GetCurve() const
	{
		UNiagaraDataObject *DataObj = Emitter->GetProperties()->ExternalConstants.FindDataObj(ConstantName);
		UNiagaraCurveDataObject *CurveData = Cast<UNiagaraCurveDataObject>(DataObj);
		if (CurveData)
		{
			return CurveData->GetCurveObject();
		}
		return nullptr;
	}

	void Construct(const FArguments& InArgs)
	{
		Emitter = InArgs._Emitter;
		ConstantName = InArgs._ConstantName;

		this->ChildSlot
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().VAlign(VAlign_Center).AutoWidth().Padding(4)[SNew(STextBlock).Text(FText::FromName(ConstantName))]
			+ SHorizontalBox::Slot().VAlign(VAlign_Center).Padding(4)
			[
				SNew(SContentReference)
				.AllowedClass(UCurveVector::StaticClass())
				.AssetReference(this, &SCurveConstantWidget::GetCurve)
				.AllowSelectingNewAsset(true)
				.AllowClearingReference(true)
				.OnSetReference(this, &SCurveConstantWidget::OnCurveSelected)
				.WidthOverride(130)
			]
		];
	}
};

class SEmitterWidget : public SCompoundWidget, public FNotifyHook
{
private:
	TSharedPtr<FNiagaraSimulation> Emitter;
	FNiagaraEffectInstance *EffectInstance;
	TSharedPtr<STextBlock> UpdateComboText, SpawnComboText;
	TSharedPtr<SComboButton> UpdateCombo, SpawnCombo;
	TSharedPtr<SContentReference> UpdateScriptSelector, SpawnScriptSelector;
	TSharedPtr<SWidget> ThumbnailWidget;
	TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
	TSharedPtr<FAssetThumbnail> MaterialThumbnail;
	UNiagaraScript *CurUpdateScript, *CurSpawnScript;
	UMaterial *CurMaterial;

	TSharedPtr<IDetailsView> RendererPropertiesView;

	TArray<TSharedPtr<FString> > RenderModuleList;

	TSharedPtr< SListView<TSharedPtr<EditorExposedVectorConstant>>> UpdateScriptConstantList;
	SVerticalBox::FSlot *UpdateScriptConstantListSlot, *UpdateScriptCurveConstantListSlot;
	TSharedPtr< SListView<TSharedPtr<EditorExposedVectorConstant>>> SpawnScriptConstantList;
	SVerticalBox::FSlot *SpawnScriptConstantListSlot;

public:
	SLATE_BEGIN_ARGS(SEmitterWidget) :
		_Emitter(nullptr)
	{
	}
	SLATE_ARGUMENT(TSharedPtr<FNiagaraSimulation>, Emitter)
	SLATE_ARGUMENT(FNiagaraEffectInstance*, Effect)
	SLATE_END_ARGS()

	TSharedRef<ITableRow> OnGenerateConstantListRow(TSharedPtr<EditorExposedVectorConstant> InItem, const TSharedRef< STableViewBase >& OwnerTable)
	{
		return SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
		.Content()
		[
			SNew(SVectorConstantWidget).ConstantName(InItem->ConstName).Emitter(Emitter)
		];
	}


	TSharedRef<ITableRow> OnGenerateCurveConstantListRow(TSharedPtr<EditorExposedVectorCurveConstant> InItem, const TSharedRef< STableViewBase >& OwnerTable)
	{
		return SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
			.Content()
			[
				SNew(SCurveConstantWidget).ConstantName(InItem->ConstName).Emitter(Emitter)
			];
	}


	void OnEmitterNameChanged(const FText &InText)
	{
		Emitter->GetProperties()->Name = InText.ToString();
	}

	FText GetEmitterName() const
	{
		return FText::FromString(Emitter->GetProperties()->Name);
	}

	void OnUpdateScriptSelectedFromPicker(UObject *Asset);
	void OnSpawnScriptSelectedFromPicker(UObject *Asset);

	void OnMaterialSelected(UObject *Asset)
	{
		TComponentReregisterContext<UNiagaraComponent> ComponentReregisterContext;
		MaterialThumbnail->SetAsset(Asset);
		MaterialThumbnail->RefreshThumbnail();
		MaterialThumbnail->GetViewportRenderTargetTexture(); // Access the texture once to trigger it to render
		ThumbnailPool->Tick(0);
		CurMaterial = Cast<UMaterial>(Asset);
		Emitter->GetEffectRenderer()->SetMaterial(CurMaterial, ERHIFeatureLevel::SM5);
		Emitter->GetProperties()->Material = CurMaterial;
	}

	UObject *GetMaterial() const
	{
		return Emitter->GetProperties()->Material;
	}

	UObject *GetUpdateScript() const
	{
		return Emitter->GetProperties()->UpdateScript;
	}

	UObject *GetSpawnScript() const
	{
		return Emitter->GetProperties()->SpawnScript;
	}

	void OnEmitterEnabledChanged(ECheckBoxState NewCheckedState)
	{
		const bool bNewEnabledState = (NewCheckedState == ESlateCheckBoxState::Checked);
		Emitter->SetEnabled(bNewEnabledState);
	}

	ECheckBoxState IsEmitterEnabled() const
	{
		return Emitter->IsEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}

	void OnRenderModuleChanged(TSharedPtr<FString> ModName, ESelectInfo::Type SelectInfo);


	void OnSpawnRateChanged(const FText &InText)
	{
		float Rate = FCString::Atof(*InText.ToString());
		Emitter->GetProperties()->SpawnRate = Rate;
	}

	FText GetSpawnRateText() const
	{
		TCHAR Txt[32];
		FCString::Snprintf(Txt, 32, TEXT("%f"), Emitter->GetProperties()->SpawnRate );
		return FText::FromString(Txt);
	}

	void OnNumLoopsChanged(const FText &InText)
	{
		int Loops = FCString::Atoi(*InText.ToString());
		Emitter->GetProperties()->NumLoops = Loops;
	}

	FText GetLoopsText() const
	{
		TCHAR Txt[32];
		FCString::Snprintf(Txt, 32, TEXT("%i"), Emitter->GetProperties()->NumLoops);
		return FText::FromString(Txt);
	}


	FText GetRenderModuleText() const
	{
		if (Emitter->GetProperties()->RenderModuleType == RMT_None)
		{
			return NSLOCTEXT("NiagaraEffectEditor", "None", "None");
		}

		return FText::FromString(*RenderModuleList[Emitter->GetProperties()->RenderModuleType-1]);
	}

	TSharedRef<SWidget>GenerateRenderModuleComboWidget(TSharedPtr<FString> WidgetString )
	{
		return SNew(STextBlock)
			.Text(FText::FromString(*WidgetString));
	}

	FText GetStatsText() const
	{
		TCHAR Txt[128];
		FCString::Snprintf(Txt, 128, TEXT("%i Particles  |  %.2f ms  |  %.1f MB"), Emitter->GetNumParticles(), Emitter->GetTotalCPUTime(), Emitter->GetTotalBytesUsed()/(1024.0*1024.0));
		return FText::FromString(Txt);
	}

	void Construct(const FArguments& InArgs);




};


class SNiagaraTimeline : public SCompoundWidget, public FNotifyHook
{
private:
	TSharedPtr<FNiagaraSimulation> Emitter;
	UNiagaraEffect *Effect;
	FNiagaraEffectInstance *EffectInstance;
	TSharedPtr<SCurveEditor>CurveEditor;

public:
	SLATE_BEGIN_ARGS(SNiagaraTimeline) :
	_Emitter(nullptr)
	{
	}
	SLATE_ARGUMENT(TSharedPtr<FNiagaraSimulation>, Emitter)
		SLATE_ARGUMENT(FNiagaraEffectInstance*, EffectInstance)
		SLATE_ARGUMENT(UNiagaraEffect*, Effect)
		SLATE_END_ARGS()

	virtual void Construct(const FArguments& InArgs);

	void SetCurve(UCurveBase *Curve)
	{
		CurveEditor->SetCurveOwner(Curve);
	}

};





class SNiagaraEffectEditorWidget : public SCompoundWidget, public FNotifyHook
{
private:
	UNiagaraEffect *EffectObj;
	FNiagaraEffectInstance *EffectInstance;

	/** Notification list to pass messages to editor users  */
	TSharedPtr<SNotificationList> NotificationListPtr;
	SOverlay::FOverlaySlot* DetailsViewSlot;
	
	TSharedPtr< SListView<TSharedPtr<FNiagaraSimulation> > > ListView;

	class FNiagaraEffectEditor *EffectEditor;
public:
	SLATE_BEGIN_ARGS(SNiagaraEffectEditorWidget)
		: _EffectObj(nullptr)
	{
	}
	SLATE_ARGUMENT(UNiagaraEffect*, EffectObj)
	SLATE_ARGUMENT(TSharedPtr<SWidget>, TitleBar)
	SLATE_ARGUMENT(FNiagaraEffectInstance*, EffectInstance)
	SLATE_ARGUMENT(class FNiagaraEffectEditor*, EffectEditor)
	SLATE_END_ARGS()


	/** Function to check whether we should show read-only text in the panel */
	EVisibility ReadOnlyVisibility() const
	{
		return EVisibility::Hidden;
	}

	void UpdateList()
	{
		ListView->RequestListRefresh();
	}

	TSharedRef<ITableRow> OnGenerateWidgetForList(TSharedPtr<FNiagaraSimulation> InItem, const TSharedRef< STableViewBase >& OwnerTable)
	{
		return SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
			.Content()
			[
				SNew(SBorder)
				//.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(2.0f)
				[
					SNew(SEmitterWidget).Emitter(InItem).Effect(EffectInstance)
				]
			];
	}

	void OnSelectionChanged(TSharedPtr<FNiagaraSimulation> SelectedItem, ESelectInfo::Type SelType); 

	void Construct(const FArguments& InArgs);

};

