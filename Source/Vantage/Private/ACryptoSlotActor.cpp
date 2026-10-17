#include "ACryptoSlotActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

ACryptoSlotActor::ACryptoSlotActor()
{
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));

	CardMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CardMeshComponent"));
	CardMeshComponent->SetupAttachment(RootComponent);

	IconPlaneComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("IconPlaneComponent"));
	IconPlaneComponent->SetupAttachment(CardMeshComponent);

	UIWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("UIWidgetComponent"));
	UIWidgetComponent->SetupAttachment(CardMeshComponent);
	UIWidgetComponent->SetWidgetSpace(EWidgetSpace::World);
	UIWidgetComponent->SetDrawAtDesiredSize(true);
}

void ACryptoSlotActor::BeginPlay()
{
	Super::BeginPlay();

	if (IconPlaneComponent && IconPlaneComponent->GetMaterial(IconMaterialIndex))
	{
		DynamicIconMaterial = IconPlaneComponent->CreateAndSetMaterialInstanceDynamic(IconMaterialIndex);
	}
}

void ACryptoSlotActor::InitSlot(const FCryptoAssetData& AssetData, UTexture2D* IconTexture)
{
	if (DynamicIconMaterial && IconTexture)
	{
		DynamicIconMaterial->SetTextureParameterValue(TextureParameterName, IconTexture);
	}

	if (UIWidgetComponent)
	{
		UUserWidget* UserWidget = UIWidgetComponent->GetUserWidgetObject();
		if (UserWidget)
		{
			// Lógica de actualización de la UI con AssetData
		}
	}
}