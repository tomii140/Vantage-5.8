#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "IWebSocket.h"
#include "CryptoFinanceSubsystem.generated.h"

USTRUCT(BlueprintType)
struct FCryptoAssetData
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    FString Symbol;

    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    double LastPrice;

    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    double PriceChange;

    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    double PriceChangePercent;

    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    double HighPrice;

    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    double LowPrice;

    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    double Volume;
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
    void ConnectWebSocket();
    bool HandleTicker(float DeltaTime);

    TSharedPtr<IWebSocket> WebSocket;
    TMap<FString, FCryptoAssetData> AssetCache;
    FCriticalSection Mutex;
    FTSTicker::FDelegateHandle TickerHandle;

    bool bNewDataReceived = false;
};