// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "EnginePrivate.h"
#include "Engine/Note.h"

ANote::ANote(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	USceneComponent* SceneComponent = ObjectInitializer.CreateDefaultSubobject<USceneComponent>(this, TEXT("SceneComp"));
	RootComponent = SceneComponent;
	RootComponent->Mobility = EComponentMobility::Static;

#if WITH_EDITORONLY_DATA
	ArrowComponent = ObjectInitializer.CreateEditorOnlyDefaultSubobject<UArrowComponent>(this, TEXT("Arrow"));
	SpriteComponent = ObjectInitializer.CreateEditorOnlyDefaultSubobject<UBillboardComponent>(this, TEXT("Sprite"));

	if (!IsRunningCommandlet())
	{
		// Structure to hold one-time initialization
		struct FConstructorStatics
		{
			ConstructorHelpers::FObjectFinderOptional<UTexture2D> NoteTextureObject;
			FName ID_Notes;
			FText NAME_Notes;
			FConstructorStatics()
				: NoteTextureObject(TEXT("/Engine/EditorResources/S_Note"))
				, ID_Notes(TEXT("Notes"))
				, NAME_Notes(NSLOCTEXT("SpriteCategory", "Notes", "Notes"))
			{
			}
		};
		static FConstructorStatics ConstructorStatics;

		if (ArrowComponent)
		{
			ArrowComponent->ArrowColor = FColor(150, 200, 255);

			ArrowComponent->ArrowSize = 0.5f;
			ArrowComponent->bTreatAsASprite = true;
			ArrowComponent->SpriteInfo.Category = ConstructorStatics.ID_Notes;
			ArrowComponent->SpriteInfo.DisplayName = ConstructorStatics.NAME_Notes;
			ArrowComponent->Mobility = EComponentMobility::Static;
			ArrowComponent->bIsScreenSizeScaled = true;
		}

		if (SpriteComponent)
		{
			SpriteComponent->Sprite = ConstructorStatics.NoteTextureObject.Get();
			SpriteComponent->RelativeScale3D = FVector(0.5f, 0.5f, 0.5f);
			SpriteComponent->SpriteInfo.Category = ConstructorStatics.ID_Notes;
			SpriteComponent->SpriteInfo.DisplayName = ConstructorStatics.NAME_Notes;
			SpriteComponent->AttachParent = RootComponent;
			SpriteComponent->Mobility = EComponentMobility::Static;
		}
	}
#endif // WITH_EDITORONLY_DATA

	bHidden = true;
	bCanBeDamaged = false;
}

#if WITH_EDITORONLY_DATA
/** Returns SpriteComponent subobject **/
UBillboardComponent* ANote::GetSpriteComponent() const { return SpriteComponent; }
/** Returns ArrowComponent subobject **/
UArrowComponent* ANote::GetArrowComponent() const { return ArrowComponent; }
#endif
