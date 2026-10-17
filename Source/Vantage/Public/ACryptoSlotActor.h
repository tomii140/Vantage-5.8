#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CryptoFinanceSubsystem.h"
#include "ACryptoSlotActor.generated.h"

class UStaticMeshComponent;
class UWidgetComponent;
class UMaterialInstanceDynamic;

UCLASS(Blueprintable)
class VANTAGE_API ACryptoSlotActor : public AActor
{
	GENERATED_BODY()

public:
	ACryptoSlotActor();

	UFUNCTION(BlueprintCallable, Category = "Vantage|Slot")
	virtual void InitSlot(const FCryptoAssetData& AssetData, UTexture2D* IconTexture);

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> CardMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> IconPlaneComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UWidgetComponent> UIWidgetComponent;

	UPROPERTY(EditDefaultsOnly, Category = "Vantage|Materials")
	FName TextureParameterName = FName("IconTexture");

	UPROPERTY(EditDefaultsOnly, Category = "Vantage|Materials")
	int32 IconMaterialIndex = 0;

private:
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> DynamicIconMaterial;
};