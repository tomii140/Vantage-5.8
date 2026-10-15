#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CryptoWorker.h"
#include "CryptoFinanceSubsystem.generated.h"

USTRUCT(BlueprintType)
struct FCryptoAssetData
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    FString Symbol;

    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    float LastPrice;

    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    float PriceChange;

    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    float PriceChangePercent;

    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    float HighPrice;

    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    float LowPrice;

    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    float Volume;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCryptoDataUpdated, const TArray<FCryptoAssetData>&, UpdatedAssets);

UCLASS()
class VANTAGE_API UCryptoFinanceSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    UPROPERTY(BlueprintAssignable, Category = "Vantage|Events")
    FOnCryptoDataUpdated OnCryptoDataUpdated;

    UFUNCTION(BlueprintCallable, Category = "Vantage|Finance")
    bool GetAssetData(const FString& Symbol, FCryptoAssetData& OutData);

private:
    bool Tick(float DeltaTime);

    TSharedPtr<FCryptoWorker> CryptoWorker;
    TMap<FString, FCryptoAssetData> AssetCache;
    FCriticalSection Mutex;
    FTSTicker::FDelegateHandle TickerHandle;
};