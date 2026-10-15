#include "ACryptoTerminalActor.h"
#include "CryptoFinanceSubsystem.h"
#include "Engine/Engine.h"

ACryptoTerminalActor::ACryptoTerminalActor()
{
    PrimaryActorTick.bCanEverTick = true;
}

void ACryptoTerminalActor::BeginPlay()
{
    Super::BeginPlay();
    UE_LOG(LogTemp, Log, TEXT("[Vantage] CryptoTerminalActor inicializado en la escena. Esperando datos del WebSocket..."));
}

void ACryptoTerminalActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    TimeSinceLastPrint += DeltaTime;
    if (TimeSinceLastPrint < PrintInterval)
    {
        return;
    }
    TimeSinceLastPrint = 0.0f;

    if (UGameInstance* GameInstance = GetGameInstance())
    {
        UCryptoFinanceSubsystem* FinanceSubsystem = GameInstance->GetSubsystem<UCryptoFinanceSubsystem>();
        if (FinanceSubsystem)
        {
            FCryptoAssetData AssetData;
            if (FinanceSubsystem->GetAssetData(TargetSymbol, AssetData))
            {
                FString DebugMessage = FString::Printf(
                    TEXT("=== [VANTAGE CRYPTO DATA] ===\n"
                        "Símbolo: %s\n"
                        "Precio Actual (LastPrice): %.2f\n"
                        "Cambio (PriceChange): %.2f\n"
                        "Variación (PriceChangePercent): %.2f%%\n"
                        "Máximo 24h (HighPrice): %.2f\n"
                        "Mínimo 24h (LowPrice): %.2f\n"
                        "Volumen (Volume): %.2f"),
                    *AssetData.Symbol,
                    AssetData.LastPrice,
                    AssetData.PriceChange,
                    AssetData.PriceChangePercent,
                    AssetData.HighPrice,
                    AssetData.LowPrice,
                    AssetData.Volume
                );

                UE_LOG(LogTemp, Log, TEXT("%s"), *DebugMessage);

                if (GEngine)
                {
                    GEngine->AddOnScreenDebugMessage(-1, PrintInterval, FColor::Cyan, DebugMessage);
                }
            }
            else
            {
                if (GEngine)
                {
                    GEngine->AddOnScreenDebugMessage(-1, 1.0f, FColor::Yellow, FString::Printf(TEXT("[Vantage] Esperando datos de %s en caché..."), *TargetSymbol));
                }
            }
        }
    }
}