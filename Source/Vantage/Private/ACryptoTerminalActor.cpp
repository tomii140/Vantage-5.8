#include "ACryptoTerminalActor.h"
#include "CryptoFinanceSubsystem.h"
#include "Engine/Engine.h"

int32 ACryptoTerminalActor::GlobalScrollOffset = 0;

ACryptoTerminalActor::ACryptoTerminalActor()
{
    PrimaryActorTick.bCanEverTick = true;
}

void ACryptoTerminalActor::BeginPlay()
{
    Super::BeginPlay();
    UE_LOG(LogTemp, Log, TEXT("[Vantage] Master CryptoTerminalActor inicializado gestionando 5 slots."));
}

void ACryptoTerminalActor::SetGlobalScrollOffset(int32 NewOffset)
{
    GlobalScrollOffset = FMath::Max(0, NewOffset);
}

int32 ACryptoTerminalActor::GetGlobalScrollOffset()
{
    return GlobalScrollOffset;
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
            TArray<FCryptoAssetData> AllAssets;
            FinanceSubsystem->GetAllAssets(AllAssets);

            if (AllAssets.Num() > 0)
            {
                // Iteramos en un bucle cerrado de 0 a 4 para los 5 slots simultáneos
                for (int32 i = 0; i < DisplayLimit; ++i)
                {
                    int32 TargetIndex = GlobalScrollOffset + i;

                    if (TargetIndex < AllAssets.Num())
                    {
                        const FCryptoAssetData& Data = AllAssets[TargetIndex];

                        FString DebugMessage = FString::Printf(
                            TEXT("=== [SLOT %d] ===\n"
                                "Símbolo: %s | Precio: $%.2f | Vol: %.2f"),
                            i,
                            *Data.Symbol,
                            Data.LastPrice,
                            Data.Volume
                        );

                        if (GEngine)
                        {
                            // Imprime cada slot de manera independiente en pantalla sin solaparse
                            GEngine->AddOnScreenDebugMessage(10 + i, PrintInterval + 0.1f, FColor::Cyan, DebugMessage);
                        }
                    }
                }
            }
        }
    }
}