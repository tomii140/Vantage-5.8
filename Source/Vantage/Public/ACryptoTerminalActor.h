#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CryptoFinanceSubsystem.h"
#include "ACryptoTerminalActor.generated.h"

class ACryptoSlotActor;
class UTexture2D;

UENUM(BlueprintType)
enum class ETerminalMode : uint8
{
	Mode3D_Horizontal UMETA(DisplayName = "3D Horizontal"),
	Mode3D_Vertical   UMETA(DisplayName = "3D Vertical"),
	Mode3D_Diagonal   UMETA(DisplayName = "3D Diagonal"),
	Mode3D_Circular   UMETA(DisplayName = "3D Circular")
};

UCLASS()
class VANTAGE_API ACryptoTerminalActor : public AActor
{
	GENERATED_BODY()

public:
	ACryptoTerminalActor();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vantage|Config")
	float PrintInterval = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vantage|Layout")
	ETerminalMode CurrentLayoutMode = ETerminalMode::Mode3D_Horizontal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vantage|Setup")
	TSubclassOf<ACryptoSlotActor> BaseSlotClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vantage|Setup")
	TMap<FString, TObjectPtr<UTexture2D>> CoinTextureMap;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCryptoAssetSelected, const FString&, AssetSymbol);

	UPROPERTY(BlueprintAssignable, Category = "Vantage|Events")
	FOnCryptoAssetSelected OnCryptoAssetSelected;

	UFUNCTION(BlueprintCallable, Category = "Vantage|Terminal")
	static void SetGlobalScrollOffset(int32 NewOffset);

	UFUNCTION(BlueprintCallable, Category = "Vantage|Terminal")
	static int32 GetGlobalScrollOffset();

	UFUNCTION(BlueprintCallable, Category = "Vantage|Layout")
	FVector CalculateSlotPosition(int32 Index, int32 TotalElements) const;

	UFUNCTION(BlueprintCallable, Category = "Vantage|Input")
	void HandleSlotTap(const FString& AssetSymbol);

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;

private:
	float TimeSinceLastPrint = 0.0f;
	static int32 GlobalScrollOffset;
	const int32 DisplayLimit = 5;
};